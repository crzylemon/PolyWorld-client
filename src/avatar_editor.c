/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar_editor.c                                                                     |
|   Purpose: in-game avatar editor + 3D preview                                               |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__

#include "avatar_editor.h"
#include "accessory.h"
#include "auth.h"
#include "avatar_anim.h"
#include "font.h"
#include "input.h"
#include "log.h"
#include "shader.h"
#include "math_types.h"
#include "mesh_loader.h"
#include "platform.h"
#include "renderer.h"
#include "texture.h"
#include "ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern void stbi_image_free(void*);
extern void stbi_set_flip_vertically_on_load(int);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define COL_INK_R (ui_theme_col()->text[0])
#define COL_INK_G (ui_theme_col()->text[1])
#define COL_INK_B (ui_theme_col()->text[2])
#define COL_MUTED_R (ui_theme_col()->muted[0])
#define COL_MUTED_G (ui_theme_col()->muted[1])
#define COL_MUTED_B (ui_theme_col()->muted[2])
#define COL_SOFT_R (ui_theme_col()->soft[0])
#define COL_SOFT_G (ui_theme_col()->soft[1])
#define COL_SOFT_B (ui_theme_col()->soft[2])
#define COL_LINE_R (ui_theme_col()->line[0])
#define COL_LINE_G (ui_theme_col()->line[1])
#define COL_LINE_B (ui_theme_col()->line[2])
#define COL_PANEL_R (ui_theme_col()->bg[0])
#define COL_PANEL_G (ui_theme_col()->bg[1])
#define COL_PANEL_B (ui_theme_col()->bg[2])
#define COL_ACCENT_R (ui_theme_col()->lime[0])
#define COL_ACCENT_G (ui_theme_col()->lime[1])
#define COL_ACCENT_B (ui_theme_col()->lime[2])
#define COL_ERR_R (ui_theme_col()->err[0])
#define COL_ERR_G (ui_theme_col()->err[1])
#define COL_ERR_B (ui_theme_col()->err[2])
#define COL_PREV_R (ui_theme_col()->preview[0])
#define COL_PREV_G (ui_theme_col()->preview[1])
#define COL_PREV_B (ui_theme_col()->preview[2])
#define COL_TAB_ON_R COL_SOFT_R
#define COL_TAB_ON_G COL_SOFT_G
#define COL_TAB_ON_B COL_SOFT_B
#define COL_BTN_R COL_SOFT_R
#define COL_BTN_G COL_SOFT_G
#define COL_BTN_B COL_SOFT_B
#define COL_LIME_R COL_ACCENT_R
#define COL_LIME_G COL_ACCENT_G
#define COL_LIME_B COL_ACCENT_B
#define COL_ON_LIME_R (ui_theme_col()->on_lime[0])
#define COL_ON_LIME_G (ui_theme_col()->on_lime[1])
#define COL_ON_LIME_B (ui_theme_col()->on_lime[2])

static unsigned int s_color_prog = 0;
static unsigned int s_tex_prog = 0;
static unsigned int s_round_prog = 0;

static struct {
    bool mesh_ready;
    AvatarAnim anim;
    AvatarAnim body_legacy;
    AvatarAnim body_new;
    bool body_legacy_ready;
    bool body_new_ready;
    Accessory accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    uint32_t tex_shirt, tex_pants, tex_head;
    uint32_t tex_acc[PW_MAX_EQUIPPED_ACCESSORIES];
    uint32_t tex_avatar;
    int loaded_shirt, loaded_pants, loaded_head;
    int loaded_acc[PW_MAX_EQUIPPED_ACCESSORIES];
    int loaded_flags;
    char loaded_host[128];
    char skin_hex[8];
} g_preview;

static int editor_acc_worn_count(const int accessories[PW_MAX_EQUIPPED_ACCESSORIES]) {
    int n = 0;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++)
        if (accessories[i] > 0) n++;
    return n;
}

static bool editor_acc_is_worn(const int accessories[PW_MAX_EQUIPPED_ACCESSORIES], int id) {
    if (id <= 0) return false;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++)
        if (accessories[i] == id) return true;
    return false;
}

static void editor_acc_clear(int accessories[PW_MAX_EQUIPPED_ACCESSORIES]) {
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) accessories[i] = 0;
}

static void editor_acc_toggle(int accessories[PW_MAX_EQUIPPED_ACCESSORIES], int id) {
    if (id <= 0) return;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        if (accessories[i] == id) {
            for (int j = i; j < PW_MAX_EQUIPPED_ACCESSORIES - 1; j++)
                accessories[j] = accessories[j + 1];
            accessories[PW_MAX_EQUIPPED_ACCESSORIES - 1] = 0;
            return;
        }
    }
    if (editor_acc_worn_count(accessories) >= PW_MAX_EQUIPPED_ACCESSORIES) return;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        if (accessories[i] == 0) {
            accessories[i] = id;
            return;
        }
    }
}

static void editor_sync_accessory_mirror(AvatarEditor* ed) {
    if (!ed) return;
    ed->accessory = ed->accessories[0];
}

static char* read_file_alloc(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char* data = (char*)malloc((size_t)sz + 1);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    data[sz] = '\0';
    if (out_len) *out_len = (size_t)sz;
    return data;
}

static int editor_mesh_flags(const AvatarEditor* ed) {
    if (!ed) return 0;
    int flags = 0;
    for (int i = 0; i < ed->item_count; i++) {
        if (strcmp(ed->items[i].mesh_style, "new") != 0) continue;
        if (ed->items[i].id == ed->shirt && strcmp(ed->items[i].type, "shirt") == 0) flags |= 1;
        if (ed->items[i].id == ed->pants && strcmp(ed->items[i].type, "pants") == 0) flags |= 2;
        if (ed->items[i].id == ed->head && strcmp(ed->items[i].type, "head") == 0) flags |= 4;
    }
    return flags;
}

static void apply_preview_mesh_flags(int flags) {
    if (!g_preview.body_legacy_ready && !g_preview.body_new_ready) return;
    avatar_anim_detach(&g_preview.anim);
    avatar_anim_apply_mesh_flags(
        &g_preview.anim,
        g_preview.body_legacy_ready ? &g_preview.body_legacy : NULL,
        g_preview.body_new_ready ? &g_preview.body_new : NULL,
        flags);
    g_preview.loaded_flags = flags;
    g_preview.mesh_ready = g_preview.anim.parts[0].valid ||
                           g_preview.anim.parts[1].valid;
}

static void ensure_shaders(void) {
    if (!s_color_prog) s_color_prog = shader_load_program("ui_color");
    if (!s_tex_prog) s_tex_prog = shader_load_program("ui_tex");
    if (!s_round_prog) s_round_prog = shader_load_program("ui_round");
}

static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b, float a, int sw, int sh) {
    ensure_shaders();
    float nx0 = x / (float)sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)sh * 2.0f;
    float nx1 = (x + w) / (float)sw * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)sh * 2.0f;
    float verts[] = { nx0,ny0, nx1,ny0, nx1,ny1, nx0,ny0, nx1,ny1, nx0,ny1 };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_color_prog);
    glUniform4f(glGetUniformLocation(s_color_prog, "u_color"), r, g, b, a);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static void draw_round_rect(float x, float y, float w, float h, float radius,
                            float r, float g, float b, float a, int sw, int sh) {
    if (w < 0.5f || h < 0.5f || a <= 0.01f) return;
    if (radius < 0.5f || !s_round_prog) {
        draw_rect(x, y, w, h, r, g, b, a, sw, sh);
        return;
    }
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    ensure_shaders();
    float nx0 = x / (float)sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)sh * 2.0f;
    float nx1 = (x + w) / (float)sw * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)sh * 2.0f;
    float verts[] = {
        nx0, ny0, 0, 0, nx1, ny0, 1, 0, nx1, ny1, 1, 1,
        nx0, ny0, 0, 0, nx1, ny1, 1, 1, nx0, ny1, 0, 1
    };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_round_prog);
    glUniform4f(glGetUniformLocation(s_round_prog, "u_color"), r, g, b, a);
    glUniform2f(glGetUniformLocation(s_round_prog, "u_size"), w, h);
    glUniform1f(glGetUniformLocation(s_round_prog, "u_radius"), radius);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static void draw_tex(unsigned int tex, float x, float y, float w, float h, int sw, int sh) {
    if (!tex) return;
    ensure_shaders();
    float nx0 = x / (float)sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)sh * 2.0f;
    float nx1 = (x + w) / (float)sw * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)sh * 2.0f;
    float verts[] = {
        nx0,ny0, 0,0, nx1,ny0, 1,0, nx1,ny1, 1,1,
        nx0,ny0, 0,0, nx1,ny1, 1,1, nx0,ny1, 0,1
    };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_tex_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(s_tex_prog, "u_tex"), 0);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static void parse_hex_color(const char* hex, float* r, float* g, float* b) {
    *r = *g = *b = 0.92f;
    if (!hex || hex[0] != '#') return;
    unsigned int v = 0;
    if (sscanf(hex + 1, "%06x", &v) == 1) {
        *r = ((v >> 16) & 0xFF) / 255.0f;
        *g = ((v >> 8) & 0xFF) / 255.0f;
        *b = (v & 0xFF) / 255.0f;
    }
}

static unsigned int tex_from_png_mem(const uint8_t* buf, size_t len, int flip_v) {
    if (!buf || len < 16) return 0;
    int w, h, c;
    if (flip_v) stbi_set_flip_vertically_on_load(1);
    unsigned char* px = stbi_load_from_memory(buf, (int)len, &w, &h, &c, 4);
    if (flip_v) stbi_set_flip_vertically_on_load(0);
    if (!px) return 0;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    return tex;
}

static uint32_t clothing_tex_from_png_mem_acc(Accessory* acc, const uint8_t* buf, size_t len) {
    if (!buf || len < 16) return 0;
    int w, h, c;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* px = stbi_load_from_memory(buf, (int)len, &w, &h, &c, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!px) return 0;
    if (acc) accessory_set_atlas(acc, px, w, h);
    uint32_t tex = texture_load_atlas_from_memory(px, w, h, 4);
    stbi_image_free(px);
    return tex;
}

static uint32_t clothing_tex_from_png_mem(const uint8_t* buf, size_t len) {
    return clothing_tex_from_png_mem_acc(NULL, buf, len);
}

static unsigned g_ae_thumb_gen = 0;
static unsigned g_preview_load_gen = 0;
static int g_ae_thumbs_inflight = 0;
#define AE_THUMB_MAX_INFLIGHT 4

typedef struct {
    AvatarEditor* ed;
    int index;
    unsigned gen;
} AeThumbCtx;

enum {
    PREVIEW_SLOT_SHIRT = 0,
    PREVIEW_SLOT_PANTS = 1,
    PREVIEW_SLOT_HEAD = 2,
    PREVIEW_SLOT_ACC_OBJ = 10,
    PREVIEW_SLOT_ACC_TEX = 20
};

typedef struct {
    unsigned gen;
    int slot;
    int want_id;
} PreviewLoadCtx;

static void ae_thumb_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    AeThumbCtx* ctx = (AeThumbCtx*)user;
    if (!ctx) return;
    AvatarEditor* ed = ctx->ed;
    int index = ctx->index;
    unsigned gen = ctx->gen;
    free(ctx);
    if (g_ae_thumbs_inflight > 0) g_ae_thumbs_inflight--;
    (void)path;
    if (!ed || gen != g_ae_thumb_gen) return;
    if (index < 0 || index >= ed->item_count) return;
    ed->items[index].thumb_loading = false;
    ed->items[index].thumb_loaded = true;
    unsigned int tex = tex_from_png_mem(data, len, 0);
    if (tex) ed->items[index].thumb_tex = tex;
}

static void delete_tex(uint32_t* tex);

static void preview_tex_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    PreviewLoadCtx* ctx = (PreviewLoadCtx*)user;
    if (!ctx) return;
    unsigned gen = ctx->gen;
    int slot = ctx->slot;
    int want = ctx->want_id;
    free(ctx);
    (void)path;
    if (gen != g_preview_load_gen) return;

    if (slot == PREVIEW_SLOT_SHIRT) {
        if (g_preview.loaded_shirt != want) return;
        delete_tex(&g_preview.tex_shirt);
        g_preview.tex_shirt = clothing_tex_from_png_mem(data, len);
    } else if (slot == PREVIEW_SLOT_PANTS) {
        if (g_preview.loaded_pants != want) return;
        delete_tex(&g_preview.tex_pants);
        g_preview.tex_pants = clothing_tex_from_png_mem(data, len);
    } else if (slot == PREVIEW_SLOT_HEAD) {
        if (g_preview.loaded_head != want) return;
        delete_tex(&g_preview.tex_head);
        g_preview.tex_head = clothing_tex_from_png_mem(data, len);
    } else if (slot >= PREVIEW_SLOT_ACC_TEX && slot < PREVIEW_SLOT_ACC_TEX + PW_MAX_EQUIPPED_ACCESSORIES) {
        int ai = slot - PREVIEW_SLOT_ACC_TEX;
        if (g_preview.loaded_acc[ai] != want) return;
        delete_tex(&g_preview.tex_acc[ai]);
        g_preview.tex_acc[ai] = clothing_tex_from_png_mem_acc(&g_preview.accessories[ai], data, len);
    }
}

static void preview_obj_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    PreviewLoadCtx* ctx = (PreviewLoadCtx*)user;
    if (!ctx) return;
    unsigned gen = ctx->gen;
    int slot = ctx->slot;
    int want = ctx->want_id;
    free(ctx);
    (void)path;
    if (gen != g_preview_load_gen) return;
    if (slot < PREVIEW_SLOT_ACC_OBJ || slot >= PREVIEW_SLOT_ACC_OBJ + PW_MAX_EQUIPPED_ACCESSORIES) return;
    int ai = slot - PREVIEW_SLOT_ACC_OBJ;
    if (g_preview.loaded_acc[ai] != want) return;
    accessory_unload(&g_preview.accessories[ai]);
    if (data && len > 0) {
        accessory_load(&g_preview.accessories[ai], (const char*)data, len);
    }
}

static void preview_start_tex(int slot, int want_id, const char* url) {
    PreviewLoadCtx* ctx = (PreviewLoadCtx*)malloc(sizeof(PreviewLoadCtx));
    if (!ctx) return;
    ctx->gen = g_preview_load_gen;
    ctx->slot = slot;
    ctx->want_id = want_id;
    platform_load_file(url, preview_tex_on_loaded, ctx);
}

static void preview_start_obj(int ai, int want_id, const char* url) {
    PreviewLoadCtx* ctx = (PreviewLoadCtx*)malloc(sizeof(PreviewLoadCtx));
    if (!ctx) return;
    ctx->gen = g_preview_load_gen;
    ctx->slot = PREVIEW_SLOT_ACC_OBJ + ai;
    ctx->want_id = want_id;
    platform_load_file(url, preview_obj_on_loaded, ctx);
}

static void delete_tex(uint32_t* tex) {
    if (tex && *tex) {
        glDeleteTextures(1, tex);
        *tex = 0;
    }
}

static void clear_items(AvatarEditor* ed) {
    g_ae_thumb_gen++;
    g_ae_thumbs_inflight = 0;
    for (int i = 0; i < ed->item_count; i++) {
        if (ed->items[i].thumb_loaded && ed->items[i].thumb_tex)
            glDeleteTextures(1, &ed->items[i].thumb_tex);
        ed->items[i].thumb_tex = 0;
        ed->items[i].thumb_loaded = false;
        ed->items[i].thumb_loading = false;
    }
    ed->item_count = 0;
}

static void ensure_preview_mesh(void) {
    if (g_preview.body_legacy_ready || g_preview.body_new_ready) {
        if (!g_preview.mesh_ready) apply_preview_mesh_flags(g_preview.loaded_flags);
        return;
    }
    size_t leg_len = 0, neu_len = 0;
    char* legacy = read_file_alloc("assets/avatar.obj", &leg_len);
    if (!legacy) legacy = read_file_alloc("website/uploads/packages/33.obj", &leg_len);
    char* neu = read_file_alloc("assets/new.obj", &neu_len);
    if (!neu) neu = read_file_alloc("website/uploads/packages/new.obj", &neu_len);
    if (legacy) {
        memset(&g_preview.body_legacy, 0, sizeof(g_preview.body_legacy));
        if (avatar_anim_load(&g_preview.body_legacy, legacy, leg_len))
            g_preview.body_legacy_ready = true;
        free(legacy);
    }
    if (neu) {
        memset(&g_preview.body_new, 0, sizeof(g_preview.body_new));
        if (avatar_anim_load(&g_preview.body_new, neu, neu_len))
            g_preview.body_new_ready = true;
        free(neu);
    }
    apply_preview_mesh_flags(0);
}

static void ensure_preview_avatar_tex(void) {
    if (g_preview.tex_avatar) return;
    const char* paths[] = {
        "assets/avatar.png",
        "website/assets/wasm/avatar.png",
        "assets/guestavatar.png",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        size_t len = 0;
        char* data = read_file_alloc(paths[i], &len);
        if (!data) continue;
        g_preview.tex_avatar = clothing_tex_from_png_mem((const uint8_t*)data, len);
        free(data);
        if (g_preview.tex_avatar) return;
    }
}

void avatar_preview_set_outfit(const char* host, const char* skin_hex,
                               int shirt, int pants, int head,
                               const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                               int mesh_flags) {
    ensure_preview_mesh();
    ensure_preview_avatar_tex();
    apply_preview_mesh_flags(mesh_flags);
    strncpy(g_preview.skin_hex, (skin_hex && skin_hex[0]) ? skin_hex : "#eaeaea",
            sizeof(g_preview.skin_hex) - 1);
    g_preview.skin_hex[sizeof(g_preview.skin_hex) - 1] = '\0';

    const char* h = (host && host[0]) ? host : "https://polyworld.games";
    bool host_changed = (strcmp(g_preview.loaded_host, h) != 0);
    char url[384];

    if (host_changed || g_preview.loaded_shirt != shirt) {
        if (g_preview.tex_shirt && g_preview.tex_shirt != g_preview.tex_avatar)
            delete_tex(&g_preview.tex_shirt);
        else
            g_preview.tex_shirt = 0;
        g_preview.loaded_shirt = shirt;
        if (shirt > 0) {
            snprintf(url, sizeof(url), "%s/uploads/shirts/%d.png", h, shirt);
            preview_start_tex(PREVIEW_SLOT_SHIRT, shirt, url);
        }
    }
    if (host_changed || g_preview.loaded_pants != pants) {
        if (g_preview.tex_pants && g_preview.tex_pants != g_preview.tex_avatar)
            delete_tex(&g_preview.tex_pants);
        else
            g_preview.tex_pants = 0;
        g_preview.loaded_pants = pants;
        if (pants > 0) {
            snprintf(url, sizeof(url), "%s/uploads/pants/%d.png", h, pants);
            preview_start_tex(PREVIEW_SLOT_PANTS, pants, url);
        }
    }
    if (host_changed || g_preview.loaded_head != head) {
        if (g_preview.tex_head && g_preview.tex_head != g_preview.tex_avatar)
            delete_tex(&g_preview.tex_head);
        else
            g_preview.tex_head = 0;
        g_preview.loaded_head = head;
        if (head > 0) {
            snprintf(url, sizeof(url), "%s/uploads/heads/%d.png", h, head);
            preview_start_tex(PREVIEW_SLOT_HEAD, head, url);
        }
    }
    if (host_changed) {
        for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++)
            g_preview.loaded_acc[ai] = -999;
    }
    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        int want = accessories ? accessories[ai] : 0;
        if (host_changed || g_preview.loaded_acc[ai] != want) {
            accessory_unload(&g_preview.accessories[ai]);
            delete_tex(&g_preview.tex_acc[ai]);
            g_preview.loaded_acc[ai] = want;
            if (want > 0) {
                snprintf(url, sizeof(url), "%s/uploads/accessories/%d.obj", h, want);
                preview_start_obj(ai, want, url);
                snprintf(url, sizeof(url), "%s/uploads/accessories/%d.png", h, want);
                preview_start_tex(PREVIEW_SLOT_ACC_TEX + ai, want, url);
            }
        }
    }
    strncpy(g_preview.loaded_host, h, sizeof(g_preview.loaded_host) - 1);
}

static void sync_preview_appearance(AvatarEditor* ed) {
    if (!ed || !ed->open) return;
    avatar_preview_set_outfit(ed->host, ed->skin_color, ed->shirt, ed->pants, ed->head,
                              ed->accessories, editor_mesh_flags(ed));
}

static void load_inventory(AvatarEditor* ed) {
    clear_items(ed);
    ed->loading = true;
    ed->thumbs_pending = false;
    ed->error[0] = '\0';
    if (!auth_avatar_inventory(ed->session_token, ed->items, AVATAR_EDITOR_MAX_ITEMS, &ed->item_count)) {
        snprintf(ed->error, sizeof(ed->error), "Could not load inventory.");
        ed->loading = false;
        return;
    }

    for (int i = 0; i < ed->item_count; i++) {
        ed->items[i].thumb_tex = 0;
        ed->items[i].thumb_loaded = false;
        ed->items[i].thumb_loading = false;
    }
    ed->thumbs_pending = (ed->item_count > 0);
    ed->loading = false;
}

static void avatar_editor_pump_thumbs(AvatarEditor* ed) {
    if (!ed || !ed->open || !ed->thumbs_pending) return;
    bool any_pending = false;
    for (int i = 0; i < ed->item_count; i++) {
        if (ed->items[i].thumb_loaded) continue;
        any_pending = true;
        if (ed->items[i].thumb_loading) continue;
        if (g_ae_thumbs_inflight >= AE_THUMB_MAX_INFLIGHT) return;
        char url[384];
        if (ed->items[i].image_path[0] == 'h') {
            snprintf(url, sizeof(url), "%s", ed->items[i].image_path);
        } else if (ed->items[i].image_path[0] == '/') {
            snprintf(url, sizeof(url), "%s%s", ed->host, ed->items[i].image_path);
        } else if (ed->items[i].image_path[0]) {
            snprintf(url, sizeof(url), "%s/%s", ed->host, ed->items[i].image_path);
        } else {
            ed->items[i].thumb_loaded = true;
            continue;
        }
        AeThumbCtx* ctx = (AeThumbCtx*)malloc(sizeof(AeThumbCtx));
        if (!ctx) return;
        ctx->ed = ed;
        ctx->index = i;
        ctx->gen = g_ae_thumb_gen;
        ed->items[i].thumb_loading = true;
        g_ae_thumbs_inflight++;
        platform_load_file(url, ae_thumb_on_loaded, ctx);
    }
    if (!any_pending) ed->thumbs_pending = false;
}

void avatar_preview_draw(Renderer* renderer,
                         float prev_x, float prev_y, float prev_w, float prev_h,
                         int screen_w, int screen_h, float dt,
                         float* cam_yaw, float* cam_pitch, float* cam_dist,
                         const Vec3* cam_target, float fov,
                         bool auto_spin, bool dragging, float* drag_lx, float* drag_ly) {
    if (!renderer || !g_preview.mesh_ready) return;
    if (prev_w < 8.0f || prev_h < 8.0f) return;
    if (!cam_yaw || !cam_pitch || !cam_dist) return;
    ensure_preview_avatar_tex();

    const InputState* in = input_get_state();
    if (dragging && in && drag_lx && drag_ly) {
        float dx = in->mouse_x - *drag_lx;
        float dy = in->mouse_y - *drag_ly;
        *cam_yaw -= dx * 0.45f;
        *cam_pitch += dy * 0.35f;
        if (*cam_pitch > 55.0f) *cam_pitch = 55.0f;
        if (*cam_pitch < -25.0f) *cam_pitch = -25.0f;
        *drag_lx = in->mouse_x;
        *drag_ly = in->mouse_y;
    } else if (auto_spin && dt > 0.0f && dt < 0.1f) {
        *cam_yaw += dt * 22.0f;
    }

    avatar_anim_update(&g_preview.anim, ANIM_STATE_IDLE, 0.0f, dt > 0.0f ? dt : 0.016f);

    int vx = (int)prev_x;
    int vw = (int)prev_w;
    int vh = (int)prev_h;
    int vy = screen_h - ((int)prev_y + vh);
    if (vw < 1 || vh < 1) return;

    glEnable(GL_SCISSOR_TEST);
    glScissor(vx, vy, vw, vh);
    glViewport(vx, vy, vw, vh);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(COL_PREV_R, COL_PREV_G, COL_PREV_B, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float yaw_rad = *cam_yaw * (float)M_PI / 180.0f;
    float pitch_rad = *cam_pitch * (float)M_PI / 180.0f;
    float dist = *cam_dist > 0.5f ? *cam_dist : 6.0f;
    Vec3 target = cam_target ? *cam_target : (Vec3){0.0f, 2.2f, 0.0f};
    Vec3 cam_pos = {
        target.x + dist * cosf(pitch_rad) * sinf(yaw_rad),
        target.y + dist * sinf(pitch_rad),
        target.z + dist * cosf(pitch_rad) * cosf(yaw_rad)
    };
    if (fov < 10.0f) fov = 42.0f;
    Mat4 view = mat4_look_at(cam_pos, target, (Vec3){0, 1, 0});
    Mat4 projection = mat4_perspective(fov, prev_w / prev_h, 0.1f, 100.0f);

    float sr, sg, sb;
    parse_hex_color(g_preview.skin_hex[0] ? g_preview.skin_hex : "#eaeaea", &sr, &sg, &sb);
    Vec3 skin = {sr, sg, sb};
    Vec3 pos = {0, 0, 0};
    float yaw = 0.0f;

    uint32_t shirt_tex = g_preview.tex_shirt ? g_preview.tex_shirt : g_preview.tex_avatar;
    uint32_t pants_tex = g_preview.tex_pants ? g_preview.tex_pants : g_preview.tex_avatar;
    uint32_t head_tex = g_preview.tex_head ? g_preview.tex_head : g_preview.tex_avatar;

    Vec3 prev_light_dir = renderer->light_dir;
    Vec3 prev_light_color = renderer->light_color;
    bool prev_shadows = renderer->shadows_enabled;
    bool prev_fog = renderer->fog_enabled;
    renderer->shadows_enabled = false;
    renderer->fog_enabled = false;
    renderer->light_dir = (Vec3){-0.45f, -1.0f, -0.35f};
    renderer->light_color = (Vec3){1.0f, 0.97f, 0.92f};

    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        if (!g_preview.anim.parts[p].valid) continue;
        Mat4 part_mat = avatar_anim_get_part_matrix(&g_preview.anim, p, pos, yaw, AVATAR_PREVIEW_SCALE);
        uint32_t tex = shirt_tex;
        if (p == ANIM_PART_HEAD) tex = head_tex;
        else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) tex = pants_tex;
        int tex_mode = tex ? 3 : 0;
        renderer_draw_mesh(renderer, &g_preview.anim.parts[p].mesh,
                           &part_mat, skin, tex, tex_mode, &view, &projection);
    }

    for (int ai = 0; ai < PW_MAX_EQUIPPED_ACCESSORIES; ai++) {
        Accessory* acc = &g_preview.accessories[ai];
        if (!acc->loaded) continue;
        for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
            if (!acc->parts[i].valid) continue;
            int attach = acc->parts[i].attach_part;
            if (attach < 0 || attach >= AVATAR_PART_COUNT) continue;
            Mat4 acc_mat = avatar_anim_get_part_matrix(&g_preview.anim, attach, pos, yaw, AVATAR_PREVIEW_SCALE);
            Vec3 acc_color = g_preview.tex_acc[ai] ? (Vec3){1, 1, 1} : skin;
            renderer_draw_mesh(renderer, &acc->parts[i].mesh,
                               &acc_mat, acc_color, g_preview.tex_acc[ai],
                               g_preview.tex_acc[ai] ? 5 : 0, &view, &projection);
        }
    }

    renderer->shadows_enabled = prev_shadows;
    renderer->fog_enabled = prev_fog;
    renderer->light_dir = prev_light_dir;
    renderer->light_color = prev_light_color;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, screen_w, screen_h);
}

static void draw_3d_preview(AvatarEditor* ed, Renderer* renderer, int screen_w, int screen_h, float dt) {
    if (!ed) return;
    avatar_preview_draw(renderer, ed->preview_x, ed->preview_y, ed->preview_w, ed->preview_h,
                        screen_w, screen_h, dt,
                        &ed->cam_yaw, &ed->cam_pitch, &ed->cam_dist,
                        NULL, 42.0f,
                        ed->auto_spin, ed->preview_dragging, &ed->drag_lx, &ed->drag_ly);
}

void avatar_editor_init(AvatarEditor* ed) {
    static bool fonts_ready = false;
    if (!ed) return;
    if (!fonts_ready) {
        font_init();
        fonts_ready = true;
    }
}

void avatar_editor_shutdown(AvatarEditor* ed) {
    g_preview_load_gen++;
    clear_items(ed);
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) {
        accessory_unload(&g_preview.accessories[i]);
        delete_tex(&g_preview.tex_acc[i]);
    }
    delete_tex(&g_preview.tex_shirt);
    delete_tex(&g_preview.tex_pants);
    delete_tex(&g_preview.tex_head);
    delete_tex(&g_preview.tex_avatar);
    avatar_anim_detach(&g_preview.anim);
    avatar_anim_clear(&g_preview.body_legacy);
    avatar_anim_clear(&g_preview.body_new);
    memset(&g_preview, 0, sizeof(g_preview));
    memset(ed, 0, sizeof(*ed));
}

void avatar_editor_open(AvatarEditor* ed, const char* session_token, const char* host,
                        const char* skin_color, int shirt, int pants, int head,
                        const int accessories[PW_MAX_EQUIPPED_ACCESSORIES]) {
    ed->open = true;
    ed->saving = false;
    ed->dirty = false;
    ed->tab = 0;
    ed->scroll = 0;
    ed->error[0] = '\0';
    ed->status[0] = '\0';
    ed->panel_x = ed->panel_y = 0;
    ed->panel_w = ed->panel_h = 0;
    ed->preview_dragging = false;
    ed->cam_yaw = 200.0f;
    ed->cam_pitch = 10.0f;
    ed->cam_dist = 6.0f;
    ed->auto_spin = true;

    g_preview.loaded_shirt = g_preview.loaded_pants = g_preview.loaded_head = -999;
    for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++) g_preview.loaded_acc[i] = -999;
    g_preview.loaded_host[0] = '\0';

    if (!session_token || !session_token[0]) {
        ed->session_token[0] = '\0';
        snprintf(ed->error, sizeof(ed->error), "Log in to edit your avatar.");
        ed->item_count = 0;
        ed->loading = false;
        ed->load_deferred = false;
        ed->need_profile_fetch = false;
        ed->thumbs_pending = false;
        return;
    }
    strncpy(ed->session_token, session_token, sizeof(ed->session_token) - 1);
    strncpy(ed->host, host && host[0] ? host : "https://polyworld.games",
            sizeof(ed->host) - 1);
    strncpy(ed->skin_color, skin_color && skin_color[0] ? skin_color : "#eaeaea",
            sizeof(ed->skin_color) - 1);
    ed->shirt = shirt;
    ed->pants = pants;
    ed->head = head;
    editor_acc_clear(ed->accessories);
    if (accessories) {
        for (int i = 0; i < PW_MAX_EQUIPPED_ACCESSORIES; i++)
            ed->accessories[i] = accessories[i];
    }
    editor_sync_accessory_mirror(ed);
    strncpy(ed->base_skin, ed->skin_color, sizeof(ed->base_skin) - 1);
    ed->base_shirt = shirt;
    ed->base_pants = pants;
    ed->base_head = head;
    memcpy(ed->base_accessories, ed->accessories, sizeof(ed->base_accessories));
    ed->base_accessory = ed->accessory;

    ed->need_profile_fetch = true;
    ed->loading = true;
    ed->load_deferred = true;
    ed->thumbs_pending = false;
    ed->skeleton_t = 0.0f;
    ed->item_count = 0;
}

void avatar_editor_close(AvatarEditor* ed) {
    ed->open = false;
    ed->saving = false;
    ed->preview_dragging = false;
    g_preview_load_gen++;
    clear_items(ed);
}

bool avatar_editor_blocks_input(const AvatarEditor* ed) {
    return ed && ed->open;
}

static const char* tab_type(int tab) {
    switch (tab) {
        case 0: return "shirt";
        case 1: return "pants";
        case 2: return "head";
        case 3: return "accessory";
        default: return "shirt";
    }
}

static int* slot_ptr(AvatarEditor* ed, int tab) {
    switch (tab) {
        case 0: return &ed->shirt;
        case 1: return &ed->pants;
        case 2: return &ed->head;
        default: return NULL;
    }
}

static const char* skin_presets[] = {
    "#eaeaea", "#f5d0c5", "#e0ac69", "#c68642", "#8d5524",
    "#5c3a21", "#3b2215", "#ffdbac", "#4da6cb", "#a8e063"
};
#define SKIN_PRESET_COUNT 10

static bool hit(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

void avatar_editor_render(AvatarEditor* ed, Renderer* renderer,
                          int screen_w, int screen_h, float dt) {
    if (!ed->open) return;
    font_init();
    ensure_shaders();
    glViewport(0, 0, screen_w, screen_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    draw_rect(0, 0, (float)screen_w, (float)screen_h, 0.10f, 0.12f, 0.08f, 0.45f, screen_w, screen_h);

    float pw = fminf(820.0f, (float)screen_w - 48.0f);
    float ph = fminf(580.0f, (float)screen_h - 48.0f);
    float px = ((float)screen_w - pw) * 0.5f;
    float py = ((float)screen_h - ph) * 0.5f;
    ed->panel_x = px; ed->panel_y = py; ed->panel_w = pw; ed->panel_h = ph;

    draw_rect(px + 10, py + 12, pw, ph, 0, 0, 0, 0.10f, screen_w, screen_h);
    draw_rect(px + 4, py + 5, pw, ph, 0, 0, 0, 0.08f, screen_w, screen_h);
    draw_rect(px, py, pw, ph, COL_PANEL_R, COL_PANEL_G, COL_PANEL_B, 1.0f, screen_w, screen_h);

    draw_rect(px, py, 4.0f, ph, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
    draw_rect(px, py, pw, 52.0f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
    draw_rect(px, py + 52.0f, pw, 1.0f, COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);

    font_draw_scaled("Avatar", px + 22, py + 16, 22, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
    draw_rect(px + pw - 44, py + 12, 28, 28, COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);
    font_draw_scaled("X", px + pw - 35, py + 16, 16, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);

    if (!ed->session_token[0]) {
        font_draw_scaled(ed->error[0] ? ed->error : "Log in to edit your avatar.",
                         px + 24, py + 80, 16, COL_ERR_R, COL_ERR_G, COL_ERR_B, 1, screen_w, screen_h);
        return;
    }

    float prev_x = px + 28;
    float prev_y = py + 72;
    float prev_w = 248.0f;
    float prev_h = 340.0f;
    ed->preview_x = prev_x;
    ed->preview_y = prev_y;
    ed->preview_w = prev_w;
    ed->preview_h = prev_h;

    if (ed->loading) {
        ed->skeleton_t += (dt > 0.0f && dt < 0.1f) ? dt : 0.016f;
        float pulse = 0.55f + 0.12f * (0.5f + 0.5f * sinf(ed->skeleton_t * 4.0f));
        float br = 0.88f * pulse, bg = 0.88f * pulse, bb = 0.88f * pulse;
        float lr = 0.78f * pulse, lg = 0.78f * pulse, lb = 0.78f * pulse;

        draw_round_rect(prev_x, prev_y, prev_w, prev_h, 12.0f, br, bg, bb, 1, screen_w, screen_h);
        draw_round_rect(prev_x + 40, prev_y + 48, prev_w - 80, prev_h - 100, 10.0f,
                        lr, lg, lb, 1, screen_w, screen_h);
        draw_round_rect(prev_x, prev_y + prev_h + 10, 140, 12, 4.0f, lr, lg, lb, 1, screen_w, screen_h);
        draw_round_rect(prev_x, prev_y + prev_h + 36, 40, 14, 4.0f, lr, lg, lb, 1, screen_w, screen_h);
        for (int i = 0; i < 10; i++) {
            float cx = prev_x + (i % 5) * 36.0f;
            float cy = prev_y + prev_h + 56.0f + (i / 5) * 34.0f;
            draw_round_rect(cx, cy, 28, 28, 8.0f, br, bg, bb, 1, screen_w, screen_h);
        }

        float grid_x = prev_x + prev_w + 28;
        float grid_y = prev_y;
        float grid_w = px + pw - 28 - grid_x;
        float tab_x = grid_x;
        for (int t = 0; t < 4; t++) {
            draw_round_rect(tab_x, grid_y, 72, 30, 8.0f, br, bg, bb, 1, screen_w, screen_h);
            tab_x += 80;
        }
        float cell = 72.0f;
        float gap = 10.0f;
        int cols = (int)((grid_w + gap) / (cell + gap));
        if (cols < 1) cols = 1;
        float cells_y = grid_y + 42.0f;
        int skel_n = cols * 3;
        if (skel_n > 12) skel_n = 12;
        for (int i = 0; i < skel_n; i++) {
            int col = i % cols;
            int row = i / cols;
            float cx = grid_x + (float)col * (cell + gap);
            float cy = cells_y + (float)row * (cell + gap);
            if (cy + cell > py + ph - 70) break;
            draw_round_rect(cx, cy, cell, cell, 10.0f, br, bg, bb, 1, screen_w, screen_h);
        }

        float btn_y = py + ph - 56;
        draw_round_rect(px + pw - 220, btn_y, 90, 38, 8.0f, br, bg, bb, 1, screen_w, screen_h);
        draw_round_rect(px + pw - 118, btn_y, 90, 38, 8.0f, br, bg, bb, 1, screen_w, screen_h);

        if (ed->load_deferred) {
            ed->load_deferred = false;
        } else if (ed->need_profile_fetch) {
            ed->need_profile_fetch = false;
            char sk[8]; int sh = ed->shirt, pa = ed->pants, he = ed->head, ac = ed->accessory;
            int api_accs[PW_MAX_EQUIPPED_ACCESSORIES] = {0};
            if (auth_avatar_get(ed->session_token, sk, sizeof(sk), &sh, &pa, &he, &ac, api_accs)) {
                strncpy(ed->skin_color, sk, sizeof(ed->skin_color) - 1);
                ed->shirt = sh; ed->pants = pa; ed->head = he;
                memcpy(ed->accessories, api_accs, sizeof(ed->accessories));
                editor_sync_accessory_mirror(ed);
                strncpy(ed->base_skin, ed->skin_color, sizeof(ed->base_skin) - 1);
                ed->base_shirt = sh; ed->base_pants = pa; ed->base_head = he;
                memcpy(ed->base_accessories, ed->accessories, sizeof(ed->base_accessories));
                ed->base_accessory = ed->accessory;
            }

        } else {
            load_inventory(ed);
            if (!ed->loading)
                sync_preview_appearance(ed);
        }
        return;
    }

    if (ed->thumbs_pending)
        avatar_editor_pump_thumbs(ed);

    draw_rect(prev_x - 2, prev_y - 2, prev_w + 4, prev_h + 4, COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);
    draw_rect(prev_x, prev_y, prev_w, prev_h, COL_PREV_R, COL_PREV_G, COL_PREV_B, 1, screen_w, screen_h);

    sync_preview_appearance(ed);
    draw_3d_preview(ed, renderer, screen_w, screen_h, dt);

    glViewport(0, 0, screen_w, screen_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    ensure_shaders();

    font_draw_scaled("Drag right * scroll zoom", prev_x, prev_y + prev_h + 10, 12,
                     COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
    {
        const char* spin_lab = ed->auto_spin ? "Stop spin" : "Spin";
        float spin_w = font_text_width_scaled(spin_lab, 13) + 20.0f;
        float spin_x = prev_x + prev_w - spin_w;
        float spin_y = prev_y + prev_h + 6.0f;
        draw_rect(spin_x, spin_y, spin_w, 22, COL_BTN_R, COL_BTN_G, COL_BTN_B, 1, screen_w, screen_h);
        font_draw_scaled(spin_lab, spin_x + 10, spin_y + 4, 13,
                         COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
    }
    font_draw_scaled("Skin", prev_x, prev_y + prev_h + 36, 14,
                     COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
    for (int i = 0; i < SKIN_PRESET_COUNT; i++) {
        float cx = prev_x + (i % 5) * 36.0f;
        float cy = prev_y + prev_h + 56.0f + (i / 5) * 34.0f;
        float cr, cg, cb;
        parse_hex_color(skin_presets[i], &cr, &cg, &cb);
        bool sel = (strcmp(ed->skin_color, skin_presets[i]) == 0);
        if (sel) draw_rect(cx - 3, cy - 3, 34, 34, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
        draw_rect(cx, cy, 28, 28, cr, cg, cb, 1, screen_w, screen_h);
    }

    float grid_x = prev_x + prev_w + 28;
    float grid_y = prev_y;
    float grid_w = px + pw - 28 - grid_x;
    const char* tabs[] = { "Shirts", "Pants", "Heads", "Hats" };
    float tab_x = grid_x;
    for (int t = 0; t < 4; t++) {
        float tw = font_text_width_scaled(tabs[t], 14) + 24;
        bool on = (ed->tab == t);
        if (on) {
            draw_rect(tab_x, grid_y, tw, 30, COL_TAB_ON_R, COL_TAB_ON_G, COL_TAB_ON_B, 1, screen_w, screen_h);
            draw_rect(tab_x, grid_y + 28, tw, 2, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
            font_draw_scaled(tabs[t], tab_x + 12, grid_y + 7, 14, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        } else {
            draw_rect(tab_x, grid_y, tw, 30, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
            font_draw_scaled(tabs[t], tab_x + 12, grid_y + 7, 14,
                             COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        }
        tab_x += tw + 8;
    }

    float cell = 72.0f;
    float gap = 10.0f;
    int cols = (int)((grid_w + gap) / (cell + gap));
    if (cols < 1) cols = 1;
    float list_y = grid_y + 44;
    float list_h = ph - (list_y - py) - 78;

    int drawn = 0;
    if (ed->tab == 3) {
        bool none_sel = (editor_acc_worn_count(ed->accessories) == 0);
        float cx = grid_x;
        float cy = list_y;
        if (none_sel) draw_rect(cx - 3, cy - 3, cell + 6, cell + 6, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
        draw_rect(cx, cy, cell, cell, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
        font_draw_scaled("None", cx + 16, cy + 28, 14, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        drawn = 1;
        char acc_count[32];
        snprintf(acc_count, sizeof(acc_count), "Worn %d/%d",
                 editor_acc_worn_count(ed->accessories), PW_MAX_EQUIPPED_ACCESSORIES);
        font_draw_scaled(acc_count, grid_x, grid_y + 36, 13,
                         COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
    }

    const char* want = tab_type(ed->tab);
    int* cur = slot_ptr(ed, ed->tab);
    for (int i = 0; i < ed->item_count; i++) {
        if (strcmp(ed->items[i].type, want) != 0) continue;
        int slot = drawn++;
        int col = slot % cols;
        int row = slot / cols;
        float cx = grid_x + col * (cell + gap);
        float cy = list_y + row * (cell + gap);
        if (cy + cell > list_y + list_h) break;
        bool sel = (ed->tab == 3)
            ? editor_acc_is_worn(ed->accessories, ed->items[i].id)
            : (cur && *cur == ed->items[i].id);
        if (sel) draw_rect(cx - 3, cy - 3, cell + 6, cell + 6, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
        draw_rect(cx, cy, cell, cell, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
        if (ed->items[i].thumb_loaded)
            draw_tex(ed->items[i].thumb_tex, cx + 6, cy + 6, cell - 12, cell - 12, screen_w, screen_h);
    }

    if (ed->error[0])
        font_draw_scaled(ed->error, px + 24, py + ph - 82, 14, COL_ERR_R, COL_ERR_G, COL_ERR_B, 1, screen_w, screen_h);
    else if (ed->status[0] && strcmp(ed->status, "SAVED_OK") != 0)
        font_draw_scaled(ed->status, px + 24, py + ph - 82, 14, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);

    float btn_y = py + ph - 56;
    float save_w = 128, cancel_w = 108;
    float save_x = px + pw - 28 - save_w;
    float cancel_x = save_x - 12 - cancel_w;
    draw_rect(cancel_x, btn_y, cancel_w, 38, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
    font_draw_scaled("Cancel", cancel_x + 26, btn_y + 11, 16, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
    draw_rect(save_x, btn_y, save_w, 38, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
    font_draw_scaled(ed->saving ? "Saving..." : "Save", save_x + 36, btn_y + 11, 16,
                     COL_ON_LIME_R, COL_ON_LIME_G, COL_ON_LIME_B, 1, screen_w, screen_h);

    glEnable(GL_DEPTH_TEST);
}

bool avatar_editor_on_mousedown(AvatarEditor* ed, float x, float y, int button) {
    if (!ed->open) return false;
    float px = ed->panel_x, py = ed->panel_y, pw = ed->panel_w, ph = ed->panel_h;
    if (pw < 1) return true;

    if (button == 2) {
        if (hit(x, y, ed->preview_x, ed->preview_y, ed->preview_w, ed->preview_h)) {
            ed->preview_dragging = true;
            ed->drag_lx = x;
            ed->drag_ly = y;
            ed->auto_spin = false;
        }
        return true;
    }
    if (button != 0) return true;

    if (!hit(x, y, px, py, pw, ph)) {
        avatar_editor_close(ed);
        return true;
    }

    if (hit(x, y, px + pw - 44, py + 12, 28, 28)) {
        avatar_editor_close(ed);
        return true;
    }

    if (!ed->session_token[0]) return true;

    float prev_x = ed->preview_x;
    float prev_y = ed->preview_y;
    float prev_w = ed->preview_w;
    float prev_h = ed->preview_h;

    {
        const char* spin_lab = ed->auto_spin ? "Stop spin" : "Spin";
        float spin_w = font_text_width_scaled(spin_lab, 13) + 20.0f;
        float spin_x = prev_x + prev_w - spin_w;
        float spin_y = prev_y + prev_h + 6.0f;
        if (hit(x, y, spin_x, spin_y, spin_w, 22)) {
            ed->auto_spin = !ed->auto_spin;
            return true;
        }
    }

    if (hit(x, y, prev_x, prev_y, prev_w, prev_h))
        return true;

    for (int i = 0; i < SKIN_PRESET_COUNT; i++) {
        float cx = prev_x + (i % 5) * 36.0f;
        float cy = prev_y + prev_h + 56.0f + (i / 5) * 34.0f;
        if (hit(x, y, cx, cy, 28, 28)) {
            strncpy(ed->skin_color, skin_presets[i], sizeof(ed->skin_color) - 1);
            ed->dirty = true;
            return true;
        }
    }

    float grid_x = prev_x + prev_w + 28;
    float grid_y = prev_y;
    float grid_w = px + pw - 28 - grid_x;
    const char* tabs[] = { "Shirts", "Pants", "Heads", "Hats" };
    float tab_x = grid_x;
    for (int t = 0; t < 4; t++) {
        float tw = font_text_width_scaled(tabs[t], 14) + 24;
        if (hit(x, y, tab_x, grid_y, tw, 30)) {
            ed->tab = t;
            return true;
        }
        tab_x += tw + 8;
    }

    float cell = 72.0f;
    float gap = 10.0f;
    int cols = (int)((grid_w + gap) / (cell + gap));
    if (cols < 1) cols = 1;
    float list_y = grid_y + 44;
    float list_h = ph - (list_y - py) - 78;

    if (ed->tab == 3 && hit(x, y, grid_x, list_y, cell, cell)) {
        editor_acc_clear(ed->accessories);
        editor_sync_accessory_mirror(ed);
        ed->dirty = true;
        sync_preview_appearance(ed);
        return true;
    }

    const char* want = tab_type(ed->tab);
    int* cur = slot_ptr(ed, ed->tab);
    int match_i = 0;
    for (int i = 0; i < ed->item_count; i++) {
        if (strcmp(ed->items[i].type, want) != 0) continue;
        int slot = match_i + (ed->tab == 3 ? 1 : 0);
        match_i++;
        int col = slot % cols;
        int row = slot / cols;
        float cx = grid_x + col * (cell + gap);
        float cy = list_y + row * (cell + gap);
        if (cy + cell > list_y + list_h) break;
        if (hit(x, y, cx, cy, cell, cell)) {
            if (ed->tab == 3) {
                editor_acc_toggle(ed->accessories, ed->items[i].id);
                editor_sync_accessory_mirror(ed);
            } else if (cur) {
                *cur = ed->items[i].id;
            }
            ed->dirty = true;
            sync_preview_appearance(ed);
            return true;
        }
    }

    float btn_y = py + ph - 56;
    float save_w = 128, cancel_w = 108;
    float save_x = px + pw - 28 - save_w;
    float cancel_x = save_x - 12 - cancel_w;
    if (hit(x, y, cancel_x, btn_y, cancel_w, 38)) {
        strncpy(ed->skin_color, ed->base_skin, sizeof(ed->skin_color) - 1);
        ed->shirt = ed->base_shirt;
        ed->pants = ed->base_pants;
        ed->head = ed->base_head;
        memcpy(ed->accessories, ed->base_accessories, sizeof(ed->accessories));
        ed->accessory = ed->base_accessory;
        avatar_editor_close(ed);
        return true;
    }
    if (hit(x, y, save_x, btn_y, save_w, 38) && !ed->saving) {
        ed->saving = true;
        ed->error[0] = '\0';
        if (auth_avatar_save(ed->session_token, ed->skin_color,
                             ed->shirt, ed->pants, ed->head, ed->accessories,
                             &ed->package)) {
            strncpy(ed->base_skin, ed->skin_color, sizeof(ed->base_skin) - 1);
            ed->base_shirt = ed->shirt;
            ed->base_pants = ed->pants;
            ed->base_head = ed->head;
            memcpy(ed->base_accessories, ed->accessories, sizeof(ed->base_accessories));
            ed->base_accessory = ed->accessory;
            ed->dirty = false;
            snprintf(ed->status, sizeof(ed->status), "SAVED_OK");
            ed->saving = false;
        } else {
            snprintf(ed->error, sizeof(ed->error), "Save failed.");
            ed->saving = false;
        }
        return true;
    }
    return true;
}

void avatar_editor_on_mouseup(AvatarEditor* ed, int button) {
    if (!ed) return;
    if (button == 2) ed->preview_dragging = false;
}

bool avatar_editor_on_scroll(AvatarEditor* ed, float x, float y, float delta) {
    if (!ed || !ed->open) return false;

    if (!hit(x, y, ed->panel_x, ed->panel_y, ed->panel_w, ed->panel_h) &&
        !hit(x, y, ed->preview_x, ed->preview_y, ed->preview_w, ed->preview_h))
        return false;
    if (ed->cam_dist < 0.5f) ed->cam_dist = 6.0f;
    ed->cam_dist += delta * 0.55f;
    if (ed->cam_dist < 2.5f) ed->cam_dist = 2.5f;
    if (ed->cam_dist > 12.0f) ed->cam_dist = 12.0f;
    return true;
}

bool avatar_editor_on_key(AvatarEditor* ed, int keycode) {
    if (!ed || !ed->open) return false;
    if (keycode == 27) {
        avatar_editor_close(ed);
        return true;
    }
    return true;
}

bool avatar_editor_consume_saved(AvatarEditor* ed) {
    if (!ed || strcmp(ed->status, "SAVED_OK") != 0) return false;
    ed->status[0] = '\0';
    return true;
}

const char* avatar_editor_skin(const AvatarEditor* ed) { return ed->skin_color; }
int avatar_editor_shirt(const AvatarEditor* ed) { return ed->shirt; }
int avatar_editor_pants(const AvatarEditor* ed) { return ed->pants; }
int avatar_editor_head(const AvatarEditor* ed) { return ed->head; }
int avatar_editor_accessory(const AvatarEditor* ed) { return ed ? ed->accessory : 0; }
const int* avatar_editor_accessories(const AvatarEditor* ed) {
    return ed ? ed->accessories : NULL;
}
int avatar_editor_package(const AvatarEditor* ed) { return ed ? ed->package : 0; }

#endif
