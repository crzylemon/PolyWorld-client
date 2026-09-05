/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: physics.c                                                                           |
|   Purpose: Jolt. Used to be ODE. Some comments still lie.                                   |
\*-------------------------------------------------------------------------------------------*/

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "JoltC/JoltC.h"
#include "physics.h"
#include "protocol.h"

#define PI_F 3.14159265358979323846f
#define RAD_TO_DEG (180.0f / PI_F)
#define DEG_TO_RAD (PI_F / 180.0f)

#define PW_WELD_MAX_LIN_SPEED 55.0f
#define PW_WELD_MAX_ANG_SPEED 8.0f
#define PW_WELD_MAX_POS       80000.0f
#define PW_PHYSICS_COLLISION_STEPS 1

#define LAYER_STATIC   0
#define LAYER_DYNAMIC  1
#define LAYER_PLAYER   2
#define LAYER_RAGDOLL  3
#define LAYER_DEBRIS   4
#define LAYER_DISABLED 5
#define LAYER_COUNT    6

typedef struct {
    bool active;
    BodyType type;
    ColliderType collider;
    JPC_BodyID jolt_id;
    Vec3 half_extents;
    float radius;
    float mass;
    float restitution;
    float friction;
    unsigned long category;
    unsigned long collide;
    bool disabled;
    bool lock_rotation;
    bool never_sleep;
    JPC_ObjectLayer base_layer;
    int hull_point_count;
    Vec3 hull_points[BODY_DESC_MAX_HULL_POINTS];
    bool island_member;
    bool island_body_owner;
    Vec3 island_local_pos;
    float island_lq[4];
} PhysicsSlot;

struct PhysicsWorld {
    JPC_PhysicsSystem* system;
    JPC_BodyInterface* bi;
    JPC_TempAllocatorImpl* temp;
    JPC_JobSystemSingleThreaded* jobs;
    JPC_BroadPhaseLayerInterface* bpl;
    JPC_ObjectVsBroadPhaseLayerFilter* ovbpl;
    JPC_ObjectLayerPairFilter* olpf;
    JPC_ObjectLayerFilter* ray_ol_filter;

    PhysicsSlot slots[MAX_PHYSICS_BODIES];
    uint32_t count;
    Vec3 gravity;

    struct {
        PhysicsBodyID body_a, body_b;
        bool active;
        bool welded;
        uint8_t type;
        JPC_Constraint* constraint;
        ConstraintDesc desc;
        Vec3 local_a, local_b, local_axis;
        bool locals_set;
    } connectors[MAX_CONNECTORS];
    uint32_t connector_count;
    uint32_t weld_island[MAX_PHYSICS_BODIES];
    uint32_t next_weld_island;
    int weld_batch;
    bool weld_compound_dirty;

    JPC_ContactListener* contacts;
};

static PhysicsWorld* g_legacy_world = NULL;
static bool g_jolt_init = false;

static PhysicsBodyID body_id_from_jolt(const PhysicsWorld* world, JPC_BodyID jid);
static PhysicsBodyID body_id_from_body_ptr(const PhysicsWorld* world, const JPC_Body* body);
static void physics_rebuild_weld_islands(PhysicsWorld* world);
static void physics_flush_weld_compounds(PhysicsWorld* world);
static void physics_sanitize_dynamics(PhysicsWorld* world);
static void island_dissolve_jolt(PhysicsWorld* world, JPC_BodyID bid);
static uint32_t pw_uf_find(uint32_t* parent, uint32_t x);
static void pw_uf_union(uint32_t* parent, uint32_t a, uint32_t b);

static JPC_Vec3 pw_clamp_vec3(JPC_Vec3 v, float max_len) {
    float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (!isfinite(len2)) return (JPC_Vec3){ 0, 0, 0, 0 };
    if (len2 > max_len * max_len && len2 > 0.0f) {
        float s = max_len / sqrtf(len2);
        v.x *= s; v.y *= s; v.z *= s;
    }
    return v;
}

static int pw_pose_sane(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z) &&
           fabsf(x) < PW_WELD_MAX_POS && fabsf(y) < PW_WELD_MAX_POS && fabsf(z) < PW_WELD_MAX_POS;
}

static float physics_slot_mass(const PhysicsSlot* s) {
    if (!s) return 1.0f;
    if (s->collider == COLLIDER_SPHERE) {
        float r = s->radius > 0.0f ? s->radius : s->half_extents.x;
        return pw_part_mass(r * 2.0f, r * 2.0f, r * 2.0f);
    }
    return pw_part_mass(s->half_extents.x * 2.0f, s->half_extents.y * 2.0f,
                        s->half_extents.z * 2.0f);
}

static float physics_rigid_mass(const PhysicsWorld* world, const PhysicsSlot* slot) {
    if (!slot) return 1.0f;
    if (!world || !slot->island_member || !slot->jolt_id)
        return physics_slot_mass(slot);
    float m = 0.0f;
    JPC_BodyID bid = slot->jolt_id;
    for (uint32_t i = 1; i < world->count; i++) {
        const PhysicsSlot* s = &world->slots[i];
        if (!s->active || s->jolt_id != bid) continue;
        m += physics_slot_mass(s);
    }
    return m > 0.0f ? m : 1.0f;
}

static void physics_apply_fall_wind(PhysicsWorld* world, float dt) {
    if (!world || dt <= 0.0f) return;
    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->active || s->type != BODY_DYNAMIC || !s->jolt_id) continue;
        if (s->island_member && !s->island_body_owner) continue;
        if (s->base_layer == LAYER_PLAYER || s->base_layer == LAYER_RAGDOLL) continue;
        if (!JPC_BodyInterface_IsActive(world->bi, s->jolt_id)) continue;
        JPC_Vec3 v = JPC_BodyInterface_GetLinearVelocity(world->bi, s->jolt_id);
        if (v.y >= -0.5f) continue;
        float wind = PW_FALL_WIND, term = PW_FALL_TERMINAL;
        pw_fall_params(physics_rigid_mass(world, s), &wind, &term);
        v.y += wind * dt;
        if (v.y < -term) v.y = -term;
        JPC_BodyInterface_SetLinearVelocity(world->bi, s->jolt_id, v);
    }
}

static void physics_sanitize_dynamics(PhysicsWorld* world) {
    if (!world) return;
    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->active || s->type != BODY_DYNAMIC || !s->jolt_id) continue;
        if (s->island_member && !s->island_body_owner) continue;
        if (!JPC_BodyInterface_IsActive(world->bi, s->jolt_id)) continue;

        JPC_RVec3 p = JPC_BodyInterface_GetPosition(world->bi, s->jolt_id);
        JPC_Quat q = JPC_BodyInterface_GetRotation(world->bi, s->jolt_id);
        if (!pw_pose_sane((float)p.x, (float)p.y, (float)p.z) ||
            !isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z) || !isfinite(q.w)) {
            JPC_BodyInterface_SetLinearVelocity(world->bi, s->jolt_id, (JPC_Vec3){0,0,0,0});
            JPC_BodyInterface_SetAngularVelocity(world->bi, s->jolt_id, (JPC_Vec3){0,0,0,0});
        }
    }
}

static bool bodies_share_weld_island(const PhysicsWorld* world, PhysicsBodyID a, PhysicsBodyID b) {
    if (!world || a == 0 || b == 0 || a == b) return false;
    if (a >= MAX_PHYSICS_BODIES || b >= MAX_PHYSICS_BODIES) return false;
    uint32_t ia = world->weld_island[a];
    uint32_t ib = world->weld_island[b];
    return ia != 0 && ia == ib;
}

static bool bodies_share_fixed_constraint(const PhysicsWorld* world, PhysicsBodyID a, PhysicsBodyID b) {
    if (bodies_share_weld_island(world, a, b)) return true;
    if (!world || a == 0 || b == 0 || a == b) return false;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active || !world->connectors[i].welded) continue;
        PhysicsBodyID ca = world->connectors[i].body_a;
        PhysicsBodyID cb = world->connectors[i].body_b;
        if ((ca == a && cb == b) || (ca == b && cb == a)) return true;
    }
    return false;
}

static bool contact_is_kinematic_vs_dynamic(const JPC_Body* b1, const JPC_Body* b2) {
    if (!b1 || !b2) return false;
    JPC_MotionType m1 = JPC_Body_GetMotionType(b1);
    JPC_MotionType m2 = JPC_Body_GetMotionType(b2);
    return (m1 == JPC_MOTION_TYPE_KINEMATIC && m2 == JPC_MOTION_TYPE_DYNAMIC) ||
           (m1 == JPC_MOTION_TYPE_DYNAMIC && m2 == JPC_MOTION_TYPE_KINEMATIC);
}

static JPC_ValidateResult client_OnContactValidate(
    void* self,
    const JPC_Body* inBody1,
    const JPC_Body* inBody2,
    JPC_RVec3 inBaseOffset,
    const JPC_CollideShapeResult* inCollisionResult)
{
    (void)inBaseOffset; (void)inCollisionResult;
    PhysicsWorld* world = (PhysicsWorld*)self;
    if (!world) return JPC_VALIDATE_RESULT_ACCEPT_ALL_CONTACTS;
    if (contact_is_kinematic_vs_dynamic(inBody1, inBody2))
        return JPC_VALIDATE_RESULT_REJECT_ALL_CONTACTS;
    PhysicsBodyID a = body_id_from_body_ptr(world, inBody1);
    PhysicsBodyID b = body_id_from_body_ptr(world, inBody2);
    if (bodies_share_fixed_constraint(world, a, b))
        return JPC_VALIDATE_RESULT_REJECT_ALL_CONTACTS;
    if (world && a && b) {
        for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
            if (!world->connectors[i].active) continue;
            if (!constraint_disables_collision(world->connectors[i].type)) continue;
            PhysicsBodyID ca = world->connectors[i].body_a;
            PhysicsBodyID cb = world->connectors[i].body_b;
            if ((ca == a && cb == b) || (ca == b && cb == a))
                return JPC_VALIDATE_RESULT_REJECT_ALL_CONTACTS;
        }
    }
    return JPC_VALIDATE_RESULT_ACCEPT_ALL_CONTACTS;
}

static void client_OnContactAdded(
    void* self,
    const JPC_Body* inBody1,
    const JPC_Body* inBody2,
    const JPC_ContactManifold* inManifold,
    JPC_ContactSettings* ioSettings)
{
    (void)inManifold;
    if (!ioSettings) return;
    PhysicsWorld* world = (PhysicsWorld*)self;
    if (contact_is_kinematic_vs_dynamic(inBody1, inBody2)) {
        ioSettings->CombinedRestitution = 0.0f;
        ioSettings->IsSensor = true;
        return;
    }
    PhysicsBodyID a = body_id_from_body_ptr(world, inBody1);
    PhysicsBodyID b = body_id_from_body_ptr(world, inBody2);
    if (bodies_share_fixed_constraint(world, a, b)) {
        ioSettings->CombinedRestitution = 0.0f;
        return;
    }
    JPC_ObjectLayer l1 = JPC_Body_GetObjectLayer(inBody1);
    JPC_ObjectLayer l2 = JPC_Body_GetObjectLayer(inBody2);
    if (l1 != LAYER_RAGDOLL && l2 != LAYER_RAGDOLL &&
        l1 != LAYER_DEBRIS && l2 != LAYER_DEBRIS)
        ioSettings->CombinedRestitution = 0.0f;
}

static uint bpl_GetNum(const void* s) { (void)s; return 2; }
static JPC_BroadPhaseLayer bpl_Get(const void* s, JPC_ObjectLayer l) {
    (void)s;
    return (l == LAYER_STATIC || l == LAYER_DISABLED) ? 0 : 1;
}

static bool ovbpl_Filter(const void* s, JPC_ObjectLayer l1, JPC_BroadPhaseLayer l2) {
    (void)s;
    if (l1 == LAYER_DISABLED) return false;
    if (l1 == LAYER_STATIC) return l2 == 1;
    return true;
}

static bool olpf_Filter(const void* s, JPC_ObjectLayer l1, JPC_ObjectLayer l2) {
    (void)s;
    if (l1 == LAYER_DISABLED || l2 == LAYER_DISABLED) return false;
    if (l1 == LAYER_STATIC && l2 == LAYER_STATIC) return false;

    if ((l1 == LAYER_PLAYER && l2 == LAYER_DYNAMIC) || (l1 == LAYER_DYNAMIC && l2 == LAYER_PLAYER))
        return false;
    if ((l1 == LAYER_PLAYER && l2 == LAYER_RAGDOLL) || (l1 == LAYER_RAGDOLL && l2 == LAYER_PLAYER))
        return false;
    if ((l1 == LAYER_PLAYER && l2 == LAYER_DEBRIS) || (l1 == LAYER_DEBRIS && l2 == LAYER_PLAYER))
        return false;
    if ((l1 == LAYER_RAGDOLL && l2 == LAYER_DEBRIS) || (l1 == LAYER_DEBRIS && l2 == LAYER_RAGDOLL))
        return false;
    return true;
}

static bool ray_ol_ShouldCollide(const void* s, JPC_ObjectLayer layer) {
    (void)s;
    return layer != LAYER_DISABLED;
}

static void ensure_jolt_init(void) {
    if (g_jolt_init) return;
    JPC_RegisterDefaultAllocator();
    JPC_FactoryInit();
    JPC_RegisterTypes();
    g_jolt_init = true;
}

static JPC_ObjectLayer layer_for_type(BodyType type) {
    if (type == BODY_STATIC) return LAYER_STATIC;

    if (type == BODY_KINEMATIC) return LAYER_PLAYER;
    return LAYER_DYNAMIC;
}

static JPC_ObjectLayer layer_from_bits(unsigned long category, BodyType type) {
    if (category & 0x1UL) return LAYER_PLAYER;
    if (category & 0x2UL) return LAYER_RAGDOLL;
    if (category & 0x4UL) return LAYER_DEBRIS;
    return layer_for_type(type);
}

static JPC_Quat quat_from_euler_zyx(float pitch_deg, float yaw_deg, float roll_deg) {
    float rx = pitch_deg * DEG_TO_RAD;
    float ry = yaw_deg * DEG_TO_RAD;
    float rz = roll_deg * DEG_TO_RAD;
    float cx = cosf(rx * 0.5f), sx = sinf(rx * 0.5f);
    float cy = cosf(ry * 0.5f), sy = sinf(ry * 0.5f);
    float cz = cosf(rz * 0.5f), sz = sinf(rz * 0.5f);

    return (JPC_Quat){
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    };
}

static Vec3 quat_to_euler_zyx(JPC_Quat q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float sinp = 2.0f * (w * y - z * x);
    float pitch, yaw, roll;
    if (fabsf(sinp) >= 0.9999f) {
        yaw = copysignf(PI_F * 0.5f, sinp);
        pitch = 0.0f;
        roll = atan2f(-2.0f * (x * y - w * z), 1.0f - 2.0f * (y * y + z * z));
    } else {
        yaw = asinf(sinp);
        pitch = atan2f(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y));
        roll = atan2f(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));
    }
    return (Vec3){ pitch * RAD_TO_DEG, yaw * RAD_TO_DEG, roll * RAD_TO_DEG };
}

static Mat4 mat4_from_pos_quat(JPC_RVec3 pos, JPC_Quat q) {
    Mat4 out = mat4_identity();
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    out.m[0] = 1.0f - (yy + zz);
    out.m[1] = xy + wz;
    out.m[2] = xz - wy;
    out.m[3] = 0.0f;
    out.m[4] = xy - wz;
    out.m[5] = 1.0f - (xx + zz);
    out.m[6] = yz + wx;
    out.m[7] = 0.0f;
    out.m[8] = xz + wy;
    out.m[9] = yz - wx;
    out.m[10] = 1.0f - (xx + yy);
    out.m[11] = 0.0f;
    out.m[12] = (float)pos.x;
    out.m[13] = (float)pos.y;
    out.m[14] = (float)pos.z;
    out.m[15] = 1.0f;
    return out;
}

static JPC_Quat pw_qmul(JPC_Quat a, JPC_Quat b) {
    return (JPC_Quat){
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}
static JPC_Quat pw_qconj(JPC_Quat q) { return (JPC_Quat){ -q.x, -q.y, -q.z, q.w }; }
static void pw_qnorm(JPC_Quat* q) {
    float n = sqrtf(q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w);
    if (n < 1e-8f) { *q = (JPC_Quat){0,0,0,1}; return; }
    q->x/=n; q->y/=n; q->z/=n; q->w/=n;
}
static JPC_Vec3 pw_qrot(JPC_Quat q, JPC_Vec3 v) {
    JPC_Quat p = { v.x, v.y, v.z, 0 };
    JPC_Quat r = pw_qmul(pw_qmul(q, p), pw_qconj(q));
    return (JPC_Vec3){ r.x, r.y, r.z, 0 };
}

static PhysicsWorld* resolve_world(PhysicsWorld* world) {
    if (!world || world == (PhysicsWorld*)1) return g_legacy_world;
    return world;
}

static const PhysicsWorld* resolve_world_c(const PhysicsWorld* world) {
    if (!world || world == (const PhysicsWorld*)1) return g_legacy_world;
    return world;
}

static PhysicsSlot* slot_get(PhysicsWorld* world, PhysicsBodyID id) {
    if (!world || id == PHYSICS_BODY_INVALID || id >= MAX_PHYSICS_BODIES) return NULL;
    PhysicsSlot* s = &world->slots[id];
    return s->active ? s : NULL;
}

static const PhysicsSlot* slot_get_c(const PhysicsWorld* world, PhysicsBodyID id) {
    if (!world || id == PHYSICS_BODY_INVALID || id >= MAX_PHYSICS_BODIES) return NULL;
    const PhysicsSlot* s = &world->slots[id];
    return s->active ? s : NULL;
}

static bool slot_jolt_valid(const PhysicsWorld* world, JPC_BodyID bid) {
    if (!world || bid == 0 || bid == 0xffffffffu) return false;
    return JPC_BodyInterface_IsAdded(world->bi, bid);
}

static void slot_get_part_pose(const PhysicsWorld* world, const PhysicsSlot* slot,
                               JPC_RVec3* pos, JPC_Quat* rot) {
    JPC_BodyInterface_GetPositionAndRotation(world->bi, slot->jolt_id, pos, rot);
    if (!slot->island_member) return;
    JPC_Quat lq = {
        slot->island_lq[0], slot->island_lq[1],
        slot->island_lq[2], slot->island_lq[3]
    };
    pw_qnorm(&lq);
    JPC_Vec3 lp = { slot->island_local_pos.x, slot->island_local_pos.y,
                    slot->island_local_pos.z, 0 };
    JPC_Vec3 off = pw_qrot(*rot, lp);
    pos->x += off.x;
    pos->y += off.y;
    pos->z += off.z;
    *rot = pw_qmul(*rot, lq);
    pw_qnorm(rot);
}

static void slot_set_part_pose(PhysicsWorld* world, PhysicsSlot* slot,
                               JPC_RVec3 pos, JPC_Quat rot, JPC_Activation act) {
    if (!slot->island_member) {
        JPC_BodyInterface_SetPositionAndRotation(world->bi, slot->jolt_id, pos, rot, act);
        return;
    }
    JPC_Quat lq = {
        slot->island_lq[0], slot->island_lq[1],
        slot->island_lq[2], slot->island_lq[3]
    };
    pw_qnorm(&lq);
    JPC_Quat q_com = pw_qmul(rot, pw_qconj(lq));
    pw_qnorm(&q_com);
    JPC_Vec3 lp = { slot->island_local_pos.x, slot->island_local_pos.y,
                    slot->island_local_pos.z, 0 };
    JPC_Vec3 off = pw_qrot(q_com, lp);
    JPC_RVec3 com = { pos.x - off.x, pos.y - off.y, pos.z - off.z, 0 };
    JPC_BodyInterface_SetPositionAndRotation(world->bi, slot->jolt_id, com, q_com, act);
}

static void island_clear_slot(PhysicsSlot* s) {
    if (!s) return;
    s->island_member = false;
    s->island_body_owner = false;
    s->island_local_pos = (Vec3){ 0, 0, 0 };
    s->island_lq[0] = s->island_lq[1] = s->island_lq[2] = 0.0f;
    s->island_lq[3] = 1.0f;
}

static PhysicsBodyID body_id_from_jolt(const PhysicsWorld* world, JPC_BodyID jid) {
    if (!world || jid == 0 || jid == 0xFFFFFFFFu) return PHYSICS_BODY_INVALID;
    uint64_t ud = JPC_BodyInterface_GetUserData(world->bi, jid);
    if (ud == 0 || ud >= MAX_PHYSICS_BODIES) return PHYSICS_BODY_INVALID;
    if (!world->slots[ud].active) return PHYSICS_BODY_INVALID;
    return (PhysicsBodyID)ud;
}

static PhysicsBodyID body_id_from_body_ptr(const PhysicsWorld* world, const JPC_Body* body) {
    if (!world || !body) return PHYSICS_BODY_INVALID;
    uint64_t ud = JPC_Body_GetUserData(body);
    if (ud == 0 || ud >= MAX_PHYSICS_BODIES) return PHYSICS_BODY_INVALID;
    if (!world->slots[ud].active) return PHYSICS_BODY_INVALID;
    return (PhysicsBodyID)ud;
}

PhysicsWorld* physics_create(Vec3 gravity) {
    ensure_jolt_init();

    PhysicsWorld* pw = (PhysicsWorld*)calloc(1, sizeof(PhysicsWorld));
    if (!pw) return NULL;

    pw->temp = JPC_TempAllocatorImpl_new(32 * 1024 * 1024);
    pw->jobs = JPC_JobSystemSingleThreaded_new(2048);

    JPC_BroadPhaseLayerInterfaceFns bpl_fns = { bpl_GetNum, bpl_Get };
    pw->bpl = JPC_BroadPhaseLayerInterface_new(NULL, bpl_fns);
    JPC_ObjectVsBroadPhaseLayerFilterFns ovbpl_fns = { ovbpl_Filter };
    pw->ovbpl = JPC_ObjectVsBroadPhaseLayerFilter_new(NULL, ovbpl_fns);
    JPC_ObjectLayerPairFilterFns olpf_fns = { olpf_Filter };
    pw->olpf = JPC_ObjectLayerPairFilter_new(NULL, olpf_fns);
    JPC_ObjectLayerFilterFns ray_ol_fns = { ray_ol_ShouldCollide };
    pw->ray_ol_filter = JPC_ObjectLayerFilter_new(NULL, ray_ol_fns);

    pw->system = JPC_PhysicsSystem_new();
    JPC_PhysicsSystem_Init(pw->system, 8192, 0, 8192, 8192, pw->bpl, pw->ovbpl, pw->olpf);
    JPC_PhysicsSystem_SetGravity(pw->system,
        (JPC_Vec3){ gravity.x, gravity.y, gravity.z, 0 });
    pw->bi = JPC_PhysicsSystem_GetBodyInterface(pw->system);
    pw->gravity = gravity;
    pw->count = 1;
    {
        JPC_ContactListenerFns cfns;
        memset(&cfns, 0, sizeof(cfns));
        cfns.OnContactValidate = client_OnContactValidate;
        cfns.OnContactAdded = client_OnContactAdded;
        cfns.OnContactPersisted = client_OnContactAdded;
        pw->contacts = JPC_ContactListener_new(pw, cfns);
        JPC_PhysicsSystem_SetContactListener(pw->system, pw->contacts);
    }
    return pw;
}

void physics_destroy(PhysicsWorld* world) {
    if (!world) return;

    physics_clear_connectors(world);
    for (uint32_t i = 1; i < MAX_PHYSICS_BODIES; i++) {
        if (world->slots[i].active)
            physics_destroy_body(world, i);
    }

    if (world->system) {
        JPC_PhysicsSystem_SetContactListener(world->system, NULL);
        JPC_PhysicsSystem_delete(world->system);
    }
    if (world->contacts) JPC_ContactListener_delete(world->contacts);
    if (world->temp) JPC_TempAllocatorImpl_delete(world->temp);
    if (world->jobs) JPC_JobSystemSingleThreaded_delete(world->jobs);
    if (world->bpl) JPC_BroadPhaseLayerInterface_delete(world->bpl);
    if (world->ovbpl) JPC_ObjectVsBroadPhaseLayerFilter_delete(world->ovbpl);
    if (world->olpf) JPC_ObjectLayerPairFilter_delete(world->olpf);
    if (world->ray_ol_filter) JPC_ObjectLayerFilter_delete(world->ray_ol_filter);

    if (g_legacy_world == world) g_legacy_world = NULL;
    free(world);
}

bool physics_init(PhysicsWorld* world, Vec3 gravity) {
    (void)world;
    PhysicsWorld* created = physics_create(gravity);
    if (!created) return false;
    g_legacy_world = created;
    return true;
}

void physics_shutdown(PhysicsWorld* world) {
    (void)world;
    if (g_legacy_world) {
        physics_destroy(g_legacy_world);
        g_legacy_world = NULL;
    }
}

static JPC_Shape* make_cylinder_shape(float radius, float half_height, float mass) {
    if (radius < 0.01f) radius = 0.01f;
    if (half_height < 0.01f) half_height = 0.01f;
    if (mass < 0.01f) mass = 1.0f;
    JPC_CylinderShapeSettings cs;
    JPC_CylinderShapeSettings_default(&cs);
    cs.HalfHeight = half_height;
    cs.Radius = radius;
    cs.ConvexRadius = 0.01f;
    float vol = PI_F * radius * radius * half_height * 2.0f;
    if (vol < 1e-6f) vol = 1e-6f;
    cs.Density = mass / vol;
    JPC_Shape* shape = NULL;
    JPC_String* err = NULL;
    if (!JPC_CylinderShapeSettings_Create(&cs, &shape, &err)) {
        if (err) { fprintf(stderr, "[Jolt] cylinder: %s\n", JPC_String_c_str(err)); JPC_String_delete(err); }
        return NULL;
    }
    return shape;
}

PhysicsBodyID physics_create_body(PhysicsWorld* world, const BodyDesc* desc) {
    world = resolve_world(world);
    if (!world || !desc) return PHYSICS_BODY_INVALID;

    for (uint32_t i = 1; i < MAX_PHYSICS_BODIES; i++) {
        if (world->slots[i].active) continue;

        PhysicsSlot* slot = &world->slots[i];
        memset(slot, 0, sizeof(*slot));
        slot->active = true;
        slot->type = desc->type;
        slot->collider = desc->collider;
        slot->half_extents = desc->half_extents;
        slot->radius = desc->radius;
        slot->mass = desc->mass > 0.01f ? desc->mass : 1.0f;
        slot->restitution = desc->restitution;
        slot->friction = desc->friction > 0.0f ? desc->friction : 1.0f;
        slot->category = 0;
        slot->collide = ~0UL;
        slot->base_layer = layer_for_type(desc->type);
        slot->island_lq[3] = 1.0f;
        if (desc->collider == COLLIDER_HULL && desc->hull_point_count >= 3) {
            const Vec3* src = desc->hull_points_ext ? desc->hull_points_ext : desc->hull_points;
            int n = desc->hull_point_count;
            if (n > BODY_DESC_MAX_HULL_POINTS) n = BODY_DESC_MAX_HULL_POINTS;
            slot->hull_point_count = n;
            memcpy(slot->hull_points, src, (size_t)n * sizeof(Vec3));
        }

        JPC_Shape* shape = NULL;
        JPC_String* err = NULL;
        float mass = slot->mass;

        if (desc->collider == COLLIDER_SPHERE) {
            float r = desc->radius > 0.01f ? desc->radius : 0.01f;
            JPC_SphereShapeSettings ss;
            JPC_SphereShapeSettings_default(&ss);
            ss.Radius = r;
            ss.Density = mass / ((4.0f / 3.0f) * PI_F * r * r * r);
            if (!JPC_SphereShapeSettings_Create(&ss, &shape, &err)) {
                if (err) { fprintf(stderr, "[Jolt] sphere: %s\n", JPC_String_c_str(err)); JPC_String_delete(err); }
                slot->active = false;
                return PHYSICS_BODY_INVALID;
            }
        } else if (desc->collider == COLLIDER_CYLINDER) {
            float r = desc->radius > 0.01f ? desc->radius : 0.01f;
            float hh = fabsf(desc->half_extents.y);
            shape = make_cylinder_shape(r, hh, mass);
            if (!shape) {
                slot->active = false;
                return PHYSICS_BODY_INVALID;
            }
        } else if (desc->collider == COLLIDER_HULL && desc->hull_point_count >= 3) {
            const Vec3* src = desc->hull_points_ext ? desc->hull_points_ext : desc->hull_points;
            int n = desc->hull_point_count;
            int nmax = desc->hull_points_ext ? 256 : BODY_DESC_MAX_HULL_POINTS;
            if (n > nmax) n = nmax;
            JPC_Vec3 pts[256];
            float bmin[3] = { 1e30f, 1e30f, 1e30f };
            float bmax[3] = { -1e30f, -1e30f, -1e30f };
            for (int pi = 0; pi < n; pi++) {
                float px = src[pi].x;
                float py = src[pi].y;
                float pz = src[pi].z;
                pts[pi] = (JPC_Vec3){ px, py, pz, 0 };
                if (px < bmin[0]) bmin[0] = px;
                if (px > bmax[0]) bmax[0] = px;
                if (py < bmin[1]) bmin[1] = py;
                if (py > bmax[1]) bmax[1] = py;
                if (pz < bmin[2]) bmin[2] = pz;
                if (pz > bmax[2]) bmax[2] = pz;
            }
            float vol = (bmax[0] - bmin[0]) * (bmax[1] - bmin[1]) * (bmax[2] - bmin[2]);
            if (vol < 1e-6f) vol = 1e-6f;
            JPC_ConvexHullShapeSettings hs;
            JPC_ConvexHullShapeSettings_default(&hs);
            hs.Points = pts;
            hs.PointsLen = (size_t)n;
            hs.MaxConvexRadius = 0.01f;
            hs.Density = mass / vol;
            if (!JPC_ConvexHullShapeSettings_Create(&hs, &shape, &err)) {
                if (err) { fprintf(stderr, "[Jolt] hull: %s\n", JPC_String_c_str(err)); JPC_String_delete(err); }
                slot->active = false;
                return PHYSICS_BODY_INVALID;
            }
        } else {
            float hx = fabsf(desc->half_extents.x); if (hx < 0.01f) hx = 0.01f;
            float hy = fabsf(desc->half_extents.y); if (hy < 0.01f) hy = 0.01f;
            float hz = fabsf(desc->half_extents.z); if (hz < 0.01f) hz = 0.01f;
            JPC_BoxShapeSettings bs;
            JPC_BoxShapeSettings_default(&bs);
            bs.HalfExtent = (JPC_Vec3){ hx, hy, hz, 0 };
            bs.ConvexRadius = 0.01f;
            bs.Density = mass / (hx * 2.0f * hy * 2.0f * hz * 2.0f);
            if (!JPC_BoxShapeSettings_Create(&bs, &shape, &err)) {
                if (err) { fprintf(stderr, "[Jolt] box: %s\n", JPC_String_c_str(err)); JPC_String_delete(err); }
                slot->active = false;
                return PHYSICS_BODY_INVALID;
            }
        }

        JPC_BodyCreationSettings bcs;
        JPC_BodyCreationSettings_default(&bcs);
        bcs.Position = (JPC_RVec3){ desc->position.x, desc->position.y, desc->position.z, 0 };
        bcs.Rotation = (JPC_Quat){ 0, 0, 0, 1 };
        bcs.Shape = shape;
        bcs.UserData = (uint64_t)i;
        bcs.ObjectLayer = slot->base_layer;
        bcs.Friction = slot->friction;
        bcs.Restitution = slot->restitution;
        bcs.LinearDamping = 0.01f;
        bcs.AngularDamping = 0.05f;
        bcs.AllowSleeping = true;
        bcs.AllowDynamicOrKinematic = true;
        bcs.CollideKinematicVsNonDynamic = true;

        if (desc->type == BODY_STATIC) {
            bcs.MotionType = JPC_MOTION_TYPE_STATIC;
        } else if (desc->type == BODY_KINEMATIC) {
            bcs.MotionType = JPC_MOTION_TYPE_KINEMATIC;
        } else {
            bcs.MotionType = JPC_MOTION_TYPE_DYNAMIC;
        }

        JPC_Activation act = (desc->type == BODY_STATIC)
            ? JPC_ACTIVATION_DONT_ACTIVATE
            : JPC_ACTIVATION_ACTIVATE;
        slot->jolt_id = JPC_BodyInterface_CreateAndAddBody(world->bi, &bcs, act);
        JPC_Shape_Release(shape);

        if (slot->jolt_id == 0xffffffffu) {
            slot->active = false;
            slot->jolt_id = 0;
            return PHYSICS_BODY_INVALID;
        }

        if (i >= world->count) world->count = i + 1;
        return (PhysicsBodyID)i;
    }
    return PHYSICS_BODY_INVALID;
}

void physics_destroy_body(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;

    physics_break_connectors_for_body(world, id);
    physics_flush_weld_compounds(world);

    if (slot->island_member && slot->jolt_id)
        island_dissolve_jolt(world, slot->jolt_id);

    if (slot->jolt_id) {
        JPC_BodyID bid = slot->jolt_id;
        bool shared = false;
        for (uint32_t i = 1; i < world->count; i++) {
            if (i == id || !world->slots[i].active) continue;
            if (world->slots[i].jolt_id == bid) { shared = true; break; }
        }
        if (!shared && slot_jolt_valid(world, bid)) {
            JPC_BodyInterface_RemoveBody(world->bi, bid);
            JPC_BodyInterface_DestroyBody(world->bi, bid);
        }
        slot->jolt_id = 0;
    }
    island_clear_slot(slot);
    slot->active = false;
}

void physics_step(PhysicsWorld* world, float dt) {
    world = resolve_world(world);
    if (!world || dt <= 0.0f) return;

    physics_flush_weld_compounds(world);

    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->active || !s->lock_rotation || !s->jolt_id) continue;
        if (s->island_member && !s->island_body_owner) continue;
        JPC_BodyInterface_SetAngularVelocity(world->bi, s->jolt_id, (JPC_Vec3){ 0, 0, 0, 0 });
    }

    JPC_PhysicsSystem_Update(world->system, dt, PW_PHYSICS_COLLISION_STEPS, world->temp, (JPC_JobSystem*)world->jobs);
    physics_apply_fall_wind(world, dt);
    physics_sanitize_dynamics(world);
}

Vec3 physics_get_position(const PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot || !slot->jolt_id) return (Vec3){ 0, 0, 0 };
    JPC_RVec3 p;
    JPC_Quat q;
    slot_get_part_pose(world, slot, &p, &q);
    return (Vec3){ (float)p.x, (float)p.y, (float)p.z };
}

Vec3 physics_get_rotation(const PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot || !slot->jolt_id) return (Vec3){ 0, 0, 0 };
    JPC_RVec3 p;
    JPC_Quat q;
    slot_get_part_pose(world, slot, &p, &q);
    return quat_to_euler_zyx(q);
}

Mat4 physics_get_transform_mat4(const PhysicsWorld* world, PhysicsBodyID id) {
    Mat4 out = mat4_identity();
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot || !slot->jolt_id) return out;
    JPC_RVec3 p;
    JPC_Quat q;
    slot_get_part_pose(world, slot, &p, &q);
    return mat4_from_pos_quat(p, q);
}

void physics_get_rotation_euler(const PhysicsWorld* world, PhysicsBodyID id, Vec3* euler_out) {
    if (!euler_out) return;
    *euler_out = physics_get_rotation(world, id);
}

Vec3 physics_get_velocity(const PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot) return (Vec3){ 0, 0, 0 };
    JPC_Vec3 v = JPC_BodyInterface_GetLinearVelocity(world->bi, slot->jolt_id);
    return (Vec3){ v.x, v.y, v.z };
}

Vec3 physics_get_angular_velocity(const PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot) return (Vec3){ 0, 0, 0 };
    JPC_Vec3 v = JPC_BodyInterface_GetAngularVelocity(world->bi, slot->jolt_id);
    return (Vec3){ v.x, v.y, v.z };
}

float physics_get_mass(const PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot) return 0.0f;
    return physics_rigid_mass(world, slot);
}

bool physics_same_rigid_body(const PhysicsWorld* world, PhysicsBodyID a, PhysicsBodyID b) {
    if (a == b) return true;
    if (!a || !b) return false;
    world = resolve_world_c(world);
    const PhysicsSlot* sa = slot_get_c(world, a);
    const PhysicsSlot* sb = slot_get_c(world, b);
    if (!sa || !sb || !sa->jolt_id || !sb->jolt_id) return false;
    return sa->jolt_id == sb->jolt_id;
}

bool physics_is_on_ground(const PhysicsWorld* world, PhysicsBodyID id) {
    (void)world;
    (void)id;
    return false;
}

void physics_set_position(PhysicsWorld* world, PhysicsBodyID id, Vec3 pos) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->jolt_id) return;
    JPC_RVec3 p;
    JPC_Quat q;
    slot_get_part_pose(world, slot, &p, &q);
    p = (JPC_RVec3){ pos.x, pos.y, pos.z, 0 };
    JPC_Activation act = (slot->type == BODY_STATIC)
        ? JPC_ACTIVATION_DONT_ACTIVATE
        : JPC_ACTIVATION_ACTIVATE;
    slot_set_part_pose(world, slot, p, q, act);
}

void physics_set_velocity(PhysicsWorld* world, PhysicsBodyID id, Vec3 velocity) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    JPC_BodyInterface_SetLinearVelocity(world->bi, slot->jolt_id,
        (JPC_Vec3){ velocity.x, velocity.y, velocity.z, 0 });
}

static void island_set_member_motion(PhysicsWorld* world, JPC_BodyID bid,
                                     BodyType type, JPC_ObjectLayer layer, float restitution) {
    if (!world || !bid) return;
    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->active || s->jolt_id != bid) continue;
        s->type = type;
        s->base_layer = layer;
        if (type == BODY_DYNAMIC && restitution >= 0.0f)
            s->restitution = restitution;
    }
}

void physics_make_dynamic(PhysicsWorld* world, PhysicsBodyID id, float restitution) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->jolt_id) return;

    if (slot->type == BODY_DYNAMIC && slot->base_layer == LAYER_DYNAMIC)
        return;
    if (restitution < 0.0f) restitution = 0.0f;
    JPC_BodyID bid = slot->jolt_id;
    JPC_RVec3 p;
    JPC_Quat q;
    JPC_Vec3 lin = JPC_BodyInterface_GetLinearVelocity(world->bi, bid);
    JPC_Vec3 ang = JPC_BodyInterface_GetAngularVelocity(world->bi, bid);
    JPC_BodyInterface_GetPositionAndRotation(world->bi, bid, &p, &q);
    JPC_BodyInterface_SetObjectLayer(world->bi, bid, LAYER_DYNAMIC);
    JPC_BodyInterface_SetMotionType(world->bi, bid,
        JPC_MOTION_TYPE_DYNAMIC, JPC_ACTIVATION_DONT_ACTIVATE);
    if (!slot->island_member) {
        const JPC_Shape* shape = JPC_BodyInterface_GetShape(world->bi, bid);
        if (shape)
            JPC_BodyInterface_SetShape(world->bi, bid, shape, true,
                                       JPC_ACTIVATION_DONT_ACTIVATE);
    }
    JPC_BodyInterface_SetPositionAndRotation(world->bi, bid, p, q,
                                             JPC_ACTIVATION_DONT_ACTIVATE);
    JPC_BodyInterface_SetLinearVelocity(world->bi, bid, lin);
    JPC_BodyInterface_SetAngularVelocity(world->bi, bid, ang);
    JPC_BodyInterface_SetRestitution(world->bi, bid, restitution);
    JPC_BodyInterface_SetGravityFactor(world->bi, bid, 1.0f);
    JPC_BodyInterface_ActivateBody(world->bi, bid);
    island_set_member_motion(world, bid, BODY_DYNAMIC, LAYER_DYNAMIC, restitution);
}

void physics_make_static(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->jolt_id) return;
    if (slot->type == BODY_STATIC && slot->base_layer == LAYER_STATIC)
        return;
    JPC_BodyID bid = slot->jolt_id;
    JPC_RVec3 p;
    JPC_Quat q;
    JPC_BodyInterface_GetPositionAndRotation(world->bi, bid, &p, &q);
    JPC_BodyInterface_SetLinearVelocity(world->bi, bid, (JPC_Vec3){ 0, 0, 0, 0 });
    JPC_BodyInterface_SetAngularVelocity(world->bi, bid, (JPC_Vec3){ 0, 0, 0, 0 });
    JPC_BodyInterface_SetObjectLayer(world->bi, bid, LAYER_STATIC);
    JPC_BodyInterface_SetMotionType(world->bi, bid,
        JPC_MOTION_TYPE_STATIC, JPC_ACTIVATION_DONT_ACTIVATE);
    JPC_BodyInterface_SetPositionAndRotation(world->bi, bid, p, q,
                                             JPC_ACTIVATION_DONT_ACTIVATE);
    island_set_member_motion(world, bid, BODY_STATIC, LAYER_STATIC, -1.0f);
}

void physics_set_angular_velocity(PhysicsWorld* world, PhysicsBodyID id, Vec3 angular) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    JPC_BodyInterface_SetAngularVelocity(world->bi, slot->jolt_id,
        (JPC_Vec3){ angular.x, angular.y, angular.z, 0 });
}

void physics_activate(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->jolt_id) return;
    JPC_BodyInterface_ActivateBody(world->bi, slot->jolt_id);
}

void physics_apply_impulse(PhysicsWorld* world, PhysicsBodyID id, Vec3 impulse) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    JPC_BodyInterface_AddImpulse(world->bi, slot->jolt_id,
        (JPC_Vec3){ impulse.x, impulse.y, impulse.z, 0 });
    JPC_BodyInterface_ActivateBody(world->bi, slot->jolt_id);
}

void physics_apply_force(PhysicsWorld* world, PhysicsBodyID id, Vec3 force) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    JPC_BodyInterface_AddForce(world->bi, slot->jolt_id,
        (JPC_Vec3){ force.x, force.y, force.z, 0 });
}

void physics_set_never_disable(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    slot->never_sleep = true;
    const JPC_BodyLockInterface* bli = JPC_PhysicsSystem_GetBodyLockInterface(world->system);
    JPC_BodyLockWrite* lock = JPC_BodyLockWrite_new(bli, slot->jolt_id);
    if (lock && JPC_BodyLockWrite_Succeeded(lock)) {
        JPC_Body* body = JPC_BodyLockWrite_GetBody(lock);
        if (body) JPC_Body_SetAllowSleeping(body, false);
    }
    if (lock) JPC_BodyLockWrite_delete(lock);
}

void physics_lock_rotation(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    slot->lock_rotation = true;
    JPC_BodyInterface_SetAngularVelocity(world->bi, slot->jolt_id, (JPC_Vec3){ 0, 0, 0, 0 });
}

void physics_unlock_rotation(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    slot->lock_rotation = false;
}

void physics_set_rotation_euler(PhysicsWorld* world, PhysicsBodyID id, Vec3 euler_deg) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->jolt_id) return;
    JPC_RVec3 p;
    JPC_Quat old_q;
    slot_get_part_pose(world, slot, &p, &old_q);
    JPC_Quat q = quat_from_euler_zyx(euler_deg.x, euler_deg.y, euler_deg.z);
    JPC_Activation act = (slot->type == BODY_STATIC)
        ? JPC_ACTIVATION_DONT_ACTIVATE
        : JPC_ACTIVATION_ACTIVATE;
    slot_set_part_pose(world, slot, p, q, act);
}

void physics_set_rotation_mat4(PhysicsWorld* world, PhysicsBodyID id, Mat4 rot) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;

    Vec3 x = vec3_normalize((Vec3){ rot.m[0], rot.m[1], rot.m[2] });
    Vec3 y = vec3_normalize((Vec3){ rot.m[4], rot.m[5], rot.m[6] });
    Vec3 z = vec3_normalize(vec3_cross(x, y));
    y = vec3_normalize(vec3_cross(z, x));
    float m00 = x.x, m01 = y.x, m02 = z.x;
    float m10 = x.y, m11 = y.y, m12 = z.y;
    float m20 = x.z, m21 = y.z, m22 = z.z;

    JPC_Quat q;
    float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    JPC_RVec3 p;
    JPC_Quat old_q;
    slot_get_part_pose(world, slot, &p, &old_q);
    JPC_Activation act = (slot->type == BODY_STATIC)
        ? JPC_ACTIVATION_DONT_ACTIVATE
        : JPC_ACTIVATION_ACTIVATE;
    slot_set_part_pose(world, slot, p, q, act);
}

void physics_set_geom_bits(PhysicsWorld* world, PhysicsBodyID id,
                           unsigned long category, unsigned long collide) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot) return;
    if (category != 0) slot->category = category;
    if (collide != 0) slot->collide = collide;
    slot->base_layer = layer_from_bits(slot->category, slot->type);
    if (!slot->disabled)
        JPC_BodyInterface_SetObjectLayer(world->bi, slot->jolt_id, slot->base_layer);
    (void)slot->collide;
}

void physics_add_box_geom(PhysicsWorld* world, PhysicsBodyID id, Vec3 half_extents, Vec3 offset) {
    (void)world; (void)id; (void)half_extents; (void)offset;
}

void physics_add_cylinder_geom(PhysicsWorld* world, PhysicsBodyID id,
                               float radius, float length, Vec3 offset) {
    (void)world; (void)id; (void)radius; (void)length; (void)offset;
}

void physics_disable_geom(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || slot->disabled) return;
    slot->disabled = true;
    JPC_BodyInterface_SetObjectLayer(world->bi, slot->jolt_id, LAYER_DISABLED);
}

void physics_enable_geom(PhysicsWorld* world, PhysicsBodyID id) {
    world = resolve_world(world);
    PhysicsSlot* slot = slot_get(world, id);
    if (!slot || !slot->disabled) return;
    slot->disabled = false;
    JPC_BodyInterface_SetObjectLayer(world->bi, slot->jolt_id, slot->base_layer);
}

int physics_get_contacts(PhysicsWorld* world, PhysicsBodyID id, Vec3* push_out) {
    (void)world;
    (void)id;
    if (push_out) *push_out = (Vec3){ 0, 0, 0 };
    return 0;
}

RaycastHit physics_raycast(const PhysicsWorld* world, Vec3 origin, Vec3 direction, float max_dist) {
    RaycastHit hit = { 0 };
    world = resolve_world_c(world);
    if (!world || max_dist <= 0.0f) return hit;

    float len = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len < 1e-8f) return hit;
    float nx = direction.x / len, ny = direction.y / len, nz = direction.z / len;

    const JPC_NarrowPhaseQuery* npq = JPC_PhysicsSystem_GetNarrowPhaseQuery(world->system);
    JPC_NarrowPhaseQuery_CastRayArgs args;
    memset(&args, 0, sizeof(args));
    args.Ray.Origin = (JPC_RVec3){ origin.x, origin.y, origin.z, 0 };
    args.Ray.Direction = (JPC_Vec3){ nx * max_dist, ny * max_dist, nz * max_dist, 0 };
    args.Result.Fraction = 1.1f;
    args.Result.BodyID = 0xFFFFFFFFu;
    args.ObjectLayerFilter = world->ray_ol_filter;

    if (!JPC_NarrowPhaseQuery_CastRay(npq, &args) || args.Result.Fraction > 1.0f)
        return hit;

    float t = args.Result.Fraction * max_dist;
    hit.hit = true;
    hit.distance = t;
    hit.point = (Vec3){
        origin.x + nx * t,
        origin.y + ny * t,
        origin.z + nz * t
    };
    hit.normal = (Vec3){ -nx, -ny, -nz };
    hit.body = body_id_from_jolt(world, args.Result.BodyID);

    if (args.Result.BodyID != 0xFFFFFFFFu) {
        const JPC_BodyLockInterface* bli = JPC_PhysicsSystem_GetBodyLockInterface(world->system);
        JPC_BodyLockRead* lock = JPC_BodyLockRead_new(bli, args.Result.BodyID);
        if (lock && JPC_BodyLockRead_Succeeded(lock)) {
            const JPC_Body* body = JPC_BodyLockRead_GetBody(lock);
            if (body) {
                JPC_RVec3 pos = { hit.point.x, hit.point.y, hit.point.z, 0 };
                JPC_Vec3 n = JPC_Body_GetWorldSpaceSurfaceNormal(body, args.Result.SubShapeID2, pos);
                float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                if (nl > 1e-5f) {
                    hit.normal = (Vec3){ n.x / nl, n.y / nl, n.z / nl };

                    if (hit.normal.x * nx + hit.normal.y * ny + hit.normal.z * nz > 0.0f)
                        hit.normal = (Vec3){ -hit.normal.x, -hit.normal.y, -hit.normal.z };
                }
            }
        }
        if (lock) JPC_BodyLockRead_delete(lock);
    }
    return hit;
}

uint32_t physics_get_body_count(const PhysicsWorld* world) {
    world = resolve_world_c(world);
    if (!world) return 0;
    uint32_t n = 0;
    for (uint32_t i = 1; i < world->count && i < MAX_PHYSICS_BODIES; i++)
        if (world->slots[i].active) n++;
    return n;
}

uint32_t physics_get_active_body_count(const PhysicsWorld* world) {
    world = resolve_world_c(world);
    if (!world) return 0;
    uint32_t n = 0;
    for (uint32_t i = 1; i < world->count && i < MAX_PHYSICS_BODIES; i++) {
        if (!world->slots[i].active) continue;
        if (JPC_BodyInterface_IsActive(world->bi, world->slots[i].jolt_id)) n++;
    }
    return n;
}

PhysicsBodyInfo physics_get_body_info(const PhysicsWorld* world, PhysicsBodyID id) {
    PhysicsBodyInfo info;
    memset(&info, 0, sizeof(info));
    world = resolve_world_c(world);
    const PhysicsSlot* slot = slot_get_c(world, id);
    if (!slot) return info;
    info.active = true;
    info.collider = slot->collider;
    info.position = physics_get_position(world, id);
    info.half_extents = slot->half_extents;
    info.radius = slot->radius;
    Mat4 m = physics_get_transform_mat4(world, id);
    memcpy(info.transform, m.m, sizeof(info.transform));
    return info;
}

static JPC_Vec3 pw_jpc_v3(float x, float y, float z) { return (JPC_Vec3){ x, y, z, 0 }; }
static JPC_RVec3 pw_jpc_rv3(float x, float y, float z) { return (JPC_RVec3){ x, y, z, 0 }; }
static JPC_Vec3 pw_jpc_norm(JPC_Vec3 v) {
    float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (n < 1e-6f) return pw_jpc_v3(0, 1, 0);
    return pw_jpc_v3(v.x / n, v.y / n, v.z / n);
}
static JPC_Vec3 pw_jpc_perp(JPC_Vec3 a) {
    JPC_Vec3 r = (fabsf(a.x) < 0.9f) ? pw_jpc_v3(1, 0, 0) : pw_jpc_v3(0, 0, 1);
    return pw_jpc_norm(pw_jpc_v3(a.y * r.z - a.z * r.y, a.z * r.x - a.x * r.z, a.x * r.y - a.y * r.x));
}

static JPC_Shape* slot_make_shape(const PhysicsSlot* slot) {
    if (!slot) return NULL;
    JPC_Shape* shape = NULL;
    JPC_String* err = NULL;
    float mass = slot->mass > 0.01f ? slot->mass : 1.0f;
    if (slot->collider == COLLIDER_SPHERE) {
        float r = slot->radius > 0.01f ? slot->radius : 0.01f;
        JPC_SphereShapeSettings ss;
        JPC_SphereShapeSettings_default(&ss);
        ss.Radius = r;
        ss.Density = mass / ((4.0f / 3.0f) * PI_F * r * r * r);
        if (!JPC_SphereShapeSettings_Create(&ss, &shape, &err)) {
            if (err) JPC_String_delete(err);
            return NULL;
        }
    } else if (slot->collider == COLLIDER_CYLINDER) {
        float r = slot->radius > 0.01f ? slot->radius : 0.01f;
        float hh = fabsf(slot->half_extents.y);
        return make_cylinder_shape(r, hh, mass);
    } else if (slot->collider == COLLIDER_HULL && slot->hull_point_count >= 3) {
        int n = slot->hull_point_count;
        if (n > BODY_DESC_MAX_HULL_POINTS) n = BODY_DESC_MAX_HULL_POINTS;
        JPC_Vec3 pts[BODY_DESC_MAX_HULL_POINTS];
        float bmin[3] = { 1e30f, 1e30f, 1e30f };
        float bmax[3] = { -1e30f, -1e30f, -1e30f };
        for (int pi = 0; pi < n; pi++) {
            float px = slot->hull_points[pi].x;
            float py = slot->hull_points[pi].y;
            float pz = slot->hull_points[pi].z;
            pts[pi] = (JPC_Vec3){ px, py, pz, 0 };
            if (px < bmin[0]) bmin[0] = px;
            if (px > bmax[0]) bmax[0] = px;
            if (py < bmin[1]) bmin[1] = py;
            if (py > bmax[1]) bmax[1] = py;
            if (pz < bmin[2]) bmin[2] = pz;
            if (pz > bmax[2]) bmax[2] = pz;
        }
        float vol = (bmax[0] - bmin[0]) * (bmax[1] - bmin[1]) * (bmax[2] - bmin[2]);
        if (vol < 1e-6f) vol = 1e-6f;
        JPC_ConvexHullShapeSettings hs;
        JPC_ConvexHullShapeSettings_default(&hs);
        hs.Points = pts;
        hs.PointsLen = (size_t)n;
        hs.MaxConvexRadius = 0.01f;
        hs.Density = mass / vol;
        if (!JPC_ConvexHullShapeSettings_Create(&hs, &shape, &err)) {
            if (err) JPC_String_delete(err);
            return NULL;
        }
    } else {
        float hx = fabsf(slot->half_extents.x); if (hx < 0.01f) hx = 0.01f;
        float hy = fabsf(slot->half_extents.y); if (hy < 0.01f) hy = 0.01f;
        float hz = fabsf(slot->half_extents.z); if (hz < 0.01f) hz = 0.01f;
        JPC_BoxShapeSettings bs;
        JPC_BoxShapeSettings_default(&bs);
        bs.HalfExtent = (JPC_Vec3){ hx, hy, hz, 0 };
        bs.ConvexRadius = 0.01f;
        bs.Density = mass / (hx * 2.0f * hy * 2.0f * hz * 2.0f);
        if (!JPC_BoxShapeSettings_Create(&bs, &shape, &err)) {
            if (err) JPC_String_delete(err);
            return NULL;
        }
    }
    return shape;
}

static void island_destroy_jolt_id(PhysicsWorld* world, JPC_BodyID bid) {
    if (!world || !slot_jolt_valid(world, bid)) return;
    JPC_BodyInterface_RemoveBody(world->bi, bid);
    JPC_BodyInterface_DestroyBody(world->bi, bid);
}

static bool slot_spawn_jolt(PhysicsWorld* world, PhysicsBodyID id,
                            JPC_RVec3 pos, JPC_Quat rot, JPC_Vec3 lin, JPC_Vec3 ang) {
    PhysicsSlot* slot = &world->slots[id];
    JPC_Shape* shape = slot_make_shape(slot);
    if (!shape) return false;
    JPC_BodyCreationSettings bcs;
    JPC_BodyCreationSettings_default(&bcs);
    bcs.Position = pos;
    bcs.Rotation = rot;
    bcs.Shape = shape;
    bcs.UserData = (uint64_t)id;
    bcs.ObjectLayer = slot->disabled ? LAYER_DISABLED : slot->base_layer;
    bcs.Friction = slot->friction;
    bcs.Restitution = slot->restitution;
    bcs.LinearDamping = 0.01f;
    bcs.AngularDamping = 0.05f;
    bcs.AllowSleeping = !slot->never_sleep;
    bcs.AllowDynamicOrKinematic = true;
    bcs.CollideKinematicVsNonDynamic = true;
    bcs.LinearVelocity = lin;
    bcs.AngularVelocity = ang;
    if (slot->type == BODY_STATIC) bcs.MotionType = JPC_MOTION_TYPE_STATIC;
    else if (slot->type == BODY_KINEMATIC) bcs.MotionType = JPC_MOTION_TYPE_KINEMATIC;
    else bcs.MotionType = JPC_MOTION_TYPE_DYNAMIC;
    JPC_Activation act = (slot->type == BODY_STATIC)
        ? JPC_ACTIVATION_DONT_ACTIVATE : JPC_ACTIVATION_ACTIVATE;
    slot->jolt_id = JPC_BodyInterface_CreateAndAddBody(world->bi, &bcs, act);
    JPC_Shape_Release(shape);
    if (slot->jolt_id == 0xffffffffu) {
        slot->jolt_id = 0;
        return false;
    }
    return true;
}

static void island_dissolve_jolt(PhysicsWorld* world, JPC_BodyID bid) {
    if (!world || !slot_jolt_valid(world, bid)) return;
    int n = 0;
    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (s->active && s->island_member && s->jolt_id == bid) n++;
    }
    if (n == 0) return;

    PhysicsBodyID* members = (PhysicsBodyID*)malloc((size_t)n * sizeof(PhysicsBodyID));
    JPC_RVec3* poses = (JPC_RVec3*)malloc((size_t)n * sizeof(JPC_RVec3));
    JPC_Quat* rots = (JPC_Quat*)malloc((size_t)n * sizeof(JPC_Quat));
    if (!members || !poses || !rots) {
        free(members); free(poses); free(rots);
        return;
    }
    JPC_Vec3 lin = JPC_BodyInterface_GetLinearVelocity(world->bi, bid);
    JPC_Vec3 ang = JPC_BodyInterface_GetAngularVelocity(world->bi, bid);
    int k = 0;
    for (uint32_t i = 1; i < world->count; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->active || !s->island_member || s->jolt_id != bid) continue;
        members[k] = i;
        slot_get_part_pose(world, s, &poses[k], &rots[k]);
        k++;
    }
    island_destroy_jolt_id(world, bid);
    for (int i = 0; i < k; i++) {
        PhysicsSlot* s = &world->slots[members[i]];
        island_clear_slot(s);
        s->jolt_id = 0;
        slot_spawn_jolt(world, members[i], poses[i], rots[i], lin, ang);
        if (s->jolt_id)
            JPC_BodyInterface_SetRestitution(world->bi, s->jolt_id, 0.0f);
    }
    free(members); free(poses); free(rots);
}

static bool island_slot_eligible(const PhysicsWorld* world, PhysicsBodyID id) {
    if (!world || id == 0 || id >= MAX_PHYSICS_BODIES) return false;
    const PhysicsSlot* s = &world->slots[id];
    if (!s->active || !s->jolt_id || s->disabled) return false;
    if (s->type != BODY_DYNAMIC) return false;
    if (s->base_layer == LAYER_PLAYER || s->base_layer == LAYER_RAGDOLL) return false;
    return slot_jolt_valid(world, s->jolt_id);
}

static bool island_already_compounded(const PhysicsWorld* world, const PhysicsBodyID* members, int count) {
    if (count < 2 || !members) return false;
    JPC_BodyID bid = world->slots[members[0]].jolt_id;
    if (!bid) return false;
    int owners = 0, matching = 0;
    for (int i = 0; i < count; i++) {
        const PhysicsSlot* s = &world->slots[members[i]];
        if (!s->island_member || s->jolt_id != bid) return false;
        matching++;
        if (s->island_body_owner) owners++;
    }
    if (owners != 1) return false;
    for (uint32_t i = 1; i < world->count; i++) {
        if (!world->slots[i].active) continue;
        if (world->slots[i].jolt_id == bid && world->slots[i].island_member)
            matching--;
    }
    return matching == 0;
}

static bool island_build_compound(PhysicsWorld* world, PhysicsBodyID* members, int count) {
    if (!world || !members || count < 2) return false;
    if (count >= (int)PW_NETOWN_MAX_ASM_PARTS) return false;

    JPC_SubShapeSettings* subs = (JPC_SubShapeSettings*)calloc((size_t)count, sizeof(JPC_SubShapeSettings));
    JPC_Shape** child_shapes = (JPC_Shape**)calloc((size_t)count, sizeof(JPC_Shape*));
    JPC_RVec3* part_pos = (JPC_RVec3*)calloc((size_t)count, sizeof(JPC_RVec3));
    JPC_Quat* part_rot = (JPC_Quat*)calloc((size_t)count, sizeof(JPC_Quat));
    if (!subs || !child_shapes || !part_pos || !part_rot) {
        free(subs); free(child_shapes); free(part_pos); free(part_rot);
        return false;
    }

    int nsub = 0;
    JPC_Vec3 avg_lin = { 0, 0, 0, 0 };
    JPC_Vec3 avg_ang = { 0, 0, 0, 0 };
    int vel_n = 0;
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;

    for (int i = 0; i < count; i++) {
        PhysicsSlot* s = &world->slots[members[i]];
        slot_get_part_pose(world, s, &part_pos[i], &part_rot[i]);
        ox += (float)part_pos[i].x;
        oy += (float)part_pos[i].y;
        oz += (float)part_pos[i].z;
    }
    float invn = 1.0f / (float)count;
    ox *= invn; oy *= invn; oz *= invn;

    for (int i = 0; i < count; i++) {
        PhysicsSlot* s = &world->slots[members[i]];
        JPC_Shape* sh = slot_make_shape(s);
        if (!sh) continue;
        JPC_SubShapeSettings_default(&subs[nsub]);
        subs[nsub].Shape = sh;
        subs[nsub].Position = (JPC_Vec3){
            (float)part_pos[i].x - ox,
            (float)part_pos[i].y - oy,
            (float)part_pos[i].z - oz, 0
        };
        subs[nsub].Rotation = part_rot[i];
        subs[nsub].UserData = members[i];
        child_shapes[nsub] = sh;
        nsub++;
        if (slot_jolt_valid(world, s->jolt_id)) {
            bool seen = false;
            for (int j = 0; j < i; j++) {
                if (world->slots[members[j]].jolt_id == s->jolt_id) { seen = true; break; }
            }
            if (!seen) {
                JPC_Vec3 lin = JPC_BodyInterface_GetLinearVelocity(world->bi, s->jolt_id);
                JPC_Vec3 ang = JPC_BodyInterface_GetAngularVelocity(world->bi, s->jolt_id);
                avg_lin.x += lin.x; avg_lin.y += lin.y; avg_lin.z += lin.z;
                avg_ang.x += ang.x; avg_ang.y += ang.y; avg_ang.z += ang.z;
                vel_n++;
            }
        }
    }
    if (nsub < 2) {
        for (int i = 0; i < nsub; i++) JPC_Shape_Release(child_shapes[i]);
        free(subs); free(child_shapes); free(part_pos); free(part_rot);
        return false;
    }
    if (vel_n > 0) {
        float inv = 1.0f / (float)vel_n;
        avg_lin.x *= inv; avg_lin.y *= inv; avg_lin.z *= inv;
        avg_ang.x *= inv; avg_ang.y *= inv; avg_ang.z *= inv;
    }

    JPC_StaticCompoundShapeSettings cs;
    JPC_StaticCompoundShapeSettings_default(&cs);
    cs.SubShapes = subs;
    cs.SubShapesLen = (size_t)nsub;
    JPC_Shape* compound = NULL;
    JPC_String* err = NULL;
    if (!JPC_StaticCompoundShapeSettings_Create(&cs, &compound, &err) || !compound) {
        if (err) JPC_String_delete(err);
        for (int i = 0; i < nsub; i++) JPC_Shape_Release(child_shapes[i]);
        free(subs); free(child_shapes); free(part_pos); free(part_rot);
        return false;
    }

    JPC_BodyCreationSettings body_settings;
    JPC_BodyCreationSettings_default(&body_settings);
    body_settings.Position = (JPC_RVec3){ ox, oy, oz, 0 };
    body_settings.Rotation = (JPC_Quat){ 0, 0, 0, 1 };
    body_settings.Shape = compound;
    body_settings.UserData = (uint64_t)members[0];
    body_settings.ObjectLayer = LAYER_DYNAMIC;
    body_settings.MotionType = JPC_MOTION_TYPE_DYNAMIC;
    body_settings.AllowDynamicOrKinematic = true;
    body_settings.AllowSleeping = true;
    body_settings.Friction = 0.8f;
    body_settings.Restitution = 0.0f;
    body_settings.LinearDamping = 0.05f;
    body_settings.AngularDamping = 0.05f;
    body_settings.LinearVelocity = avg_lin;
    body_settings.AngularVelocity = avg_ang;

    for (int i = 0; i < count; i++) {
        PhysicsSlot* s = &world->slots[members[i]];
        JPC_BodyID old = s->jolt_id;
        if (!old) continue;
        bool shared_later = false;
        for (int j = i + 1; j < count; j++) {
            if (world->slots[members[j]].jolt_id == old) { shared_later = true; break; }
        }
        s->jolt_id = 0;
        if (!shared_later) island_destroy_jolt_id(world, old);
    }

    JPC_BodyID bid = JPC_BodyInterface_CreateAndAddBody(world->bi, &body_settings, JPC_ACTIVATION_ACTIVATE);
    JPC_Shape_Release(compound);
    for (int i = 0; i < nsub; i++) JPC_Shape_Release(child_shapes[i]);
    free(subs); free(child_shapes);
    if (bid == 0 || bid == 0xffffffffu) {
        for (int i = 0; i < count; i++) {
            PhysicsSlot* s = &world->slots[members[i]];
            island_clear_slot(s);
            slot_spawn_jolt(world, members[i], part_pos[i], part_rot[i], avg_lin, avg_ang);
        }
        free(part_pos); free(part_rot);
        return false;
    }

    for (int i = 0; i < count; i++) {
        PhysicsSlot* s = &world->slots[members[i]];
        s->jolt_id = bid;
        s->island_member = true;
        s->island_body_owner = (i == 0);
        s->island_local_pos = (Vec3){
            (float)part_pos[i].x - ox,
            (float)part_pos[i].y - oy,
            (float)part_pos[i].z - oz
        };
        JPC_Quat lq = part_rot[i];
        pw_qnorm(&lq);
        s->island_lq[0] = lq.x;
        s->island_lq[1] = lq.y;
        s->island_lq[2] = lq.z;
        s->island_lq[3] = lq.w;
    }
    free(part_pos); free(part_rot);
    return true;
}

static void physics_detach_movable(PhysicsWorld* world) {
    if (!world) return;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active) continue;
        if (constraint_is_weld(world->connectors[i].type)) continue;
        if (world->connectors[i].type == CONSTRAINT_NOCOLLIDE) continue;
        if (!world->connectors[i].constraint) continue;
        JPC_PhysicsSystem_RemoveConstraint(world->system, world->connectors[i].constraint);
        JPC_Constraint_Release(world->connectors[i].constraint);
        world->connectors[i].constraint = NULL;
    }
}

static JPC_Constraint* physics_jolt_movable(PhysicsWorld* world,
                                            JPC_BodyID id_a, JPC_BodyID id_b, uint8_t type,
                                            Vec3 pa, Vec3 pb, Vec3 point, Vec3 axis,
                                            float lim_min, float lim_max,
                                            float stiff, float damp, float motor, float torque) {
    if (!world || id_a == 0 || id_b == 0 || id_a == id_b) return NULL;
    if (id_a == 0xffffffffu || id_b == 0xffffffffu) return NULL;

    const JPC_BodyLockInterface* bli = JPC_PhysicsSystem_GetBodyLockInterface(world->system);
    JPC_BodyID ids[2] = { id_a, id_b };
    JPC_BodyLockMultiWrite* locks = JPC_BodyLockMultiWrite_new(bli, ids, 2);
    if (!locks) return NULL;
    JPC_Body* b1 = JPC_BodyLockMultiWrite_GetBody(locks, 0);
    JPC_Body* b2 = JPC_BodyLockMultiWrite_GetBody(locks, 1);
    if (!b1 || !b2) {
        JPC_BodyLockMultiWrite_delete(locks);
        return NULL;
    }

    JPC_Vec3 ax = pw_jpc_norm(pw_jpc_v3(axis.x, axis.y, axis.z));
    if (fabsf(axis.x) + fabsf(axis.y) + fabsf(axis.z) < 1e-6f) {
        if (type == CONSTRAINT_SLIDER)
            ax = pw_jpc_norm(pw_jpc_v3(pb.x - pa.x, pb.y - pa.y, pb.z - pa.z));
        else
            ax = pw_jpc_v3(0, 1, 0);
    }
    JPC_Vec3 nrm = pw_jpc_perp(ax);
    JPC_RVec3 jp = pw_jpc_rv3(point.x, point.y, point.z);
    float dist = sqrtf((pa.x - pb.x) * (pa.x - pb.x) + (pa.y - pb.y) * (pa.y - pb.y) +
                       (pa.z - pb.z) * (pa.z - pb.z));
    if (dist < 0.05f) dist = 0.05f;

    JPC_Constraint* constraint = NULL;
    if (type == CONSTRAINT_HINGE) {
        JPC_HingeConstraintSettings s;
        JPC_HingeConstraintSettings_default(&s);
        s.Space = JPC_CONSTRAINT_SPACE_WORLD_SPACE;
        s.Point1 = jp; s.Point2 = jp;
        s.HingeAxis1 = ax; s.HingeAxis2 = ax;
        s.NormalAxis1 = nrm; s.NormalAxis2 = nrm;
        if (lim_min < lim_max) {
            s.LimitsMin = lim_min * DEG_TO_RAD;
            s.LimitsMax = lim_max * DEG_TO_RAD;
        }
        if (fabsf(motor) > 1e-4f) {
            float t = (torque > 1e-3f) ? torque : 100000.0f;
            s.MotorSettings.MinForceLimit = -t;
            s.MotorSettings.MaxForceLimit = t;
            s.MotorSettings.MinTorqueLimit = -t;
            s.MotorSettings.MaxTorqueLimit = t;
        }
        constraint = (JPC_Constraint*)JPC_HingeConstraintSettings_Create(&s, b1, b2);
        if (constraint && fabsf(motor) > 1e-4f) {
            JPC_HingeConstraint_SetMotorState((JPC_HingeConstraint*)constraint, JPC_MOTOR_STATE_VELOCITY);
            JPC_HingeConstraint_SetTargetAngularVelocity((JPC_HingeConstraint*)constraint, motor * DEG_TO_RAD);
        }
    } else if (type == CONSTRAINT_BALL) {
        JPC_SixDOFConstraintSettings s;
        JPC_SixDOFConstraintSettings_default(&s);
        s.Space = JPC_CONSTRAINT_SPACE_WORLD_SPACE;
        s.Position1 = jp; s.Position2 = jp;
        for (int i = 0; i < 3; i++) { s.LimitMin[i] = FLT_MAX; s.LimitMax[i] = -FLT_MAX; }
        for (int i = 3; i < 6; i++) { s.LimitMin[i] = -FLT_MAX; s.LimitMax[i] = FLT_MAX; }
        constraint = JPC_SixDOFConstraintSettings_Create(&s, b1, b2);
    } else if (type == CONSTRAINT_SLIDER) {
        JPC_SliderConstraintSettings s;
        JPC_SliderConstraintSettings_default(&s);
        s.Space = JPC_CONSTRAINT_SPACE_WORLD_SPACE;
        s.AutoDetectPoint = false;
        s.Point1 = jp; s.Point2 = jp;
        s.SliderAxis1 = ax; s.SliderAxis2 = ax;
        s.NormalAxis1 = nrm; s.NormalAxis2 = nrm;
        if (lim_min < lim_max) { s.LimitsMin = lim_min; s.LimitsMax = lim_max; }
        if (fabsf(motor) > 1e-4f) {
            float t = (torque > 1e-3f) ? torque : 100000.0f;
            s.MotorSettings.MinForceLimit = -t;
            s.MotorSettings.MaxForceLimit = t;
            s.MotorSettings.MinTorqueLimit = -t;
            s.MotorSettings.MaxTorqueLimit = t;
        }
        constraint = (JPC_Constraint*)JPC_SliderConstraintSettings_Create(&s, b1, b2);
        if (constraint && fabsf(motor) > 1e-4f) {
            JPC_SliderConstraint_SetMotorState((JPC_SliderConstraint*)constraint, JPC_MOTOR_STATE_VELOCITY);
            JPC_SliderConstraint_SetTargetVelocity((JPC_SliderConstraint*)constraint, motor);
        }
    } else {
        JPC_DistanceConstraintSettings s;
        JPC_DistanceConstraintSettings_default(&s);
        s.Space = JPC_CONSTRAINT_SPACE_WORLD_SPACE;
        s.Point1 = pw_jpc_rv3(pa.x, pa.y, pa.z);
        s.Point2 = pw_jpc_rv3(pb.x, pb.y, pb.z);
        if (type == CONSTRAINT_ROPE) { s.MinDistance = 0.0f; s.MaxDistance = dist; }
        else { s.MinDistance = dist; s.MaxDistance = dist; }
        if (type == CONSTRAINT_SPRING) {
            s.LimitsSpringSettings.Mode = JPC_SPRING_MODE_FREQUENCY_AND_DAMPING;
            s.LimitsSpringSettings.FrequencyOrStiffness = (stiff > 0.01f) ? stiff : 2.0f;
            s.LimitsSpringSettings.Damping = (damp > 0.0f) ? damp : 0.2f;
        }
        constraint = (JPC_Constraint*)JPC_DistanceConstraintSettings_Create(&s, b1, b2);
    }
    JPC_BodyLockMultiWrite_delete(locks);
    if (!constraint) return NULL;
    JPC_Constraint_AddRef(constraint);
    JPC_PhysicsSystem_AddConstraint(world->system, constraint);
    return constraint;
}

static void physics_attach_movable(PhysicsWorld* world) {
    if (!world) return;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active) continue;
        if (constraint_is_weld(world->connectors[i].type)) continue;
        if (world->connectors[i].type == CONSTRAINT_NOCOLLIDE) continue;
        if (world->connectors[i].constraint) continue;
        PhysicsSlot* sa = slot_get(world, world->connectors[i].body_a);
        PhysicsSlot* sb = slot_get(world, world->connectors[i].body_b);
        if (!sa || !sb || !sa->jolt_id || !sb->jolt_id) continue;
        if (sa->jolt_id == sb->jolt_id) continue;
        JPC_RVec3 pa_p, pb_p;
        JPC_Quat pa_q, pb_q;
        slot_get_part_pose(world, sa, &pa_p, &pa_q);
        slot_get_part_pose(world, sb, &pb_p, &pb_q);
        Vec3 pa = { (float)pa_p.x, (float)pa_p.y, (float)pa_p.z };
        Vec3 pb = { (float)pb_p.x, (float)pb_p.y, (float)pb_p.z };
        Vec3 point = world->connectors[i].desc.point;
        Vec3 axis = world->connectors[i].desc.axis;
        if (world->connectors[i].locals_set) {
            JPC_Vec3 la = {
                world->connectors[i].local_a.x,
                world->connectors[i].local_a.y,
                world->connectors[i].local_a.z, 0
            };
            JPC_Vec3 wa = pw_qrot(pa_q, la);
            point = (Vec3){ pa.x + wa.x, pa.y + wa.y, pa.z + wa.z };
            JPC_Vec3 lax = {
                world->connectors[i].local_axis.x,
                world->connectors[i].local_axis.y,
                world->connectors[i].local_axis.z, 0
            };
            JPC_Vec3 wax = pw_qrot(pa_q, lax);
            axis = (Vec3){ wax.x, wax.y, wax.z };
        }
        const ConstraintDesc* d = &world->connectors[i].desc;
        world->connectors[i].constraint = physics_jolt_movable(
            world, sa->jolt_id, sb->jolt_id, world->connectors[i].type,
            pa, pb, point, axis,
            d->limits_min, d->limits_max, d->stiffness, d->damping, d->motor, d->torque);
    }
}

static void physics_rebuild_weld_compounds(PhysicsWorld* world) {
    if (!world) return;
    physics_detach_movable(world);
    physics_rebuild_weld_islands(world);

    uint32_t n = world->count;
    if (n < 2) {
        physics_attach_movable(world);
        return;
    }
    uint32_t* parent = (uint32_t*)malloc(n * sizeof(uint32_t));
    uint32_t* counts = (uint32_t*)calloc(n, sizeof(uint32_t));
    bool* root_static = (bool*)calloc(n, sizeof(bool));
    bool* visited = (bool*)calloc(n, sizeof(bool));
    PhysicsBodyID* members = (PhysicsBodyID*)malloc(n * sizeof(PhysicsBodyID));
    if (!parent || !counts || !root_static || !visited || !members) {
        free(parent); free(counts); free(root_static); free(visited); free(members);
        physics_attach_movable(world);
        return;
    }
    for (uint32_t i = 0; i < n; i++) parent[i] = i;

    for (uint32_t ci = 1; ci < MAX_CONNECTORS; ci++) {
        if (!world->connectors[ci].active || !world->connectors[ci].welded) continue;
        uint32_t a = world->connectors[ci].body_a;
        uint32_t b = world->connectors[ci].body_b;
        if (a == 0 || b == 0 || a >= n || b >= n) continue;
        pw_uf_union(parent, a, b);
    }
    for (uint32_t i = 1; i < n; i++) {
        if (!world->slots[i].active) continue;
        if (world->slots[i].type == BODY_STATIC)
            root_static[pw_uf_find(parent, i)] = true;
        counts[pw_uf_find(parent, i)]++;
    }

    for (uint32_t i = 1; i < n; i++) {
        if (visited[i] || !world->slots[i].active) continue;
        uint32_t root = pw_uf_find(parent, i);
        if (root_static[root] || counts[root] < 2) continue;

        int mcount = 0;
        for (uint32_t j = 1; j < n; j++) {
            if (!world->slots[j].active) continue;
            if (pw_uf_find(parent, j) != root) continue;
            visited[j] = true;
            if (island_slot_eligible(world, j))
                members[mcount++] = j;
        }
        if (mcount < 2) continue;
        if (mcount >= (int)PW_NETOWN_MAX_ASM_PARTS) continue;
        if (island_already_compounded(world, members, mcount)) continue;
        for (int k = 0; k < mcount; k++) {
            PhysicsSlot* s = &world->slots[members[k]];
            if (s->island_member && s->jolt_id)
                island_dissolve_jolt(world, s->jolt_id);
        }
        island_build_compound(world, members, mcount);
    }

    for (uint32_t i = 1; i < n; i++) {
        PhysicsSlot* s = &world->slots[i];
        if (!s->island_member || !s->jolt_id) continue;
        uint32_t root = pw_uf_find(parent, i);
        int eligible = 0;
        if (!root_static[root]) {
            for (uint32_t j = 1; j < n; j++) {
                if (pw_uf_find(parent, j) != root) continue;
                if (island_slot_eligible(world, j)) eligible++;
            }
        }
        if (root_static[root] || eligible < 2)
            island_dissolve_jolt(world, s->jolt_id);
    }

    free(parent); free(counts); free(root_static); free(visited); free(members);
    physics_attach_movable(world);
}

static void physics_flush_weld_compounds(PhysicsWorld* world) {
    if (!world || !world->weld_compound_dirty) return;
    if (world->weld_batch > 0) return;
    world->weld_compound_dirty = false;
    physics_rebuild_weld_compounds(world);
}

static void physics_mark_weld_dirty(PhysicsWorld* world) {
    if (!world) return;
    world->weld_compound_dirty = true;
    if (world->weld_batch <= 0)
        physics_flush_weld_compounds(world);
}

void physics_weld_batch_begin(PhysicsWorld* world) {
    world = resolve_world(world);
    if (!world) return;
    world->weld_batch++;
}

void physics_weld_batch_end(PhysicsWorld* world) {
    world = resolve_world(world);
    if (!world) return;
    world->weld_batch--;
    if (world->weld_batch <= 0) {
        world->weld_batch = 0;
        physics_flush_weld_compounds(world);
    }
}

static ConnectorID connector_alloc(PhysicsWorld* world) {
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active) return i;
    }
    return 0;
}

ConnectorID physics_create_connector(PhysicsWorld* world, PhysicsBodyID body_a, PhysicsBodyID body_b) {
    ConstraintDesc d;
    memset(&d, 0, sizeof(d));
    d.type = CONSTRAINT_WELD;
    return physics_create_constraint(world, body_a, body_b, &d);
}

ConnectorID physics_create_constraint(PhysicsWorld* world, PhysicsBodyID body_a, PhysicsBodyID body_b,
                                     const ConstraintDesc* desc) {
    world = resolve_world(world);
    if (!world || body_a == 0 || body_b == 0) return 0;
    if (world->connector_count >= MAX_CONNECTORS) return 0;
    uint8_t type = desc ? desc->type : CONSTRAINT_WELD;
    if (constraint_is_weld(type) && bodies_share_fixed_constraint(world, body_a, body_b)) {
        for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
            if (!world->connectors[i].active) continue;
            PhysicsBodyID ca = world->connectors[i].body_a;
            PhysicsBodyID cb = world->connectors[i].body_b;
            if ((ca == body_a && cb == body_b) || (ca == body_b && cb == body_a))
                return i;
        }
    }

    PhysicsSlot* sa = slot_get(world, body_a);
    PhysicsSlot* sb = slot_get(world, body_b);
    if (!sa || !sb) return 0;
    if (sa->type == BODY_STATIC && sb->type == BODY_STATIC) return 0;

    ConnectorID cid = connector_alloc(world);
    if (!cid) return 0;

    if (type == CONSTRAINT_NOCOLLIDE) {
        world->connectors[cid].body_a = body_a;
        world->connectors[cid].body_b = body_b;
        world->connectors[cid].active = true;
        world->connectors[cid].welded = false;
        world->connectors[cid].type = CONSTRAINT_NOCOLLIDE;
        world->connectors[cid].constraint = NULL;
        world->connectors[cid].locals_set = false;
        if (desc) world->connectors[cid].desc = *desc;
        else memset(&world->connectors[cid].desc, 0, sizeof(ConstraintDesc));
        world->connector_count++;
        return cid;
    }

    if (constraint_is_weld(type)) {
        world->connectors[cid].body_a = body_a;
        world->connectors[cid].body_b = body_b;
        world->connectors[cid].active = true;
        world->connectors[cid].welded = true;
        world->connectors[cid].type = type;
        world->connectors[cid].constraint = NULL;
        world->connectors[cid].locals_set = false;
        if (desc) world->connectors[cid].desc = *desc;
        else {
            memset(&world->connectors[cid].desc, 0, sizeof(ConstraintDesc));
            world->connectors[cid].desc.type = CONSTRAINT_WELD;
        }
        world->connector_count++;
        if (sa->jolt_id) JPC_BodyInterface_SetRestitution(world->bi, sa->jolt_id, 0.0f);
        if (sb->jolt_id) JPC_BodyInterface_SetRestitution(world->bi, sb->jolt_id, 0.0f);
        physics_mark_weld_dirty(world);
        return cid;
    }

    Vec3 pa = physics_get_position(world, body_a);
    Vec3 pb = physics_get_position(world, body_b);
    Vec3 point = desc ? desc->point : (Vec3){0, 0, 0};
    int point_set = desc && desc->point_set;
    if (!point_set && fabsf(point.x) + fabsf(point.y) + fabsf(point.z) < 1e-8f)
        point = (Vec3){ (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f, (pa.z + pb.z) * 0.5f };
    Vec3 axis = desc ? desc->axis : (Vec3){0, 1, 0};
    float lim_min = desc ? desc->limits_min : 0.0f;
    float lim_max = desc ? desc->limits_max : 0.0f;
    float stiff = desc ? desc->stiffness : 0.0f;
    float damp = desc ? desc->damping : 0.0f;
    float motor = desc ? desc->motor : 0.0f;
    float torque = desc ? desc->torque : 0.0f;

    JPC_RVec3 pa_p, pb_p;
    JPC_Quat pa_q, pb_q;
    slot_get_part_pose(world, sa, &pa_p, &pa_q);
    slot_get_part_pose(world, sb, &pb_p, &pb_q);
    JPC_Quat qai = pw_qconj(pa_q);
    JPC_Quat qbi = pw_qconj(pb_q);
    JPC_Vec3 rel_a = { point.x - pa.x, point.y - pa.y, point.z - pa.z, 0 };
    JPC_Vec3 rel_b = { point.x - pb.x, point.y - pb.y, point.z - pb.z, 0 };
    JPC_Vec3 la = pw_qrot(qai, rel_a);
    JPC_Vec3 lb = pw_qrot(qbi, rel_b);
    JPC_Vec3 axw = pw_jpc_norm(pw_jpc_v3(axis.x, axis.y, axis.z));
    JPC_Vec3 lax = pw_qrot(qai, axw);

    JPC_Constraint* constraint = physics_jolt_movable(
        world, sa->jolt_id, sb->jolt_id, type, pa, pb, point, axis,
        lim_min, lim_max, stiff, damp, motor, torque);

    world->connectors[cid].body_a = body_a;
    world->connectors[cid].body_b = body_b;
    world->connectors[cid].active = true;
    world->connectors[cid].welded = false;
    world->connectors[cid].type = type;
    world->connectors[cid].constraint = constraint;
    world->connectors[cid].local_a = (Vec3){ la.x, la.y, la.z };
    world->connectors[cid].local_b = (Vec3){ lb.x, lb.y, lb.z };
    world->connectors[cid].local_axis = (Vec3){ lax.x, lax.y, lax.z };
    world->connectors[cid].locals_set = true;
    if (desc) world->connectors[cid].desc = *desc;
    else memset(&world->connectors[cid].desc, 0, sizeof(ConstraintDesc));
    world->connectors[cid].desc.type = type;
    world->connector_count++;
    return cid;
}

void physics_destroy_connector(PhysicsWorld* world, ConnectorID id) {
    world = resolve_world(world);
    if (!world || id == 0 || id >= MAX_CONNECTORS) return;
    if (!world->connectors[id].active) return;

    bool was_weld = world->connectors[id].welded || constraint_is_weld(world->connectors[id].type);
    if (world->connectors[id].constraint) {
        JPC_PhysicsSystem_RemoveConstraint(world->system, world->connectors[id].constraint);
        JPC_Constraint_Release(world->connectors[id].constraint);
        world->connectors[id].constraint = NULL;
    }
    world->connectors[id].active = false;
    world->connectors[id].welded = false;
    world->connectors[id].locals_set = false;
    if (world->connector_count > 0) world->connector_count--;
    if (was_weld) physics_mark_weld_dirty(world);
    else physics_rebuild_weld_islands(world);
}

bool physics_connector_is_active(const PhysicsWorld* world, ConnectorID id) {
    world = resolve_world_c(world);
    if (!world || id == 0 || id >= MAX_CONNECTORS) return false;
    return world->connectors[id].active;
}

int physics_break_connectors_for_body(PhysicsWorld* world, PhysicsBodyID body) {
    world = resolve_world(world);
    if (!world || body == 0) return 0;
    physics_weld_batch_begin(world);
    int broken = 0;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active) continue;
        if (world->connectors[i].body_a != body && world->connectors[i].body_b != body)
            continue;
        physics_destroy_connector(world, i);
        broken++;
    }
    physics_weld_batch_end(world);
    return broken;
}

void physics_clear_connectors(PhysicsWorld* world) {
    world = resolve_world(world);
    if (!world) return;
    physics_weld_batch_begin(world);
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (world->connectors[i].active)
            physics_destroy_connector(world, i);
    }
    physics_weld_batch_end(world);
    world->connector_count = 0;
    memset(world->weld_island, 0, sizeof(world->weld_island));
}

static uint32_t pw_uf_find(uint32_t* parent, uint32_t x) {
    while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
    return x;
}
static void pw_uf_union(uint32_t* parent, uint32_t a, uint32_t b) {
    a = pw_uf_find(parent, a); b = pw_uf_find(parent, b);
    if (a != b) parent[b] = a;
}

static void physics_rebuild_weld_islands(PhysicsWorld* world) {
    if (!world) return;
    memset(world->weld_island, 0, sizeof(world->weld_island));
    uint32_t parent[MAX_PHYSICS_BODIES];
    for (uint32_t i = 0; i < MAX_PHYSICS_BODIES; i++) parent[i] = i;
    bool any = false;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active || !world->connectors[i].welded) continue;
        PhysicsBodyID a = world->connectors[i].body_a;
        PhysicsBodyID b = world->connectors[i].body_b;
        if (a == 0 || b == 0 || a >= MAX_PHYSICS_BODIES || b >= MAX_PHYSICS_BODIES) continue;
        pw_uf_union(parent, a, b);
        any = true;
    }
    if (!any) return;
    uint32_t counts[MAX_PHYSICS_BODIES];
    memset(counts, 0, sizeof(counts));
    for (uint32_t i = 1; i < MAX_PHYSICS_BODIES; i++) {
        if (!world->slots[i].active) continue;
        counts[pw_uf_find(parent, i)]++;
    }
    world->next_weld_island = 1;
    uint32_t island_of[MAX_PHYSICS_BODIES];
    memset(island_of, 0, sizeof(island_of));
    for (uint32_t i = 1; i < MAX_PHYSICS_BODIES; i++) {
        if (!world->slots[i].active) continue;
        uint32_t root = pw_uf_find(parent, i);
        if (counts[root] < 2) continue;
        if (island_of[root] == 0) island_of[root] = world->next_weld_island++;
        world->weld_island[i] = island_of[root];
        if (world->slots[i].jolt_id)
            JPC_BodyInterface_SetRestitution(world->bi, world->slots[i].jolt_id, 0.0f);
    }
}

Vec3 physics_get_connector_position(const PhysicsWorld* world, ConnectorID id) {
    world = resolve_world_c(world);
    if (!world || id == 0 || id >= MAX_CONNECTORS || !world->connectors[id].active)
        return (Vec3){ 0, 0, 0 };
    Vec3 pa = physics_get_position(world, world->connectors[id].body_a);
    Vec3 pb = physics_get_position(world, world->connectors[id].body_b);
    return (Vec3){ (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f, (pa.z + pb.z) * 0.5f };
}

int physics_break_connectors_in_radius(PhysicsWorld* world, Vec3 center, float radius) {
    world = resolve_world(world);
    if (!world) return 0;
    physics_weld_batch_begin(world);
    int broken = 0;
    float r2 = radius * radius;
    for (uint32_t i = 1; i < MAX_CONNECTORS; i++) {
        if (!world->connectors[i].active) continue;
        Vec3 pos = physics_get_connector_position(world, i);
        float dx = pos.x - center.x;
        float dy = pos.y - center.y;
        float dz = pos.z - center.z;
        if (dx * dx + dy * dy + dz * dz < r2) {
            physics_destroy_connector(world, i);
            broken++;
        }
    }
    physics_weld_batch_end(world);
    return broken;
}

void physics_optimize_broadphase(PhysicsWorld* world) {
    world = resolve_world(world);
    if (!world || !world->system) return;
    physics_flush_weld_compounds(world);
    JPC_PhysicsSystem_OptimizeBroadPhase(world->system);
}
