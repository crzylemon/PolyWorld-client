/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar_viewer.c                                                                     |
|   Purpose: tiny wasm avatar preview for the website editor                                  |
\*-------------------------------------------------------------------------------------------*/

#include "platform.h"
#include "renderer.h"
#include "scene.h"
#include "mesh_loader.h"
#include "math_types.h"
#include "texture.h"
#include "avatar_anim.h"
#include "accessory.h"
#include "log.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include <GL/glew.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEX_SLOT_SHIRT 0
#define TEX_SLOT_PANTS 1
#define TEX_SLOT_HEAD  2

static struct {
    Renderer renderer;
    Scene scene;
    AvatarAnim body_legacy;
    AvatarAnim body_new;
    AvatarAnim anim;
    bool body_legacy_ready;
    bool body_new_ready;
    int mesh_flags;
    EntityID avatar_entity;
    float cam_yaw;
    float cam_pitch;
    float cam_dist;
    Vec3 cam_target;
    bool initialized;

    uint32_t tex[3];

    Accessory accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    uint32_t accessory_tex[PW_MAX_EQUIPPED_ACCESSORIES];

    float skin_r, skin_g, skin_b;
} g;

void input_on_keydown(int k) { (void)k; }
void input_on_keyup(int k) { (void)k; }
void input_on_mousedown(int b) { (void)b; }
void input_on_mouseup(int b) { (void)b; }
void input_on_mousemove(float dx, float dy) { (void)dx; (void)dy; }
void input_on_scroll(float d) { (void)d; }
bool chat_handle_key(int k, bool s, bool c) { (void)k; (void)s; (void)c; return false; }
bool chat_handle_char(unsigned int c) { (void)c; return false; }
bool chat_handle_click(float x, float y) { (void)x; (void)y; return false; }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void resize_canvas(int w, int h) {
    if (g.initialized) renderer_resize(&g.renderer, w, h);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_rotate(float dx, float dy) {
    g.cam_yaw -= dx * 0.5f;
    g.cam_pitch += dy * 0.3f;
    if (g.cam_pitch > 60.0f) g.cam_pitch = 60.0f;
    if (g.cam_pitch < -30.0f) g.cam_pitch = -30.0f;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_zoom(float delta) {
    g.cam_dist += delta * 0.5f;
    if (g.cam_dist < 2.0f) g.cam_dist = 2.0f;
    if (g.cam_dist > 12.0f) g.cam_dist = 12.0f;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_set_color(float r, float gc, float b) {
    g.skin_r = r; g.skin_g = gc; g.skin_b = b;
}

static void apply_mesh_flags(void) {
    if (!g.body_legacy_ready && !g.body_new_ready) return;
    avatar_anim_detach(&g.anim);
    avatar_anim_apply_mesh_flags(
        &g.anim,
        g.body_legacy_ready ? &g.body_legacy : NULL,
        g.body_new_ready ? &g.body_new : NULL,
        g.mesh_flags);
    Entity* ent = scene_get_entity(&g.scene, g.avatar_entity);
    if (ent && (g.anim.parts[0].valid || g.anim.parts[1].valid))
        ent->mesh = NULL;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_set_mesh_flags(int flags) {
    if (flags < 0) flags = 0;
    if (flags > 7) flags = 0;
    g.mesh_flags = flags;
    apply_mesh_flags();
}

typedef struct { int slot; } TexLoadCtx;

static void on_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    TexLoadCtx* ctx = (TexLoadCtx*)user;
    int slot = ctx->slot;
    free(ctx);

    if (!data || len == 0) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    uint32_t tex_id = texture_load_atlas_from_memory(pixels, w, h, 4);
    stbi_image_free(pixels);

    if (tex_id) {
        g.tex[slot] = tex_id;
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_load_slot_texture(int slot, const char* url) {
    if (slot < 0 || slot > 2 || !url || !url[0]) return;
    TexLoadCtx* ctx = (TexLoadCtx*)malloc(sizeof(TexLoadCtx));
    ctx->slot = slot;
    platform_load_file(url, on_tex_loaded, ctx);
}

static void on_accessory_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    if (!data || len == 0) return;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= PW_MAX_EQUIPPED_ACCESSORIES) return;
    accessory_load(&g.accessories[idx], (const char*)data, len);
}

static void on_accessory_tex_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    if (!data || len == 0) return;
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= PW_MAX_EQUIPPED_ACCESSORIES) return;

    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);

    int w, h, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) return;

    g.accessory_tex[idx] = texture_load_atlas_from_memory(pixels, w, h, 4);
    accessory_set_atlas(&g.accessories[idx], pixels, w, h);
    stbi_image_free(pixels);
}

static char g_loaded_acc_csv[128];

static void viewer_unload_all_accessories(void) {
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        accessory_unload(&g.accessories[i]);
        g.accessory_tex[i] = 0;
    }
    g_loaded_acc_csv[0] = '\0';
}

static void viewer_load_accessory_slot(int slot, const char* obj_url, const char* tex_url) {
    if (slot < 0 || slot >= PW_MAX_EQUIPPED_ACCESSORIES) return;
    accessory_unload(&g.accessories[slot]);
    g.accessory_tex[slot] = 0;
    if (obj_url && obj_url[0]) {
        platform_load_file(obj_url, on_accessory_loaded, (void*)(intptr_t)slot);
    }
    if (tex_url && tex_url[0]) {
        platform_load_file(tex_url, on_accessory_tex_loaded, (void*)(intptr_t)slot);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_load_accessory(const char* obj_url, const char* tex_url) {
    viewer_unload_all_accessories();
    viewer_load_accessory_slot(0, obj_url, tex_url);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_set_accessories(const char* csv_ids) {
    const char* csv = (csv_ids && csv_ids[0] && strcmp(csv_ids, "0") != 0) ? csv_ids : "";
    if (strcmp(g_loaded_acc_csv, csv) == 0) return;

    viewer_unload_all_accessories();
    strncpy(g_loaded_acc_csv, csv, sizeof(g_loaded_acc_csv) - 1);
    g_loaded_acc_csv[sizeof(g_loaded_acc_csv) - 1] = '\0';
    if (!csv[0]) return;

    char buf[128];
    strncpy(buf, csv, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int slot = 0;
    char* tok = strtok(buf, ",");
    while (tok && slot < PW_MAX_EQUIPPED_ACCESSORIES) {
        while (*tok == ' ') tok++;
        int id = atoi(tok);
        if (id > 0) {
            char obj_url[256], tex_url[256];

            snprintf(obj_url, sizeof(obj_url),
                     "https://polyworld.games/uploads/accessories/%d.obj", id);
            snprintf(tex_url, sizeof(tex_url),
                     "https://polyworld.games/uploads/accessories/%d.png", id);
            viewer_load_accessory_slot(slot, obj_url, tex_url);
            slot++;
        }
        tok = strtok(NULL, ",");
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void viewer_unload_accessory(void) {
    viewer_unload_all_accessories();
}

static void on_body_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    intptr_t which = (intptr_t)user;
    if (!data || len == 0) return;

    AvatarAnim* target = (which == 0) ? &g.body_legacy : &g.body_new;
    avatar_anim_clear(target);
    if (!avatar_anim_load(target, (const char*)data, len)) {
        avatar_anim_clear(target);
        return;
    }
    if (which == 0) g.body_legacy_ready = true;
    else g.body_new_ready = true;

    apply_mesh_flags();
}

static int part_to_slot(int part) {
    switch (part) {
        case ANIM_PART_HEAD: return TEX_SLOT_HEAD;
        case ANIM_PART_TORSO:
        case ANIM_PART_RIGHT_ARM:
        case ANIM_PART_LEFT_ARM: return TEX_SLOT_SHIRT;
        case ANIM_PART_RIGHT_LEG:
        case ANIM_PART_LEFT_LEG: return TEX_SLOT_PANTS;
        default: return TEX_SLOT_SHIRT;
    }
}

static void frame(double dt) {
    if (!g.initialized) return;

    avatar_anim_update(&g.anim, ANIM_STATE_IDLE, 0.0f, (float)dt);

    float yaw_rad = g.cam_yaw * (float)M_PI / 180.0f;
    float pitch_rad = g.cam_pitch * (float)M_PI / 180.0f;
    float cx = g.cam_target.x + g.cam_dist * cosf(pitch_rad) * sinf(yaw_rad);
    float cy = g.cam_target.y + g.cam_dist * sinf(pitch_rad);
    float cz = g.cam_target.z + g.cam_dist * cosf(pitch_rad) * cosf(yaw_rad);

    Vec3 cam_pos = {cx, cy, cz};
    Mat4 view = mat4_look_at(cam_pos, g.cam_target, (Vec3){0, 1, 0});
    float aspect = (float)g.renderer.canvas_width / (float)g.renderer.canvas_height;
    Mat4 projection = mat4_perspective(45.0f, aspect, 0.1f, 100.0f);

    renderer_begin_frame(&g.renderer);
    renderer_render_scene(&g.renderer, &g.scene, &view, &projection);

    if (g.anim.parts[0].valid || g.anim.parts[1].valid) {
        Vec3 pos = {0, 0, 0};
        float yaw = 0.0f;
        Vec3 color = {g.skin_r, g.skin_g, g.skin_b};
        float scale = AVATAR_PREVIEW_SCALE;

        for (int p = 0; p < AVATAR_PART_COUNT; p++) {
            if (!g.anim.parts[p].valid) continue;
            Mat4 part_mat = avatar_anim_get_part_matrix(&g.anim, p, pos, yaw, scale);

            int slot = part_to_slot(p);
            uint32_t tex = g.tex[slot];
            int tex_mode = tex ? 3 : 0;

            renderer_draw_mesh(&g.renderer, &g.anim.parts[p].mesh,
                               &part_mat, color, tex, tex_mode, &view, &projection);
        }

        for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
            Accessory* acc = &g.accessories[ai];
            if (!acc->loaded) continue;
            for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
                if (!acc->parts[i].valid) continue;
                int attach = acc->parts[i].attach_part;
                if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;
                Mat4 acc_mat = avatar_anim_get_part_matrix(&g.anim, attach, pos, yaw, scale);
                Vec3 acc_color = g.accessory_tex[ai] ? (Vec3){1.0f, 1.0f, 1.0f} : color;
                renderer_draw_mesh(&g.renderer, &acc->parts[i].mesh,
                                   &acc_mat, acc_color, g.accessory_tex[ai],
                                   g.accessory_tex[ai] ? 5 : 0, &view, &projection);
            }
        }
    }

    renderer_end_frame(&g.renderer);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    memset(&g, 0, sizeof(g));
    pw_log_parse_args(argc, argv);

    g.cam_yaw = 200.0f;
    g.cam_pitch = 10.0f;
    g.cam_dist = 6.0f;
    g.cam_target = (Vec3){0, 2.2f, 0};
    g.skin_r = 0.96f; g.skin_g = 0.96f; g.skin_b = 0.96f;
    g.mesh_flags = 0;

    int w = 400, h = 500;
#ifdef __EMSCRIPTEN__
    {
        double css_w, css_h;
        emscripten_get_element_css_size("#canvas", &css_w, &css_h);
        w = (int)css_w; h = (int)css_h;
    }
#endif

    if (!platform_init(w, h, "Avatar Viewer")) return 1;
    if (!renderer_init(&g.renderer, w, h, false)) return 1;

    memset(&g.scene, 0, sizeof(Scene));

    g.avatar_entity = scene_create_entity(&g.scene);
    Entity* ent = scene_get_entity(&g.scene, g.avatar_entity);
    ent->transform.position = (Vec3){0, 0, 0};
    ent->transform.scale = (Vec3){AVATAR_PREVIEW_SCALE, AVATAR_PREVIEW_SCALE, AVATAR_PREVIEW_SCALE};
    ent->transform.rotation = (Vec3){0, 0, 0};
    ent->material.color = (Vec3){g.skin_r, g.skin_g, g.skin_b};

    g.initialized = true;

#ifdef __EMSCRIPTEN__
    platform_load_file("https://polyworld.games/assets/wasm/avatar.obj",
                       on_body_loaded, (void*)(intptr_t)0);
    platform_load_file("https://polyworld.games/assets/wasm/new.obj",
                       on_body_loaded, (void*)(intptr_t)1);
#else
    platform_load_file("assets/avatar.obj", on_body_loaded, (void*)(intptr_t)0);
    platform_load_file("assets/new.obj", on_body_loaded, (void*)(intptr_t)1);
#endif

    platform_run_loop(frame);
    return 0;
}
