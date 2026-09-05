/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: gfx_benchmark.c                                                                     |
|   Purpose: stress scene so we can pick a graphics preset                                    |
\*-------------------------------------------------------------------------------------------*/

#include "gfx_benchmark.h"
#include "math_types.h"
#include "mesh_primitives.h"
#include "mesh_loader.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BENCH_PLAYERS 28
#define BENCH_PROP_COUNT 220

static GPUMesh s_box;
static bool s_box_ready = false;
static AvatarAnim s_players[BENCH_PLAYERS];
static bool s_players_inited = false;

static bool ensure_box(void) {
    if (s_box_ready) return true;
    MeshData md;
    memset(&s_box, 0, sizeof(s_box));
    if (!create_box_mesh(&md, 0.5f, 0.5f, 0.5f)) return false;
    if (!mesh_upload(&md, &s_box)) {
        mesh_data_free(&md);
        return false;
    }
    mesh_data_free(&md);
    s_box_ready = true;
    return true;
}

void gfx_benchmark_shutdown(void) {
    if (s_box_ready) {
        mesh_gpu_free(&s_box);
        memset(&s_box, 0, sizeof(s_box));
        s_box_ready = false;
    }
    memset(s_players, 0, sizeof(s_players));
    s_players_inited = false;
}

static Mat4 box_model(Vec3 pos, Vec3 half_size, float yaw_deg) {
    Mat4 t = mat4_translate(pos);
    Mat4 r = mat4_rotate_y(yaw_deg);
    Mat4 s = mat4_scale((Vec3){ half_size.x * 2.0f, half_size.y * 2.0f, half_size.z * 2.0f });
    return mat4_multiply(t, mat4_multiply(r, s));
}

static void emit_box(Renderer* r, Vec3 pos, Vec3 half_size, float yaw_deg, Vec3 color,
                     const Mat4* view, const Mat4* proj) {
    Mat4 m = box_model(pos, half_size, yaw_deg);
    renderer_draw_mesh(r, &s_box, &m, color, 0, 0, view, proj);
}

static void sync_player_meshes(const AvatarAnim* src) {
    if (!src || !src->parts[0].valid) {
        s_players_inited = false;
        return;
    }
    for (int i = 0; i < BENCH_PLAYERS; i++) {
        if (!s_players_inited) {
            memset(&s_players[i], 0, sizeof(s_players[i]));
            s_players[i].walk_phase = (float)i * 0.55f;
        }
        for (int p = 0; p < AVATAR_PART_COUNT; p++)
            s_players[i].parts[p] = src->parts[p];
    }
    s_players_inited = true;
}

static void emit_avatar(Renderer* r, AvatarAnim* anim, Vec3 pos, float yaw,
                        Vec3 skin, uint32_t tex_shirt, uint32_t tex_pants, uint32_t tex_head,
                        const Mat4* view, const Mat4* proj) {
    if (!anim || !anim->parts[0].valid) return;
    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        if (!anim->parts[p].valid) continue;
        Mat4 part_mat = avatar_anim_get_part_matrix(anim, p, pos, yaw, AVATAR_SCALE);
        uint32_t tex = tex_shirt;
        if (p == ANIM_PART_HEAD) tex = tex_head;
        else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) tex = tex_pants;
        int tex_mode = tex ? 3 : 0;
        renderer_set_shadow_id(r, (uint32_t)(1000 + p));
        renderer_draw_mesh(r, &anim->parts[p].mesh, &part_mat, skin, tex, tex_mode, view, proj);
    }
}

static void prop_at(int i, float* out_x, float* out_y, float* out_z,
                    float* out_hx, float* out_hy, float* out_hz, float* out_yaw) {

    float a = (float)i * 2.399963f;
    float rad = 4.0f + fmodf((float)(i * 17), 38.0f);
    *out_x = cosf(a) * rad;
    *out_z = sinf(a) * rad;
    int kind = i % 7;
    if (kind == 0) {
        *out_hx = 1.2f; *out_hy = 0.4f + (float)(i % 5) * 0.35f; *out_hz = 1.2f;
        *out_y = *out_hy;
    } else if (kind == 1) {
        *out_hx = 0.6f; *out_hy = 2.0f + (float)(i % 6) * 0.8f; *out_hz = 0.6f;
        *out_y = *out_hy;
    } else if (kind == 2) {
        *out_hx = 2.5f; *out_hy = 0.35f; *out_hz = 0.5f;
        *out_y = *out_hy + (float)(i % 3) * 1.2f;
    } else if (kind == 3) {
        *out_hx = 0.5f; *out_hy = 0.5f; *out_hz = 0.5f;
        *out_y = 0.5f + (float)(i % 8) * 0.55f;
    } else if (kind == 4) {
        *out_hx = 1.8f; *out_hy = 1.8f; *out_hz = 1.8f;
        *out_y = 1.8f;
    } else if (kind == 5) {
        *out_hx = 0.4f; *out_hy = 4.5f + (float)(i % 4); *out_hz = 0.4f;
        *out_y = *out_hy;
    } else {
        *out_hx = 3.0f; *out_hy = 0.25f; *out_hz = 3.0f;
        *out_y = 0.25f;
    }
    *out_yaw = (float)(i * 37 % 360);
}

static Vec3 prop_color(int i) {
    static const Vec3 palette[] = {
        { 0.75f, 0.22f, 0.22f }, { 0.22f, 0.45f, 0.85f }, { 0.25f, 0.7f, 0.35f },
        { 0.9f, 0.75f, 0.2f }, { 0.65f, 0.3f, 0.8f }, { 0.9f, 0.5f, 0.15f },
        { 0.55f, 0.55f, 0.6f }, { 0.95f, 0.95f, 0.95f }, { 0.2f, 0.2f, 0.22f }
    };
    return palette[i % 9];
}

void gfx_benchmark_render(Renderer* r, float time_sec, int sw, int sh,
                          const GfxBenchmarkAssets* assets) {
    if (!r || sw < 1 || sh < 1) return;
    if (!ensure_box()) return;

    const AvatarAnim* src = (assets && assets->anim && assets->anim->parts[0].valid)
        ? assets->anim : NULL;
    sync_player_meshes(src);

    uint32_t tex_shirt = assets ? assets->tex_shirt : 0;
    uint32_t tex_pants = assets ? assets->tex_pants : 0;
    uint32_t tex_head = assets ? assets->tex_head : 0;
    Vec3 skin = assets ? assets->skin_color : (Vec3){ 0.92f, 0.78f, 0.65f };
    if (skin.x + skin.y + skin.z < 0.05f)
        skin = (Vec3){ 0.92f, 0.78f, 0.65f };

    float prev_range = r->shadow_range;
    int prev_soft = r->shadow_soft;
    int prev_map = r->shadow_map_size;
    float prev_scale = r->render_scale;
    r->shadow_range = 50.0f;
    r->shadow_soft = 0;
    renderer_set_shadow_map_size(r, 2048);
    {
        float budget = 1280.0f * 720.0f;
        float px = (float)sw * (float)sh;
        float scale = 1.0f;
        if (px > budget) scale = sqrtf(budget / px);
        if (scale < 0.40f) scale = 0.40f;
        renderer_set_render_scale(r, scale);
    }

    float aspect = (float)sw / (float)sh;
    float orbit = time_sec * 0.35f;
    Vec3 target = { 0.0f, 3.0f, 0.0f };
    Vec3 eye = {
        sinf(orbit) * 42.0f,
        18.0f + sinf(time_sec * 0.55f) * 2.0f,
        cosf(orbit) * 42.0f
    };
    Mat4 view = mat4_look_at(eye, target, (Vec3){ 0.0f, 1.0f, 0.0f });
    Mat4 proj = mat4_perspective(60.0f, aspect, 0.1f, 300.0f);

    bool prev_fog = r->fog_enabled;
    r->fog_enabled = false;
    r->clear_r = 0.45f;
    r->clear_g = 0.68f;
    r->clear_b = 0.95f;

    float anim_dt = 1.0f / 60.0f;
    if (s_players_inited) {
        for (int i = 0; i < BENCH_PLAYERS; i++)
            avatar_anim_update(&s_players[i], ANIM_STATE_WALKING, 10.0f + (float)(i % 5), anim_dt);
    }

    renderer_begin_frame(r);

    emit_box(r, (Vec3){ 0, -0.4f, 0 }, (Vec3){ 55.0f, 0.4f, 55.0f }, 0.0f,
             (Vec3){ 0.32f, 0.55f, 0.30f }, &view, &proj);

    for (int i = 0; i < BENCH_PROP_COUNT; i++) {
        float x, y, z, hx, hy, hz, yaw;
        prop_at(i, &x, &y, &z, &hx, &hy, &hz, &yaw);
        emit_box(r, (Vec3){ x, y, z }, (Vec3){ hx, hy, hz }, yaw, prop_color(i),
                 &view, &proj);
    }

    static const Vec3 skins[] = {
        { 0.92f, 0.78f, 0.65f }, { 0.55f, 0.35f, 0.22f }, { 0.85f, 0.70f, 0.55f },
        { 0.40f, 0.26f, 0.16f }, { 0.98f, 0.88f, 0.78f }, { 0.70f, 0.50f, 0.38f }
    };

    if (s_players_inited) {
        for (int i = 0; i < BENCH_PLAYERS; i++) {
            float orbit_r = 7.0f + (float)(i % 5) * 2.8f;
            float speed = 0.55f + (float)(i % 4) * 0.12f;
            float ang = time_sec * speed + (float)i * ((float)M_PI * 2.0f / (float)BENCH_PLAYERS);
            Vec3 pos = { cosf(ang) * orbit_r, 0.0f, sinf(ang) * orbit_r };
            float yaw = ang * (180.0f / (float)M_PI) + 90.0f;
            Vec3 sk = skins[i % 6];
            emit_avatar(r, &s_players[i], pos, yaw, sk, tex_shirt, tex_pants, tex_head,
                        &view, &proj);
        }
    } else {
        for (int i = 0; i < 16; i++) {
            float ang = time_sec * 0.7f + (float)i * ((float)M_PI * 2.0f / 16.0f);
            float rad = 10.0f;
            Vec3 pos = { cosf(ang) * rad, 2.5f, sinf(ang) * rad };
            emit_box(r, pos, (Vec3){ 0.7f, 1.2f, 0.4f }, ang * 57.3f + 90.0f,
                     skins[i % 6], &view, &proj);
        }
    }

    renderer_present_scaled_3d(r);
    renderer_end_frame(r);

    r->fog_enabled = prev_fog;
    r->shadow_range = prev_range;
    r->shadow_soft = prev_soft;
    if (prev_map > 0) renderer_set_shadow_map_size(r, prev_map);
    renderer_set_render_scale(r, prev_scale > 0.01f ? prev_scale : 1.0f);
}
