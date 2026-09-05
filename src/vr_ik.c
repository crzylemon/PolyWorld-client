/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_ik.c                                                                             |
|   Purpose: R6 full-body IK for VR. It's... a lot.                                           |
\*-------------------------------------------------------------------------------------------*/

#include "vr_ik.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD ((float)M_PI / 180.0f)
#define RAD2DEG (180.0f / (float)M_PI)

static const Vec3 k_pivot[AVATAR_PART_COUNT] = {
    [ANIM_PART_HEAD]      = {0.0f, 8.00f, 0.0f},
    [ANIM_PART_TORSO]     = {0.0f, 6.0f, 0.0f},
    [ANIM_PART_RIGHT_ARM] = {0.0f, 7.5f, 3.0f},
    [ANIM_PART_LEFT_ARM]  = {0.0f, 7.5f, -3.0f},
    [ANIM_PART_RIGHT_LEG] = {0.0f, 4.0f, 1.0f},
    [ANIM_PART_LEFT_LEG]  = {0.0f, 4.0f, -1.0f},
};

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static Vec3 v3(float x, float y, float z) {
    return (Vec3){x, y, z};
}

static Vec3 quat_rotate(const PwVrTracker* t, Vec3 v) {

    float qx = t->qx, qy = t->qy, qz = t->qz, qw = t->qw;
    Vec3 u = {qx, qy, qz};
    Vec3 uv = vec3_cross(u, v);
    Vec3 uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * qw);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

static Vec3 world_to_model(Vec3 world, Vec3 feet, float yaw_deg) {
    Vec3 d = vec3_sub(world, feet);
    float rad = -yaw_deg * DEG2RAD;
    float c = cosf(rad), s = sinf(rad);
    Vec3 r = {d.x * c + d.z * s, d.y, -d.x * s + d.z * c};
    return vec3_scale(r, 1.0f / AVATAR_SCALE);
}

static Vec3 aim_down_to(Vec3 dir) {
    float len = vec3_length(dir);
    if (len < 1e-5f) return v3(0, 0, 0);
    dir = vec3_scale(dir, 1.0f / len);
    float horiz = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (horiz < 1e-5f) horiz = 1e-5f;
    float x = atan2f(-dir.z, horiz) * RAD2DEG;
    float z = atan2f(dir.x, -dir.y) * RAD2DEG;
    return v3(x, 0.0f, z);
}

void vr_ik_calib_reset(VrIkCalib* c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
}

void vr_ik_calib_defaults(VrIkCalib* c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->height_m = VR_IK_DEFAULT_HEIGHT_M;
    c->arm_span_m = VR_IK_DEFAULT_HEIGHT_M;
    c->height_studs = pw_metres_to_studs(c->height_m);
    c->arm_span_studs = pw_metres_to_studs(c->arm_span_m);
    c->ik_scale = c->height_studs / VR_IK_CANONICAL_HEIGHT_STUDS;

    float arm = 4.0f * AVATAR_SCALE * c->ik_scale;
    c->arm_len_l_studs = arm;
    c->arm_len_r_studs = arm;
    c->ready = true;
}

void vr_ik_calib_from_pose(VrIkCalib* c, const PwVrPose* pose) {
    if (!c) return;
    vr_ik_calib_defaults(c);
    if (!pose || !(pose->flags & PW_VR_FLAG_CALIB)) return;
    if (pose->height_m > 0.4f && pose->height_m < 3.0f) {
        c->height_m = pose->height_m;
        c->height_studs = pw_metres_to_studs(pose->height_m);
    }
    if (pose->arm_span_m > 0.3f && pose->arm_span_m < 4.0f) {
        c->arm_span_m = pose->arm_span_m;
        c->arm_span_studs = pw_metres_to_studs(pose->arm_span_m);
    }
    c->ik_scale = c->height_studs / VR_IK_CANONICAL_HEIGHT_STUDS;
    if (c->ik_scale < 0.5f) c->ik_scale = 0.5f;
    if (c->ik_scale > 2.0f) c->ik_scale = 2.0f;
    float arm = 4.0f * AVATAR_SCALE * c->ik_scale;
    c->arm_len_l_studs = arm;
    c->arm_len_r_studs = arm;
    c->ready = true;
}

void vr_ik_calibrate(VrIkCalib* c, const PwVrPose* standing, Vec3 feet_world,
                     float mesh_yaw_deg, bool sample_arms) {
    (void)mesh_yaw_deg;
    if (!c) return;
    vr_ik_calib_defaults(c);
    if (!standing || !(standing->flags & PW_VR_FLAG_HEAD)) return;

    float h = standing->head.y - feet_world.y;
    if (h < 1.0f) h = 1.0f;
    if (h > 12.0f) h = 12.0f;
    c->height_studs = h;
    c->height_m = pw_studs_to_metres(h);

    c->ik_scale = c->height_studs / VR_IK_CANONICAL_HEIGHT_STUDS;
    if (c->ik_scale < 0.5f) c->ik_scale = 0.5f;
    if (c->ik_scale > 2.0f) c->ik_scale = 2.0f;
    float arm = 4.0f * AVATAR_SCALE * c->ik_scale;
    c->arm_len_l_studs = arm;
    c->arm_len_r_studs = arm;
    c->arm_span_studs = c->height_studs;
    c->arm_span_m = c->height_m;

    if (sample_arms &&
        (standing->flags & PW_VR_FLAG_LHAND) && (standing->flags & PW_VR_FLAG_RHAND)) {
        Vec3 l = {standing->lhand.x, standing->lhand.y, standing->lhand.z};
        Vec3 r = {standing->rhand.x, standing->rhand.y, standing->rhand.z};
        float span = vec3_length(vec3_sub(l, r));
        if (span > 0.5f) {
            c->arm_span_studs = span;
            c->arm_span_m = pw_studs_to_metres(span);
        }
        float sh_y = feet_world.y + k_pivot[ANIM_PART_LEFT_ARM].y * AVATAR_SCALE;

        float dl = sh_y - l.y;
        float dr = sh_y - r.y;
        if (dl > arm * 0.55f) c->arm_len_l_studs = dl;
        if (dr > arm * 0.55f) c->arm_len_r_studs = dr;
    }
    c->ready = true;
}

static float ang_lerp(float a, float b, float t) {
    float d = b - a;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return a + d * t;
}

static float smoothstep01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void rigid_arm_aim(AvatarAnim* anim, int part, Vec3 hand_model) {
    Vec3 root = k_pivot[part];
    Vec3 to = vec3_sub(hand_model, root);
    float len = vec3_length(to);
    Vec3 aim = (len > 1e-4f) ? vec3_scale(to, 1.0f / len) : v3(0, -1, 0);
    Vec3 rot = aim_down_to(aim);

    float align = -aim.y;
    Vec3 prev = anim->rot[part];
    int was_hang = (fabsf(prev.x) + fabsf(prev.z)) < 10.0f;
    float hang;
    if (was_hang)
        hang = smoothstep01((align - 0.58f) / 0.30f);
    else
        hang = smoothstep01((align - 0.78f) / 0.18f);

    rot.x = ang_lerp(rot.x, 0.0f, hang);
    rot.y = ang_lerp(rot.y, 0.0f, hang);
    rot.z = ang_lerp(rot.z, 0.0f, hang);
    anim->rot[part] = rot;
    anim->pos[part] = v3(0, 0, 0);
}

static Vec3 pose_wave(Vec3 a, Vec3 b, float phase) {
    float t = 0.5f + 0.5f * sinf(phase);
    return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static Vec3 pose_mirror_lr(Vec3 r) {
    return v3(-r.x, -r.y, r.z);
}

void vr_ik_apply(AvatarAnim* anim, const PwVrPose* pose, const VrIkCalib* calib,
                 Vec3 feet_world, float mesh_yaw_deg) {
    if (!anim || !pose) return;
    VrIkCalib local;
    if (!calib || !calib->ready) {
        vr_ik_calib_defaults(&local);
        calib = &local;
    }

    if (pose->flags & PW_VR_FLAG_HEAD) {
        Vec3 look = quat_rotate(&pose->head, v3(0, 0, -1));
        float rad = -mesh_yaw_deg * DEG2RAD;
        float c = cosf(rad), s = sinf(rad);
        Vec3 lm = {look.x * c + look.z * s, look.y, -look.x * s + look.z * c};

        float yaw = atan2f(-lm.z, lm.x) * RAD2DEG;
        float nod = asinf(clampf(lm.y, -1.0f, 1.0f)) * RAD2DEG;
        anim->rot[ANIM_PART_HEAD] = v3(0.0f, yaw, nod);
        anim->pos[ANIM_PART_HEAD] = v3(0, 0, 0);
    }

    if (pose->flags & PW_VR_FLAG_LHAND) {
        Vec3 h = world_to_model(
            v3(pose->lhand.x, pose->lhand.y, pose->lhand.z),
            feet_world, mesh_yaw_deg);
        rigid_arm_aim(anim, ANIM_PART_LEFT_ARM, h);
    }
    if (pose->flags & PW_VR_FLAG_RHAND) {
        Vec3 h = world_to_model(
            v3(pose->rhand.x, pose->rhand.y, pose->rhand.z),
            feet_world, mesh_yaw_deg);
        rigid_arm_aim(anim, ANIM_PART_RIGHT_ARM, h);
    }

    if (anim->state == ANIM_STATE_WALKING) {
        float phase = anim->walk_phase;
        float opp = phase + (float)M_PI;
        Vec3 r_a = v3(0.0f, -2.0f, 28.0f);
        Vec3 r_b = v3(0.0f, -2.0f, -28.0f);
        anim->rot[ANIM_PART_RIGHT_LEG] = pose_wave(r_a, r_b, phase);
        anim->rot[ANIM_PART_LEFT_LEG] = pose_wave(pose_mirror_lr(r_a), pose_mirror_lr(r_b), opp);
        anim->pos[ANIM_PART_RIGHT_LEG] = v3(0, 0, 0);
        anim->pos[ANIM_PART_LEFT_LEG] = v3(0, 0, 0);
    }

    if (pose->flags & PW_VR_FLAG_HEAD && calib->height_studs > 1.0f) {
        float h = pose->head.y - feet_world.y;
        float ratio = h / calib->height_studs;
        float t = 0.0f;
        if (ratio < 0.92f) {
            t = (0.92f - ratio) / 0.42f;
            if (t > 1.0f) t = 1.0f;
            if (t < 0.0f) t = 0.0f;
        }
        if (t > 0.01f) {
            float squat = 58.0f * t;
            anim->rot[ANIM_PART_RIGHT_LEG].z += squat;
            anim->rot[ANIM_PART_LEFT_LEG].z += squat;
            anim->rot[ANIM_PART_RIGHT_LEG].y += -6.0f * t;
            anim->rot[ANIM_PART_LEFT_LEG].y += 6.0f * t;
            anim->pos[ANIM_PART_TORSO] = v3(0.0f, -1.35f * t, 0.35f * t);
            anim->pos[ANIM_PART_HEAD].y -= 1.35f * t;
            anim->pos[ANIM_PART_HEAD].z += 0.20f * t;
        }
    }

    anim->tool_hold = false;
    anim->vr_ik = true;
}
