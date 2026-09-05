/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar.c                                                                            |
|   Purpose: kinematic controller (sweep, not a capsule sliding around)                       |
\*-------------------------------------------------------------------------------------------*/

#include "avatar.h"
#include "avatar_anim.h"
#include <math.h>
#include <stdio.h>

#define AVATAR_WALK_SPEED 16.0f
#define AVATAR_JUMP_IMPULSE 60.0f
#define ROOT_HALF_X 1.0f
#define ROOT_HALF_Y 2.625f
#define ROOT_HALF_Z 0.5f
#define AVATAR_SPAWN_Y 2.635f
#define AVATAR_SCALE 0.476f
#define AVATAR_TURN_SPEED 12.0f
#define AVATAR_YAW_OFFSET 270.0f
#define AVATAR_GRAVITY -190.0f

#define STEP_HEIGHT 1.5f
#define CLIMB_SPEED 12.0f

#define LADDER_DIST_GAP 0.5f
#define LADDER_PROBE_COUNT 10
#define LADDER_PROBE_SPACING 0.5f
#define LADDER_PROBE_MAX_DIST 1.2f
#define LADDER_MIN_TRANSITIONS 4
#define LADDER_MIN_DEPTH_SPREAD 0.75f
#define LADDER_MIN_RUNG_GAP 0.45f
#define LADDER_MIN_RUNG_COUNT 4
#define LADDER_MIN_RUNG_SPAN 5.0f

#define LADDER_STANDOFF (ROOT_HALF_Z + 0.35f)

#define CLIMB_PROBE_OFFSET (ROOT_HALF_Y * 0.35f)

#define CLIMB_DETACH_FEET_ABOVE 0.35f

#define JUMP_MIN_HEADROOM 1.25f
#define PI_F 3.14159265358979323846f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void ladder_probe_column(PhysicsWorld* physics, PhysicsBodyID self,
                                Vec3 origin_xz, float y0, Vec3 forward,
                                float max_dist, float* dists, int count,
                                float spacing) {
    for (int i = 0; i < count; i++) {
        Vec3 ro = { origin_xz.x, y0 + (float)i * spacing, origin_xz.z };
        RaycastHit hit = physics_raycast(physics, ro, forward, max_dist);
        float d = max_dist;
        if (hit.hit && hit.body != self)
            d = hit.distance;
        dists[i] = d;
    }
}

static bool ladder_has_rung_pattern(const float* dists, int count, float gap_thresh) {
    int transitions = 0;
    int close_hits = 0;
    int far_hits = 0;
    int sign_changes = 0;
    int prev_sign = 0;
    int deep_gaps = 0;

    float near_d = 1e9f;
    float far_d = 0.0f;
    for (int i = 0; i < count; i++) {
        if (dists[i] < near_d) near_d = dists[i];
        if (dists[i] > far_d) far_d = dists[i];
    }
    if (near_d >= LADDER_PROBE_MAX_DIST * 0.85f)
        return false;

    if (far_d - near_d < LADDER_MIN_DEPTH_SPREAD)
        return false;

    for (int i = 0; i < count; i++) {
        if (dists[i] <= near_d + gap_thresh * 0.45f)
            close_hits++;
        else if (dists[i] >= near_d + gap_thresh) {
            far_hits++;

            if (dists[i] >= LADDER_PROBE_MAX_DIST * 0.88f ||
                dists[i] >= near_d + 0.95f)
                deep_gaps++;
        }
        if (i > 0) {
            float delta = dists[i] - dists[i - 1];
            if (fabsf(delta) >= gap_thresh) {
                transitions++;
                int sign = (delta > 0.0f) ? 1 : -1;
                if (prev_sign != 0 && sign != prev_sign)
                    sign_changes++;
                prev_sign = sign;
            }
        }
    }

    return transitions >= LADDER_MIN_TRANSITIONS
        && close_hits >= 3
        && far_hits >= 2
        && sign_changes >= 2
        && deep_gaps >= 2;
}

static bool avatar_try_step_up(PhysicsWorld* physics, PhysicsBodyID self,
                               Vec3 pos, Vec3 probe_dir, float forward_radius,
                               float* out_center_y) {
    float plen = sqrtf(probe_dir.x * probe_dir.x + probe_dir.z * probe_dir.z);
    if (plen < 0.001f) return false;
    probe_dir.x /= plen;
    probe_dir.z /= plen;
    probe_dir.y = 0.0f;

    float feet_y = pos.y - ROOT_HALF_Y;

    Vec3 foot_ro = { pos.x, feet_y + 0.05f, pos.z };
    RaycastHit foot = physics_raycast(physics, foot_ro, probe_dir, forward_radius + 0.45f);
    if (!foot.hit || foot.body == self || foot.distance > forward_radius + 0.3f)
        return false;

    Vec3 clear_ro = { pos.x, feet_y + STEP_HEIGHT + 0.05f, pos.z };
    RaycastHit clear = physics_raycast(physics, clear_ro, probe_dir, forward_radius + 0.55f);
    if (clear.hit && clear.body != self && clear.distance <= forward_radius + 0.3f)
        return false;

    const float dists[] = {
        forward_radius + 0.08f,
        forward_radius + 0.28f,
        forward_radius + 0.48f,
        forward_radius + 0.72f
    };
    for (int i = 0; i < 4; i++) {
        Vec3 down_ro = {
            pos.x + probe_dir.x * dists[i],
            feet_y + STEP_HEIGHT + 0.35f,
            pos.z + probe_dir.z * dists[i]
        };
        RaycastHit ledge = physics_raycast(physics, down_ro, (Vec3){0.0f, -1.0f, 0.0f},
                                           STEP_HEIGHT + 0.55f);
        if (!ledge.hit || ledge.body == self || ledge.normal.y <= 0.4f)
            continue;
        float step_h = ledge.point.y - feet_y;
        if (step_h > 0.01f && step_h <= STEP_HEIGHT) {
            if (out_center_y) *out_center_y = ledge.point.y + ROOT_HALF_Y;
            return true;
        }
    }
    return false;
}

static int avatar_step_probe_dirs(Vec3 move_heading, const Vec3* wall_n,
                                  Vec3 out_dirs[6]) {
    int n = 0;
    out_dirs[n++] = move_heading;
    if (fabsf(move_heading.x) > 0.2f)
        out_dirs[n++] = (Vec3){ move_heading.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f };
    if (fabsf(move_heading.z) > 0.2f)
        out_dirs[n++] = (Vec3){ 0.0f, 0.0f, move_heading.z > 0.0f ? 1.0f : -1.0f };
    if (wall_n) {
        float nx = wall_n->x, nz = wall_n->z;
        float nlen = sqrtf(nx * nx + nz * nz);
        if (nlen > 0.05f)
            out_dirs[n++] = (Vec3){ -nx / nlen, 0.0f, -nz / nlen };
    }
    return n;
}

static bool avatar_try_step_up_any(PhysicsWorld* physics, PhysicsBodyID self,
                                   Vec3 pos, Vec3 move_heading, float forward_radius,
                                   float side_radius, const Vec3* wall_n,
                                   float* out_center_y) {
    Vec3 dirs[6];
    int nd = avatar_step_probe_dirs(move_heading, wall_n, dirs);
    Vec3 side_perp = { -move_heading.z, 0.0f, move_heading.x };
    float side_offs[] = { 0.0f, -side_radius * 0.85f, side_radius * 0.85f };

    for (int d = 0; d < nd; d++) {
        for (int s = 0; s < 3; s++) {
            Vec3 from = {
                pos.x + side_perp.x * side_offs[s],
                pos.y,
                pos.z + side_perp.z * side_offs[s]
            };
            if (avatar_try_step_up(physics, self, from, dirs[d], forward_radius, out_center_y))
                return true;
        }
    }
    return false;
}

static bool ladder_thin_rung_stack(PhysicsWorld* physics, PhysicsBodyID self,
                                   PhysicsBodyID hit_body, Vec3 hit_point);

static bool ladder_ahead(PhysicsWorld* physics, PhysicsBodyID self,
                         Vec3 pos, Vec3 forward, float forward_radius) {
    float flen = sqrtf(forward.x * forward.x + forward.z * forward.z);
    if (flen < 0.001f) return false;
    Vec3 fwd = { forward.x / flen, 0.0f, forward.z / flen };

    float start_y = pos.y - ROOT_HALF_Y + 0.55f;
    Vec3 oxz = { pos.x, 0.0f, pos.z };
    float dists[LADDER_PROBE_COUNT];
    ladder_probe_column(physics, self, oxz, start_y, fwd,
                        LADDER_PROBE_MAX_DIST, dists, LADDER_PROBE_COUNT,
                        LADDER_PROBE_SPACING);
    if (ladder_has_rung_pattern(dists, LADDER_PROBE_COUNT, LADDER_DIST_GAP))
        return true;

    Vec3 mid = { pos.x, pos.y, pos.z };
    RaycastHit hit = physics_raycast(physics, mid, fwd, forward_radius + 1.5f);
    if (hit.hit && hit.body != self
        && ladder_thin_rung_stack(physics, self, hit.body, hit.point))
        return true;

    Vec3 shin = { pos.x, pos.y - ROOT_HALF_Y + 0.85f, pos.z };
    hit = physics_raycast(physics, shin, fwd, forward_radius + 1.5f);
    if (hit.hit && hit.body != self
        && ladder_thin_rung_stack(physics, self, hit.body, hit.point))
        return true;
    return false;
}

static bool ladder_body_contact(const float* dists, int count, float max_close) {
    for (int i = 0; i < count; i++) {
        if (dists[i] < max_close)
            return true;
    }
    return false;
}

static bool ladder_still_touching(PhysicsWorld* physics, PhysicsBodyID self,
                                  Vec3 pos, Vec3 into, float max_close) {
    float ilen = sqrtf(into.x * into.x + into.z * into.z);
    if (ilen < 0.001f) return false;
    into.x /= ilen;
    into.z /= ilen;
    Vec3 side = { -into.z, 0.0f, into.x };
    float feet_y = pos.y - ROOT_HALF_Y + 0.06f;

    const int foot_probes = 6;
    const float foot_span = 1.35f;
    float foot_spacing = foot_span / (float)(foot_probes - 1);
    float offsets[3] = { -0.7f, 0.0f, 0.7f };
    float dists[6];
    for (int o = 0; o < 3; o++) {
        Vec3 oxz = {
            pos.x + side.x * offsets[o],
            0.0f,
            pos.z + side.z * offsets[o]
        };
        ladder_probe_column(physics, self, oxz, feet_y, into,
                            LADDER_PROBE_MAX_DIST, dists, foot_probes, foot_spacing);
        if (ladder_body_contact(dists, foot_probes, max_close))
            return true;
    }
    return false;
}

static bool ladder_face_at_height(PhysicsWorld* physics, PhysicsBodyID self,
                                  Vec3 pos, Vec3 into, float at_y, float max_dist,
                                  float* out_hit_y) {
    float ilen = sqrtf(into.x * into.x + into.z * into.z);
    if (ilen < 0.001f) return false;
    Vec3 dir = { into.x / ilen, 0.0f, into.z / ilen };
    Vec3 ro = { pos.x, at_y, pos.z };
    RaycastHit hit = physics_raycast(physics, ro, dir, max_dist);
    if (!hit.hit || hit.body == self) return false;
    if (out_hit_y) *out_hit_y = hit.point.y;
    return true;
}

static void ladder_maintain_standoff(Avatar* av, PhysicsWorld* physics) {
    Vec3 into = { -av->climb_normal.x, 0.0f, -av->climb_normal.z };
    float len = sqrtf(into.x * into.x + into.z * into.z);
    if (len < 0.001f) return;
    into.x /= len;
    into.z /= len;

    float best = 1e9f;
    float heights[4] = {
        av->pos.y - ROOT_HALF_Y * 0.5f,
        av->pos.y - ROOT_HALF_Y * 0.1f,
        av->pos.y + ROOT_HALF_Y * 0.2f,
        av->pos.y + ROOT_HALF_Y * 0.55f
    };
    for (int i = 0; i < 4; i++) {
        Vec3 ro = { av->pos.x, heights[i], av->pos.z };
        RaycastHit hit = physics_raycast(physics, ro, into, LADDER_STANDOFF + 3.0f);
        if (hit.hit && hit.body != av->body && hit.distance < best)
            best = hit.distance;
    }
    if (best > 1e8f) return;

    float adj = LADDER_STANDOFF - best;

    if (adj > 0.12f) adj = 0.12f;
    if (adj < -0.15f) adj = -0.15f;
    av->pos.x += av->climb_normal.x * adj;
    av->pos.z += av->climb_normal.z * adj;
}

static bool ladder_thin_rung_stack(PhysicsWorld* physics, PhysicsBodyID self,
                                   PhysicsBodyID hit_body, Vec3 hit_point) {
    PhysicsBodyInfo info = physics_get_body_info(physics, hit_body);
    if (!info.active) return false;
    if (info.half_extents.y < 0.08f || info.half_extents.y > 0.65f)
        return false;

    float min_he = fminf(info.half_extents.x, fminf(info.half_extents.y, info.half_extents.z));
    if (min_he < 0.06f) return false;

    float hx = info.half_extents.x;
    float hz = info.half_extents.z;
    float horiz = hx > hz ? hx : hz;
    float vert = info.half_extents.y;
    if (horiz < 0.25f || horiz < vert * 1.2f) return false;

    int rung_count = 1;
    float y_first = info.position.y;
    float y = info.position.y + info.half_extents.y;
    float cx0 = info.position.x;
    float cz0 = info.position.z;
    for (int n = 0; n < 8; n++) {
        Vec3 gap_ro = { hit_point.x, y + 0.04f, hit_point.z };
        RaycastHit gap = physics_raycast(physics, gap_ro, (Vec3){0.0f, 1.0f, 0.0f}, 1.6f);

        if (gap.hit && gap.body != self && gap.distance < LADDER_MIN_RUNG_GAP)
            return false;

        Vec3 next_ro = { hit_point.x, y + LADDER_MIN_RUNG_GAP + 0.05f, hit_point.z };
        RaycastHit next = physics_raycast(physics, next_ro, (Vec3){0.0f, 1.0f, 0.0f}, 2.2f);
        if (!next.hit || next.body == self) break;
        PhysicsBodyInfo ninfo = physics_get_body_info(physics, next.body);
        if (!ninfo.active) break;
        if (ninfo.half_extents.y < 0.08f || ninfo.half_extents.y > 0.65f) break;
        float nmin = fminf(ninfo.half_extents.x,
                           fminf(ninfo.half_extents.y, ninfo.half_extents.z));
        if (nmin < 0.06f) break;
        float nh = ninfo.half_extents.x > ninfo.half_extents.z
            ? ninfo.half_extents.x : ninfo.half_extents.z;
        if (nh < 0.25f) break;

        float dx = ninfo.position.x - cx0;
        float dz = ninfo.position.z - cz0;
        if (dx * dx + dz * dz > 1.2f * 1.2f) break;

        rung_count++;
        y = ninfo.position.y + ninfo.half_extents.y;
    }
    float span = y - (y_first - info.half_extents.y);
    return rung_count >= LADDER_MIN_RUNG_COUNT && span >= LADDER_MIN_RUNG_SPAN;
}

static bool ladder_hit_is_flat_decal(PhysicsWorld* physics, PhysicsBodyID self,
                                     PhysicsBodyID hit_body, Vec3 hit_point, Vec3 into) {
    PhysicsBodyInfo info = physics_get_body_info(physics, hit_body);
    if (!info.active) return false;
    float min_he = fminf(info.half_extents.x, fminf(info.half_extents.y, info.half_extents.z));

    if (min_he >= 0.08f) return false;

    float ilen = sqrtf(into.x * into.x + into.z * into.z);
    if (ilen < 0.001f) return true;
    into.x /= ilen; into.z /= ilen; into.y = 0.0f;

    Vec3 ro = {
        hit_point.x + into.x * 0.03f,
        hit_point.y,
        hit_point.z + into.z * 0.03f
    };
    RaycastHit behind = physics_raycast(physics, ro, into, 0.4f);
    if (behind.hit && behind.body != self)
        return true;

    return min_he < 0.05f;
}

void avatar_init(Avatar* av, Scene* scene, PhysicsWorld* physics) {
    av->walk_speed = AVATAR_WALK_SPEED;
    av->jump_impulse = AVATAR_JUMP_IMPULSE;
    av->gravity = AVATAR_GRAVITY;
    av->scripted_movement = false;
    av->current_yaw = 0.0f;
    av->mode = AVATAR_MODE_PLAYER;
    av->health = 100;
    av->dead = false;
    av->shift_lock = false;

    av->entity = scene_create_entity(scene);
    Entity* ent = scene_get_entity(scene, av->entity);
    if (ent) {
        ent->transform.position = (Vec3){ 0.0f, 0.0f, 0.0f };
        ent->transform.scale = (Vec3){ AVATAR_SCALE, AVATAR_SCALE, AVATAR_SCALE };
    }

    BodyDesc desc = {
        .type = BODY_KINEMATIC,
        .collider = COLLIDER_BOX,
        .position = { 0.0f, AVATAR_SPAWN_Y, 0.0f },
        .half_extents = { ROOT_HALF_X, ROOT_HALF_Y, ROOT_HALF_Z },
        .mass = 80.0f,
        .restitution = 0.0f,
        .friction = 0.0f
    };
    av->body = physics_create_body(physics, &desc);

    physics_set_geom_bits(physics, av->body, 0x1, ~0UL);

    av->pos = (Vec3){ 0.0f, AVATAR_SPAWN_Y, 0.0f };
    av->vel = (Vec3){ 0.0f, 0.0f, 0.0f };
    av->move_intent_x = 0.0f;
    av->move_intent_z = 0.0f;
    av->death_vel = (Vec3){ 0.0f, 0.0f, 0.0f };
    av->on_ground = false;
    av->climbing = false;
    av->climb_anchor_y = 0.0f;
    av->climb_anchor_locked = false;
    av->ground_body = PHYSICS_BODY_INVALID;
    av->ground_prev_pos = (Vec3){0, 0, 0};
    av->has_ground_platform = false;

    if (ent) ent->physics_body = av->body;
}

static void avatar_clear_platform(Avatar* av) {
    av->has_ground_platform = false;
    av->ground_body = PHYSICS_BODY_INVALID;
}

static bool avatar_has_jump_headroom(PhysicsWorld* physics, PhysicsBodyID self,
                                     Vec3 pos, float facing_rad) {
    float cos_f = cosf(facing_rad);
    float sin_f = sinf(facing_rad);
    const float inset = 0.75f;
    const float samples[][2] = {
        { 0.0f, 0.0f },
        {  ROOT_HALF_X * inset,  ROOT_HALF_Z * inset },
        { -ROOT_HALF_X * inset,  ROOT_HALF_Z * inset },
        {  ROOT_HALF_X * inset, -ROOT_HALF_Z * inset },
        { -ROOT_HALF_X * inset, -ROOT_HALF_Z * inset },
    };

    float head_y = pos.y + ROOT_HALF_Y - 0.08f;
    float need = JUMP_MIN_HEADROOM;
    for (int i = 0; i < 5; i++) {
        float lx = samples[i][0], lz = samples[i][1];
        float wx = lx * cos_f - lz * sin_f;
        float wz = lx * sin_f + lz * cos_f;
        Vec3 ro = { pos.x + wx, head_y, pos.z + wz };
        RaycastHit hit = physics_raycast(physics, ro, (Vec3){ 0.0f, 1.0f, 0.0f }, need + 0.05f);
        if (hit.hit && hit.body != self && hit.distance < need)
            return false;
    }
    return true;
}

static bool avatar_find_footprint_support(PhysicsWorld* physics, PhysicsBodyID self,
                                          Vec3 pos, float facing_rad, float max_drop,
                                          float* out_center_y, PhysicsBodyID* out_body) {
    float cos_f = cosf(facing_rad);
    float sin_f = sinf(facing_rad);
    const float inset = 0.85f;
    const float samples[][2] = {
        { 0.0f, 0.0f },
        {  ROOT_HALF_X * inset,  ROOT_HALF_Z * inset },
        { -ROOT_HALF_X * inset,  ROOT_HALF_Z * inset },
        {  ROOT_HALF_X * inset, -ROOT_HALF_Z * inset },
        { -ROOT_HALF_X * inset, -ROOT_HALF_Z * inset },
    };
    float feet_y = pos.y - ROOT_HALF_Y;
    float ray_len = ROOT_HALF_Y + max_drop + 0.15f;
    bool found = false;
    float best_top = -1e9f;
    PhysicsBodyID best_body = PHYSICS_BODY_INVALID;

    for (int i = 0; i < 5; i++) {
        float lx = samples[i][0], lz = samples[i][1];
        float wx = lx * cos_f - lz * sin_f;
        float wz = lx * sin_f + lz * cos_f;
        Vec3 ro = { pos.x + wx, pos.y, pos.z + wz };
        RaycastHit hit = physics_raycast(physics, ro, (Vec3){0, -1, 0}, ray_len);
        if (!hit.hit || hit.body == self || hit.normal.y <= 0.4f) continue;
        float drop = feet_y - hit.point.y;
        if (drop < -0.05f || drop > max_drop) continue;
        if (!found || hit.point.y > best_top) {
            found = true;
            best_top = hit.point.y;
            best_body = hit.body;
        }
    }
    if (!found) return false;
    if (out_center_y) *out_center_y = best_top + ROOT_HALF_Y;
    if (out_body) *out_body = best_body;
    return true;
}

static bool avatar_would_horizontally_overlap(PhysicsWorld* physics, PhysicsBodyID self,
                                              Vec3 pos, float facing_rad) {
    float cos_f = cosf(facing_rad);
    float sin_f = sinf(facing_rad);
    float lx[4] = { ROOT_HALF_X, -ROOT_HALF_X, ROOT_HALF_X, -ROOT_HALF_X };
    float lz[4] = { ROOT_HALF_Z, ROOT_HALF_Z, -ROOT_HALF_Z, -ROOT_HALF_Z };
    float y0 = pos.y - ROOT_HALF_Y + 0.2f;
    float y1 = pos.y + ROOT_HALF_Y - 0.2f;
    for (int hi = 0; hi < 3; hi++) {
        float ty = y0 + (y1 - y0) * ((float)hi * 0.5f);
        for (int c = 0; c < 4; c++) {
            float wx = lx[c] * cos_f - lz[c] * sin_f;
            float wz = lx[c] * sin_f + lz[c] * cos_f;
            float len = sqrtf(wx * wx + wz * wz);
            if (len < 0.001f) continue;
            Vec3 dir = { wx / len, 0.0f, wz / len };
            Vec3 ro = { pos.x, ty, pos.z };
            RaycastHit hit = physics_raycast(physics, ro, dir, len + 0.05f);
            if (hit.hit && hit.body != self && hit.distance < len - 0.02f)
                return true;
        }
    }
    return false;
}

static void avatar_attach_platform(Avatar* av, PhysicsWorld* physics, PhysicsBodyID body);

static bool avatar_try_cancel_fall(Avatar* av, PhysicsWorld* physics, float facing_rad,
                                   float max_drop, bool allow_wedge_cancel) {
    if (!allow_wedge_cancel) return false;

    float ground_y = 0.0f;
    PhysicsBodyID body = PHYSICS_BODY_INVALID;
    if (avatar_find_footprint_support(physics, av->body, av->pos, facing_rad, max_drop,
                                      &ground_y, &body)) {
        av->pos.y = ground_y;
        av->vel.y = 0.0f;
        av->on_ground = true;
        av->climbing = false;
        avatar_attach_platform(av, physics, body);
        return true;
    }

    Vec3 fall_pos = av->pos;
    fall_pos.y -= 0.2f;
    if (avatar_would_horizontally_overlap(physics, av->body, fall_pos, facing_rad)) {
        av->vel.y = 0.0f;
        av->on_ground = true;
        av->climbing = false;
        return true;
    }
    return false;
}

static void avatar_attach_platform(Avatar* av, PhysicsWorld* physics, PhysicsBodyID body) {
    if (!body || body == av->body) {
        avatar_clear_platform(av);
        return;
    }
    PhysicsBodyInfo info = physics_get_body_info(physics, body);
    if (!info.active) {
        avatar_clear_platform(av);
        return;
    }
    av->ground_body = body;
    av->ground_prev_pos = physics_get_position(physics, body);
    av->has_ground_platform = true;
}

static void avatar_follow_platform(Avatar* av, PhysicsWorld* physics) {
    if (!av->has_ground_platform || !av->ground_body || av->ground_body == av->body) {
        avatar_clear_platform(av);
        return;
    }
    PhysicsBodyInfo info = physics_get_body_info(physics, av->ground_body);
    if (!info.active) {
        avatar_clear_platform(av);
        return;
    }
    Vec3 cur = physics_get_position(physics, av->ground_body);
    if (!isfinite(cur.x) || !isfinite(cur.y) || !isfinite(cur.z) ||
        !isfinite(av->ground_prev_pos.x) || !isfinite(av->ground_prev_pos.y) ||
        !isfinite(av->ground_prev_pos.z)) {

        avatar_clear_platform(av);
        return;
    }
    float dx = cur.x - av->ground_prev_pos.x;
    float dy = cur.y - av->ground_prev_pos.y;
    float dz = cur.z - av->ground_prev_pos.z;
    float d2 = dx * dx + dy * dy + dz * dz;

    const float max_follow = 4.0f;
    if (!isfinite(d2) || d2 > max_follow * max_follow) {
        av->ground_prev_pos = cur;
        return;
    }
    av->pos.x += dx;
    av->pos.y += dy;
    av->pos.z += dz;
    av->ground_prev_pos = cur;
}

static bool avatar_sweep_axis(PhysicsWorld* physics, PhysicsBodyID self,
                              Vec3* pos, float facing_rad, bool on_ground,
                              float dx, float dz, bool* already_stepped_up,
                              RaycastHit* out_hit) {
    float move_mag = sqrtf(dx * dx + dz * dz);
    if (move_mag < 0.001f) return false;

    Vec3 move_heading = { dx / move_mag, 0.0f, dz / move_mag };
    float move_angle = atan2f(move_heading.x, move_heading.z);
    float rel_angle = move_angle - facing_rad;
    float forward_radius = fabsf(ROOT_HALF_Z * cosf(rel_angle)) + fabsf(ROOT_HALF_X * sinf(rel_angle));
    float side_radius = fabsf(ROOT_HALF_Z * sinf(rel_angle)) + fabsf(ROOT_HALF_X * cosf(rel_angle));

    bool skip_step = ladder_ahead(physics, self, *pos, move_heading, forward_radius);
    if (on_ground && !skip_step && already_stepped_up && !*already_stepped_up) {
        float new_y = 0.0f;
        if (avatar_try_step_up_any(physics, self, *pos, move_heading,
                                   forward_radius, side_radius, NULL, &new_y)) {
            pos->y = new_y;
            *already_stepped_up = true;
        }
    }

    const int NUM_SLICES = 10;
    float heights[NUM_SLICES];
    float feet_y = pos->y - ROOT_HALF_Y;
    float body_bottom = on_ground ? (feet_y + STEP_HEIGHT + 0.08f) : (feet_y + 0.08f);
    float body_top = pos->y + ROOT_HALF_Y;
    if (body_top < body_bottom + 0.1f) body_top = body_bottom + 0.1f;
    for (int i = 0; i < NUM_SLICES; i++) {
        float t = (float)i / (NUM_SLICES - 1);
        heights[i] = body_bottom + t * (body_top - body_bottom);
    }

    Vec3 side_perp = { -move_heading.z, 0.0f, move_heading.x };
    float footprint_offsets[] = { -side_radius * 0.9f, 0.0f, side_radius * 0.9f };

    RaycastHit closest_hit = {0};
    closest_hit.distance = move_mag + forward_radius;
    bool wall_struck = false;

    for (int h = 0; h < NUM_SLICES; h++) {
        for (int s = 0; s < 3; s++) {
            Vec3 ro = {
                pos->x + side_perp.x * footprint_offsets[s],
                heights[h],
                pos->z + side_perp.z * footprint_offsets[s]
            };
            RaycastHit hit = physics_raycast(physics, ro, move_heading,
                                             move_mag + forward_radius + 0.05f);
            if (hit.hit && hit.body != self) {
                float true_clearance = hit.distance - forward_radius;
                if (true_clearance < 0.0f) true_clearance = 0.0f;
                if (!wall_struck || true_clearance < closest_hit.distance) {
                    closest_hit = hit;
                    closest_hit.distance = true_clearance;
                    wall_struck = true;
                }
            }
        }
    }

    if (!wall_struck) {
        pos->x += dx;
        pos->z += dz;
        if (on_ground) {
            float raise = STEP_HEIGHT + 0.25f;
            Vec3 gro = { pos->x, pos->y + raise, pos->z };
            RaycastHit ghit = physics_raycast(physics, gro, (Vec3){0, -1, 0},
                                               raise + STEP_HEIGHT + 0.35f);
            if (ghit.hit && ghit.body != self && ghit.normal.y > 0.4f) {
                float new_y = ghit.point.y + ROOT_HALF_Y;
                float dy_step = new_y - pos->y;
                if (dy_step > -0.08f && dy_step <= STEP_HEIGHT + 0.02f) {
                    pos->y = new_y;
                    if (dy_step > 0.01f && already_stepped_up) *already_stepped_up = true;
                }
            }
        }
        return false;
    }

    pos->x += move_heading.x * closest_hit.distance;
    pos->z += move_heading.z * closest_hit.distance;

    if (on_ground && !skip_step && already_stepped_up && !*already_stepped_up) {
        Vec3 wall_n = closest_hit.normal;
        float new_y = 0.0f;
        if (avatar_try_step_up_any(physics, self, *pos, move_heading,
                                   forward_radius, side_radius, &wall_n, &new_y)) {
            pos->y = new_y;
            *already_stepped_up = true;
            float used = closest_hit.distance;
            float left = move_mag - used;
            if (left > 0.001f) {
                float rdx = move_heading.x * left;
                float rdz = move_heading.z * left;
                bool no_more_step = true;
                return avatar_sweep_axis(physics, self, pos, facing_rad, on_ground,
                                         rdx, rdz, &no_more_step, out_hit);
            }
            return false;
        }
    }

    if (out_hit) *out_hit = closest_hit;
    return true;
}

void avatar_toggle_mode(Avatar* av, PhysicsWorld* physics) {
    (void)physics;
    if (av->mode == AVATAR_MODE_PLAYER) { av->mode = AVATAR_MODE_PHYSICS; }
    else { av->mode = AVATAR_MODE_PLAYER; }
}

void avatar_update(Avatar* av, const InputState* input, const Camera* cam,
                   PhysicsWorld* physics, Scene* scene, float dt) {
    if (av->mode == AVATAR_MODE_PHYSICS) return;
    av->walk_hit_body = 0;
    if (av->dead) { av->vel = (Vec3){0, 0, 0}; avatar_clear_platform(av); return; }

    if (!isfinite(av->pos.x) || !isfinite(av->pos.y) || !isfinite(av->pos.z) ||
        fabsf(av->pos.x) > 80000.0f || fabsf(av->pos.y) > 80000.0f || fabsf(av->pos.z) > 80000.0f) {

        av->pos = (Vec3){ 0.0f, 10.0f, 0.0f };
        av->vel = (Vec3){ 0, 0, 0 };
        avatar_clear_platform(av);
        if (physics && av->body)
            physics_set_position(physics, av->body, av->pos);
    }

    if (av->freeze_locomotion) {
        av->vel = (Vec3){0, 0, 0};
        av->on_ground = true;
        av->jump_buffered = false;
        physics_set_position(physics, av->body, av->pos);
        Entity* ent = scene_get_entity(scene, av->entity);
        if (ent) {
            ent->transform.position = (Vec3){
                av->pos.x,
                av->pos.y - ROOT_HALF_Y + av->step_offset,
                av->pos.z
            };
            ent->transform.rotation.y = av->current_yaw;
        }
        return;
    }

    physics_disable_geom(physics, av->body);

    if (av->on_ground) {
        avatar_follow_platform(av, physics);
    } else {
        avatar_clear_platform(av);
    }

    float prev_y = av->pos.y;

    if (av->scripted_movement)
        av->climbing = false;

    Vec3 move_dir = { 0.0f, 0.0f, 0.0f };
    Vec3 forward = camera_get_forward_xz(cam);
    Vec3 right = { -forward.z, 0.0f, forward.x };

    if (!av->scripted_movement && !av->climbing) {
        float ax = input->move_x;
        float ay = input->move_y;
        float amag = sqrtf(ax * ax + ay * ay);
        if (amag > 1.0f) {
            ax /= amag;
            ay /= amag;
            amag = 1.0f;
        }

        if (amag > 0.05f) {

            move_dir = vec3_add(vec3_scale(forward, -ay), vec3_scale(right, ax));
            float dlen = vec3_length(move_dir);
            if (dlen > 0.0001f) {
                move_dir = vec3_scale(move_dir, 1.0f / dlen);
                av->vel.x = move_dir.x * av->walk_speed * amag;
                av->vel.z = move_dir.z * av->walk_speed * amag;

                if (!av->shift_lock) {
                    float target_yaw = atan2f(move_dir.x, move_dir.z) * (180.0f / PI_F) + AVATAR_YAW_OFFSET;
                    float diff = target_yaw - av->current_yaw;
                    while (diff > 180.0f) diff -= 360.0f;
                    while (diff < -180.0f) diff += 360.0f;
                    float ease = AVATAR_TURN_SPEED * dt;
                    if (ease > 1.0f) ease = 1.0f;
                    av->current_yaw += diff * ease;
                    while (av->current_yaw >= 360.0f) av->current_yaw -= 360.0f;
                    while (av->current_yaw < 0.0f) av->current_yaw += 360.0f;
                }
            }
        } else {
            if (input->key_w) move_dir = vec3_add(move_dir, forward);
            if (input->key_s) move_dir = vec3_sub(move_dir, forward);
            if (input->key_d) move_dir = vec3_add(move_dir, right);
            if (input->key_a) move_dir = vec3_sub(move_dir, right);

            float len = vec3_length(move_dir);
            if (len > 0.0001f) {
                move_dir = vec3_scale(move_dir, 1.0f / len);
                av->vel.x = move_dir.x * av->walk_speed;
                av->vel.z = move_dir.z * av->walk_speed;

                if (!av->shift_lock) {
                    float target_yaw = atan2f(move_dir.x, move_dir.z) * (180.0f / PI_F) + AVATAR_YAW_OFFSET;
                    float diff = target_yaw - av->current_yaw;
                    while (diff > 180.0f) diff -= 360.0f;
                    while (diff < -180.0f) diff += 360.0f;
                    float ease = AVATAR_TURN_SPEED * dt;
                    if (ease > 1.0f) ease = 1.0f;
                    av->current_yaw += diff * ease;
                    while (av->current_yaw >= 360.0f) av->current_yaw -= 360.0f;
                    while (av->current_yaw < 0.0f) av->current_yaw += 360.0f;
                }
            } else {
                av->vel.x = 0.0f;
                av->vel.z = 0.0f;
            }
        }
    } else if (!av->scripted_movement) {

        av->vel.x = 0.0f;
        av->vel.z = 0.0f;
        move_dir = (Vec3){ 0.0f, 0.0f, 0.0f };
    }

    float len = vec3_length(move_dir);

    if (!av->scripted_movement && av->shift_lock) {
        float target_yaw = cam->yaw + 180.0f + AVATAR_YAW_OFFSET;
        av->current_yaw = target_yaw;
        while (av->current_yaw >= 360.0f) av->current_yaw -= 360.0f;
        while (av->current_yaw < 0.0f) av->current_yaw += 360.0f;
    }

    if (!av->scripted_movement && !av->climbing) {
        float jump_facing = (av->current_yaw - AVATAR_YAW_OFFSET) * (PI_F / 180.0f);
        bool headroom = avatar_has_jump_headroom(physics, av->body, av->pos, jump_facing);
        if (input->key_space || input->key_space_held) {
            if (av->on_ground && headroom) {
                av->vel.y = av->jump_impulse;
                av->on_ground = false;
                av->jump_buffered = false;
                avatar_clear_platform(av);
            } else if (input->key_space) {

                av->jump_buffered = true;
                av->jump_buffer_time = 0.15f;
            }
        }
        if (av->jump_buffered && av->on_ground && headroom) {
            av->vel.y = av->jump_impulse;
            av->on_ground = false;
            av->jump_buffered = false;
            avatar_clear_platform(av);
        }
        if (av->jump_buffered) {
            av->jump_buffer_time -= dt;
            if (av->jump_buffer_time <= 0.0f) av->jump_buffered = false;
        }

        if (!headroom && av->on_ground && input->key_space_held && !input->key_space)
            av->jump_buffered = false;
    }

    if (!av->on_ground && !av->climbing) av->vel.y += av->gravity * dt;

    if (av->climbing && !av->scripted_movement) {
        Vec3 climb_dir = { -av->climb_normal.x, 0.0f, -av->climb_normal.z };
        float clen = sqrtf(climb_dir.x * climb_dir.x + climb_dir.z * climb_dir.z);
        if (clen > 0.001f) {
            climb_dir.x /= clen;
            climb_dir.z /= clen;
        }

        bool climb_up = input->key_w || input->move_y < -0.25f;
        bool climb_down = (input->key_s || input->move_y > 0.25f) && !climb_up;

        float probe_y = av->pos.y + CLIMB_PROBE_OFFSET;
        float reach = LADDER_STANDOFF + 1.8f;
        bool upper_hit = ladder_face_at_height(physics, av->body, av->pos, climb_dir,
                                               probe_y, reach, NULL);
        if (upper_hit) {

            av->climb_anchor_y = probe_y;
            av->climb_anchor_locked = false;
        } else if (!av->climb_anchor_locked) {

            av->climb_anchor_locked = true;
        }

        float feet_y = av->pos.y - ROOT_HALF_Y;
        bool too_far_above = feet_y > av->climb_anchor_y + CLIMB_DETACH_FEET_ABOVE;

        bool landed = false;
        float support_y = 0.0f;
        PhysicsBodyID support_body = PHYSICS_BODY_INVALID;
        if (!too_far_above && climb_down) {
            float face = (av->current_yaw - AVATAR_YAW_OFFSET) * (PI_F / 180.0f);
            if (avatar_find_footprint_support(physics, av->body, av->pos, face,
                                              0.35f, &support_y, &support_body)
                && av->pos.y <= support_y + 0.12f
                && !ladder_thin_rung_stack(physics, av->body, support_body,
                        (Vec3){av->pos.x, support_y, av->pos.z})) {
                av->pos.y = support_y;
                landed = true;
            }
        }

        if (too_far_above || landed) {
            av->climbing = false;
            av->climb_anchor_locked = false;
            av->vel.y = 0.0f;
            if (landed) {
                av->on_ground = true;
                avatar_attach_platform(av, physics, support_body);
            }
        } else if (input->key_space) {
            av->climbing = false;
            av->climb_anchor_locked = false;
            av->vel.y = av->jump_impulse * 0.55f;
            av->vel.x = av->climb_normal.x * 4.0f;
            av->vel.z = av->climb_normal.z * 4.0f;
        } else {
            av->vel.y = 0.0f;
            if (climb_up) av->vel.y = CLIMB_SPEED;
            if (climb_down) av->vel.y = -CLIMB_SPEED * 0.7f;
            av->vel.x = 0.0f;
            av->vel.z = 0.0f;
            ladder_maintain_standoff(av, physics);
        }
    }

    av->move_intent_x = av->vel.x;
    av->move_intent_z = av->vel.z;

    Vec3 current_pos = av->pos;
    bool already_stepped_up = false;
    float facing_rad = (av->current_yaw - AVATAR_YAW_OFFSET) * (PI_F / 180.0f);

    if (!av->climbing) {
        float dx = av->vel.x * dt;
        float dz = av->vel.z * dt;
        RaycastHit hit_x = {0}, hit_z = {0};
        bool blocked_x = false, blocked_z = false;

        if (fabsf(dx) >= fabsf(dz)) {
            if (fabsf(dx) > 0.0001f)
                blocked_x = avatar_sweep_axis(physics, av->body, &current_pos, facing_rad,
                                              av->on_ground, dx, 0.0f, &already_stepped_up, &hit_x);
            if (fabsf(dz) > 0.0001f)
                blocked_z = avatar_sweep_axis(physics, av->body, &current_pos, facing_rad,
                                              av->on_ground, 0.0f, dz, &already_stepped_up, &hit_z);
        } else {
            if (fabsf(dz) > 0.0001f)
                blocked_z = avatar_sweep_axis(physics, av->body, &current_pos, facing_rad,
                                              av->on_ground, 0.0f, dz, &already_stepped_up, &hit_z);
            if (fabsf(dx) > 0.0001f)
                blocked_x = avatar_sweep_axis(physics, av->body, &current_pos, facing_rad,
                                              av->on_ground, dx, 0.0f, &already_stepped_up, &hit_x);
        }

        if (blocked_x) av->vel.x = 0.0f;
        if (blocked_z) av->vel.z = 0.0f;
        if (blocked_x && hit_x.hit && hit_x.body && hit_x.body != av->body)
            av->walk_hit_body = hit_x.body;
        if (blocked_z && hit_z.hit && hit_z.body && hit_z.body != av->body)
            av->walk_hit_body = hit_z.body;

        if (!av->scripted_movement && !av->climbing && len > 0.0001f) {
            RaycastHit* hits[2] = { blocked_x ? &hit_x : NULL, blocked_z ? &hit_z : NULL };
            float axis_dx[2] = { dx, 0.0f };
            float axis_dz[2] = { 0.0f, dz };
            for (int ai = 0; ai < 2; ai++) {
                if (!hits[ai] || !hits[ai]->hit) continue;
                Vec3 move_heading = { 0.0f, 0.0f, 0.0f };
                float amag = sqrtf(axis_dx[ai] * axis_dx[ai] + axis_dz[ai] * axis_dz[ai]);
                if (amag < 0.0001f) continue;
                move_heading.x = axis_dx[ai] / amag;
                move_heading.z = axis_dz[ai] / amag;

                float move_angle = atan2f(move_heading.x, move_heading.z);
                float rel_angle = move_angle - facing_rad;
                float forward_radius = fabsf(ROOT_HALF_Z * cosf(rel_angle))
                                     + fabsf(ROOT_HALF_X * sinf(rel_angle));

                Vec3 normal = hits[ai]->normal;
                normal.y = 0.0f;
                float nlen = sqrtf(normal.x * normal.x + normal.z * normal.z);
                if (nlen > 0.001f) { normal.x /= nlen; normal.z /= nlen; }
                else { normal = (Vec3){ -move_heading.x, 0.0f, -move_heading.z }; }
                if (normal.x * move_heading.x + normal.z * move_heading.z > 0.0f) {
                    normal.x = -normal.x; normal.z = -normal.z;
                }

                float probe_dists[LADDER_PROBE_COUNT];

                float probe_y = current_pos.y - ROOT_HALF_Y + 0.55f;
                Vec3 oxz = { current_pos.x, 0.0f, current_pos.z };
                ladder_probe_column(physics, av->body, oxz, probe_y, move_heading,
                                    LADDER_PROBE_MAX_DIST, probe_dists, LADDER_PROBE_COUNT,
                                    LADDER_PROBE_SPACING);
                bool body_against = ladder_body_contact(probe_dists, LADDER_PROBE_COUNT,
                                                        forward_radius + 1.2f)
                    || ladder_still_touching(physics, av->body, current_pos, move_heading,
                                             forward_radius + 1.6f);
                bool is_ladder = ladder_has_rung_pattern(probe_dists, LADDER_PROBE_COUNT,
                                                         LADDER_DIST_GAP);
                bool thin_rung = ladder_thin_rung_stack(physics, av->body, hits[ai]->body,
                                                        hits[ai]->point);

                if (!thin_rung) {
                    Vec3 shin = {
                        current_pos.x, current_pos.y - ROOT_HALF_Y + 0.85f, current_pos.z
                    };
                    RaycastHit shin_hit = physics_raycast(physics, shin, move_heading,
                                                          forward_radius + 1.5f);
                    if (shin_hit.hit && shin_hit.body != av->body)
                        thin_rung = ladder_thin_rung_stack(physics, av->body, shin_hit.body,
                                                           shin_hit.point);
                }
                bool flat_decal = ladder_hit_is_flat_decal(physics, av->body, hits[ai]->body,
                                                           hits[ai]->point, move_heading);
                float into_dot = -(move_heading.x * normal.x + move_heading.z * normal.z);

                if (body_against && !flat_decal && into_dot > 0.35f
                    && (is_ladder || thin_rung)) {
                    av->climbing = true;
                    av->climb_normal = normal;
                    av->climb_anchor_y = av->pos.y + CLIMB_PROBE_OFFSET;
                    av->climb_anchor_locked = false;

                    av->vel.y = 0.0f;
                    av->vel.x = 0.0f;
                    av->vel.z = 0.0f;
                    av->on_ground = false;
                    av->jump_buffered = false;
                    avatar_clear_platform(av);

                    current_pos.x = av->pos.x;
                    current_pos.z = av->pos.z;
                    current_pos.y = av->pos.y;
                    break;
                }
            }
        }
    }

    av->pos.x = current_pos.x;
    av->pos.z = current_pos.z;
    av->pos.y = current_pos.y;

    if (!av->climbing)
    {
        float cos_f = cosf(facing_rad);
        float sin_f = sinf(facing_rad);

        float cx[4] = {
             ROOT_HALF_X * cos_f - ROOT_HALF_Z * sin_f,
            -ROOT_HALF_X * cos_f - ROOT_HALF_Z * sin_f,
             ROOT_HALF_X * cos_f + ROOT_HALF_Z * sin_f,
            -ROOT_HALF_X * cos_f + ROOT_HALF_Z * sin_f,
        };
        float cz[4] = {
             ROOT_HALF_X * sin_f + ROOT_HALF_Z * cos_f,
            -ROOT_HALF_X * sin_f + ROOT_HALF_Z * cos_f,
             ROOT_HALF_X * sin_f - ROOT_HALF_Z * cos_f,
            -ROOT_HALF_X * sin_f - ROOT_HALF_Z * cos_f,
        };
        float push_x = 0.0f, push_z = 0.0f;
        float test_y[3] = {
            av->pos.y - ROOT_HALF_Y + 0.35f,
            av->pos.y,
            av->pos.y + ROOT_HALF_Y - 0.35f
        };
        for (int yi = 0; yi < 3; yi++) {
            for (int c = 0; c < 4; c++) {
                float len_c = sqrtf(cx[c]*cx[c] + cz[c]*cz[c]);
                if (len_c < 0.001f) continue;
                Vec3 dir = { cx[c]/len_c, 0.0f, cz[c]/len_c };
                Vec3 ro = { av->pos.x, test_y[yi], av->pos.z };
                RaycastHit hit = physics_raycast(physics, ro, dir, len_c + 0.01f);
                if (hit.hit && hit.body != av->body && hit.distance < len_c) {
                    float penetration = len_c - hit.distance;
                    push_x -= dir.x * penetration;
                    push_z -= dir.z * penetration;
                }
            }
        }
        av->pos.x += push_x;
        av->pos.z += push_z;
    }

    bool started_grounded = av->on_ground;
    float dy = av->vel.y * dt;
    if (av->climbing) {
        if (dy > 0.0f) {
            RaycastHit hit = physics_raycast(physics, av->pos, (Vec3){0, 1, 0},
                                             ROOT_HALF_Y + dy + 0.05f);
            if (hit.hit && hit.body != av->body
                && hit.distance < ROOT_HALF_Y + dy) {
                PhysicsBodyInfo info = physics_get_body_info(physics, hit.body);

                if (info.active && info.half_extents.y > 0.65f) {
                    av->pos.y += hit.distance - ROOT_HALF_Y - 0.01f;
                    av->vel.y = 0.0f;
                    dy = 0.0f;
                }
            }
        }
        if (dy != 0.0f)
            av->pos.y += dy;
        av->on_ground = false;
        avatar_clear_platform(av);
        ladder_maintain_standoff(av, physics);
    } else if (dy < 0.0f) {
        float support_y = 0.0f;
        PhysicsBodyID support_body = PHYSICS_BODY_INVALID;
        float fall_drop = fabsf(dy) + 0.05f;
        if (avatar_find_footprint_support(physics, av->body, av->pos, facing_rad,
                                          fall_drop, &support_y, &support_body)
            && av->pos.y + dy <= support_y + 0.02f) {
            av->pos.y = support_y;
            av->vel.y = 0.0f;
            av->on_ground = true;
            av->climbing = false;
            avatar_attach_platform(av, physics, support_body);
        } else if (avatar_try_cancel_fall(av, physics, facing_rad, STEP_HEIGHT,
                                          started_grounded)) {

        } else {
            av->pos.y += dy;
            av->on_ground = false;
            avatar_clear_platform(av);
        }
    } else if (dy > 0.0f) {
        Vec3 ro = av->pos;
        RaycastHit hit = physics_raycast(physics, ro, (Vec3){0,1,0}, ROOT_HALF_Y + dy + 0.05f);
        if (hit.hit && hit.body != av->body && hit.distance < ROOT_HALF_Y + dy) {
            av->pos.y += hit.distance - ROOT_HALF_Y - 0.01f; av->vel.y = 0.0f;
        } else { av->pos.y += dy; }
        av->on_ground = false;
        avatar_clear_platform(av);
    } else {

        float support_y = 0.0f;
        PhysicsBodyID support_body = PHYSICS_BODY_INVALID;
        if (avatar_find_footprint_support(physics, av->body, av->pos, facing_rad,
                                          STEP_HEIGHT, &support_y, &support_body)) {
            float drop = av->pos.y - support_y;
            if (drop >= -0.05f && drop <= STEP_HEIGHT) {
                av->pos.y = support_y;
                av->on_ground = true;
                avatar_attach_platform(av, physics, support_body);
            } else if (!avatar_try_cancel_fall(av, physics, facing_rad, STEP_HEIGHT,
                                               started_grounded)) {
                av->on_ground = false;
                avatar_clear_platform(av);
            }
        } else if (!avatar_try_cancel_fall(av, physics, facing_rad, STEP_HEIGHT,
                                           started_grounded)) {
            av->on_ground = false;
            avatar_clear_platform(av);
        }
    }

    physics_enable_geom(physics, av->body);
    physics_set_position(physics, av->body, av->pos);

    physics_set_rotation_euler(physics, av->body,
                               (Vec3){0, facing_rad * (180.0f / PI_F), 0});

    float y_delta = av->pos.y - prev_y;

    if (started_grounded && av->on_ground &&
        fabsf(y_delta) > 0.01f && fabsf(y_delta) <= STEP_HEIGHT + 0.05f) {
        av->step_offset -= y_delta;
    } else if (!started_grounded) {
        av->step_offset = 0.0f;
    }

    if (av->step_offset > STEP_HEIGHT) av->step_offset = STEP_HEIGHT;
    if (av->step_offset < -STEP_HEIGHT) av->step_offset = -STEP_HEIGHT;
    if (fabsf(av->step_offset) > 0.001f) {
        av->step_offset *= 0.88f;
        if (fabsf(av->step_offset) < 0.001f) av->step_offset = 0.0f;
    }

    Entity* ent = scene_get_entity(scene, av->entity);
    if (ent) {
        ent->transform.position = (Vec3){ av->pos.x, av->pos.y - ROOT_HALF_Y + av->step_offset, av->pos.z };
        ent->transform.rotation.y = av->current_yaw;
    }
}

void avatar_set_health(Avatar* av, int amount) {
    if (amount < 0 || amount > 100) return;
    bool was_dead = av->dead;
    av->health = amount;
    if (av->health <= 0) {
        if (!was_dead) {

            av->death_vel = av->vel;
        }
        av->dead = 1;
    } else {
        if (was_dead) {
            av->on_ground = false;
            av->vel = (Vec3){0, 0, 0};
            av->death_vel = (Vec3){0, 0, 0};
            avatar_clear_platform(av);
        }
        av->dead = 0;
    }
}
