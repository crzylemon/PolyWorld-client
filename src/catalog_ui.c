/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: catalog_ui.c                                                                        |
|   Purpose: in-game catalog overlay                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__

#include "catalog_ui.h"
#include "avatar_editor.h"
#include "font.h"
#include "input.h"
#include "log.h"
#include "shader.h"
#include "platform.h"
#include "prod_urls.h"
#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern void stbi_image_free(void*);
extern void stbi_set_flip_vertically_on_load(int);

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
#define COL_ON_LIME_R (ui_theme_col()->on_lime[0])
#define COL_ON_LIME_G (ui_theme_col()->on_lime[1])
#define COL_ON_LIME_B (ui_theme_col()->on_lime[2])

static unsigned int s_color_prog = 0;
static unsigned int s_tex_prog = 0;
static unsigned int s_round_prog = 0;
static unsigned g_cat_thumb_gen = 0;
static int g_cat_thumbs_inflight = 0;
#define CAT_THUMB_MAX_INFLIGHT 4

static const char* TAB_TYPES[] = { "all", "shirt", "pants", "head", "accessory" };
static const char* TAB_LABELS[] = { "All", "Shirts", "Pants", "Heads", "Hats" };

static const char* catalog_sort_label(int sort) {
    switch (sort) {
        case 1: return "Newest";
        case 2: return "Popular";
        default: return "Recommended";
    }
}

static const char* catalog_sort_qs(int sort) {
    switch (sort) {
        case 1: return "newest";
        case 2: return "popular";
        default: return "recommended";
    }
}

typedef struct {
    CatalogUi* ui;
    int index;
    unsigned gen;
} CatThumbCtx;

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
    if (w < 0.5f || h < 0.5f) return;
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static unsigned int tex_from_png_mem(const uint8_t* buf, size_t len) {
    if (!buf || len < 16) return 0;
    int w, h, c;
    unsigned char* px = stbi_load_from_memory(buf, (int)len, &w, &h, &c, 4);
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

static void clear_thumbs(CatalogUi* ui) {
    g_cat_thumb_gen++;
    g_cat_thumbs_inflight = 0;
    for (int i = 0; i < ui->item_count; i++) {
        if (ui->items[i].thumb_tex) glDeleteTextures(1, &ui->items[i].thumb_tex);
        ui->items[i].thumb_tex = 0;
        ui->items[i].thumb_loaded = false;
        ui->items[i].thumb_loading = false;
    }
}

static bool hit(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

static bool catalog_compact(int sw, int sh) {
#if PW_MOBILE
    if (platform_is_chromebook() && sw >= 900 && sh >= 560) return false;
    return true;
#else
    return sw < 760 || sh < 500;
#endif
}

static void json_copy_str(const char* obj, const char* key, char* out, int out_n) {
    out[0] = '\0';
    const char* k = strstr(obj, key);
    if (!k) return;
    const char* c = strchr(k + (int)strlen(key), ':');
    if (!c) return;
    c++;
    while (*c == ' ') c++;
    if (*c != '"') return;
    c++;
    int n = 0;
    while (*c && n < out_n - 1) {
        if (*c == '"') break;
        if (*c == '\\' && c[1]) {
            c++;
            if (*c == 'n') out[n++] = '\n';
            else if (*c == 't') out[n++] = ' ';
            else if (*c == '/' ) { out[n++] = '/'; }
            else out[n++] = *c;
            c++;
            continue;
        }
        out[n++] = *c++;
    }
    out[n] = '\0';
}

static int json_int(const char* obj, const char* key, int def) {
    const char* k = strstr(obj, key);
    if (!k) return def;
    const char* c = strchr(k + (int)strlen(key), ':');
    if (!c) return def;
    return atoi(c + 1);
}

static bool json_bool(const char* obj, const char* key) {
    const char* k = strstr(obj, key);
    if (!k) return false;
    const char* c = strchr(k + (int)strlen(key), ':');
    if (!c) return false;
    c++;
    while (*c == ' ') c++;
    return c[0] == 't' || c[0] == '1';
}

static int parse_items(CatalogUi* ui, const char* json, bool append) {
    if (!append) {
        clear_thumbs(ui);
        ui->item_count = 0;
    }
    const char* arr = strstr(json, "\"items\"");
    if (!arr) return 0;
    const char* cur = strchr(arr, '[');
    if (!cur) return 0;
    int added = 0;
    while (ui->item_count < CATALOG_UI_MAX_ITEMS) {
        const char* id_key = strstr(cur, "\"id\"");
        if (!id_key) break;
        const char* obj_end = strchr(id_key, '}');
        if (!obj_end) break;
        char obj[2048];
        int olen = (int)(obj_end - id_key) + 1;
        if (olen > 2040) olen = 2040;
        memcpy(obj, id_key, (size_t)olen);
        obj[olen] = '\0';

        CatalogUiItem* it = &ui->items[ui->item_count];
        memset(it, 0, sizeof(*it));
        it->id = json_int(obj, "\"id\"", 0);
        json_copy_str(obj, "\"name\"", it->name, (int)sizeof(it->name));
        json_copy_str(obj, "\"type\"", it->type, (int)sizeof(it->type));
        json_copy_str(obj, "\"creator\"", it->creator, (int)sizeof(it->creator));
        json_copy_str(obj, "\"image_path\"", it->image_path, (int)sizeof(it->image_path));
        json_copy_str(obj, "\"mesh_style\"", it->mesh_style, (int)sizeof(it->mesh_style));
        json_copy_str(obj, "\"description\"", it->description, (int)sizeof(it->description));
        if (!it->mesh_style[0]) strncpy(it->mesh_style, "legacy", sizeof(it->mesh_style) - 1);
        it->price = json_int(obj, "\"price\"", -1);
        it->stock = json_int(obj, "\"stock\"", 0);
        it->resale = json_int(obj, "\"resale\"", 0);
        it->owned = json_bool(obj, "\"owned\"");
        it->limited = json_bool(obj, "\"limited\"");
        it->offsale = json_bool(obj, "\"offsale\"") || it->price < 0;
        it->donate = json_bool(obj, "\"donate\"");
        if (it->id > 0) {
            ui->item_count++;
            added++;
        }
        cur = obj_end + 1;
    }
    return added;
}

static void catalog_fetch(CatalogUi* ui, bool append) {
    if (!ui->session_token[0]) {
        snprintf(ui->error, sizeof(ui->error), "Log in to browse the catalog.");
        ui->loading = false;
        return;
    }
    if (!append) {
        ui->page = 1;
        ui->scroll_y = 0;
    }
    char url[768];
    snprintf(url, sizeof(url),
             "%s?action=list&session_token=%s&type=%s&page=%d&sort=%s",
             PW_CATALOG_API_URL, ui->session_token, TAB_TYPES[ui->tab], ui->page,
             catalog_sort_qs(ui->sort));
    size_t len = 0;
    char* resp = (char*)platform_http_get(url, &len);
    if (!resp) {
        snprintf(ui->error, sizeof(ui->error), "Could not load catalog.");
        ui->loading = false;
        return;
    }
    ui->bricks = json_int(resp, "\"bricks\"", ui->bricks);
    ui->has_more = json_bool(resp, "\"has_more\"");
    if (!strstr(resp, "\"ok\":true") && !strstr(resp, "\"ok\": true")) {
        json_copy_str(resp, "\"error\"", ui->error, (int)sizeof(ui->error));
        if (!ui->error[0]) snprintf(ui->error, sizeof(ui->error), "Could not load catalog.");
        free(resp);
        ui->loading = false;
        return;
    }
    parse_items(ui, resp, append);
    ui->thumbs_pending = (ui->item_count > 0);
    ui->error[0] = '\0';
    ui->loading = false;
    free(resp);
}

static void cat_thumb_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    CatThumbCtx* ctx = (CatThumbCtx*)user;
    if (!ctx) return;
    CatalogUi* ui = ctx->ui;
    int index = ctx->index;
    unsigned gen = ctx->gen;
    free(ctx);
    if (g_cat_thumbs_inflight > 0) g_cat_thumbs_inflight--;
    (void)path;
    if (!ui || gen != g_cat_thumb_gen) return;
    if (index < 0 || index >= ui->item_count) return;
    ui->items[index].thumb_loading = false;
    ui->items[index].thumb_loaded = true;
    unsigned int tex = tex_from_png_mem(data, len);
    if (tex) ui->items[index].thumb_tex = tex;
}

static void catalog_pump_thumbs(CatalogUi* ui) {
    if (!ui || !ui->open || !ui->thumbs_pending) return;
    bool any = false;
    for (int i = 0; i < ui->item_count; i++) {
        if (ui->items[i].thumb_loaded) continue;
        any = true;
        if (ui->items[i].thumb_loading) continue;
        if (g_cat_thumbs_inflight >= CAT_THUMB_MAX_INFLIGHT) return;
        char url[384];
        if (ui->items[i].image_path[0] == 'h') {
            snprintf(url, sizeof(url), "%s", ui->items[i].image_path);
        } else if (ui->items[i].image_path[0] == '/') {
            snprintf(url, sizeof(url), "%s%s", ui->host, ui->items[i].image_path);
        } else if (ui->items[i].image_path[0]) {
            snprintf(url, sizeof(url), "%s/%s", ui->host, ui->items[i].image_path);
        } else {
            ui->items[i].thumb_loaded = true;
            continue;
        }
        CatThumbCtx* ctx = (CatThumbCtx*)malloc(sizeof(CatThumbCtx));
        if (!ctx) return;
        ctx->ui = ui;
        ctx->index = i;
        ctx->gen = g_cat_thumb_gen;
        ui->items[i].thumb_loading = true;
        g_cat_thumbs_inflight++;
        platform_load_file(url, cat_thumb_on_loaded, ctx);
    }
    if (!any) ui->thumbs_pending = false;
}

static void catalog_try_buy(CatalogUi* ui) {
    if (!ui || ui->buying || ui->view != 1) return;
    CatalogUiItem* it = &ui->detail;
    if (it->id <= 0 || it->owned || it->donate || (it->offsale && it->resale <= 0)) return;
    ui->buying = true;
    ui->status[0] = '\0';
    char body[320];
    snprintf(body, sizeof(body), "action=purchase&item_id=%d&session_token=%s",
             it->id, ui->session_token);
    size_t len = 0;
    char* resp = (char*)platform_http_post(PW_CATALOG_API_URL, body, &len);
    ui->buying = false;
    if (!resp) {
        snprintf(ui->error, sizeof(ui->error), "Purchase failed.");
        return;
    }
    if (strstr(resp, "\"ok\":true") || strstr(resp, "\"ok\": true")) {
        ui->bricks = json_int(resp, "\"bricks\"", ui->bricks);
        it->owned = true;
        for (int i = 0; i < ui->item_count; i++) {
            if (ui->items[i].id == it->id) ui->items[i].owned = true;
        }
        snprintf(ui->status, sizeof(ui->status), "Added to your inventory.");
        ui->error[0] = '\0';
    } else {
        json_copy_str(resp, "\"error\"", ui->error, (int)sizeof(ui->error));
        if (!ui->error[0]) snprintf(ui->error, sizeof(ui->error), "Purchase failed.");
    }
    free(resp);
}

void catalog_ui_init(CatalogUi* ui) {
    (void)ui;
    font_init();
}

void catalog_ui_shutdown(CatalogUi* ui) {
    if (!ui) return;
    clear_thumbs(ui);
    ui->open = false;
}

void catalog_ui_open(CatalogUi* ui, const char* session_token, const char* host,
                     const char* skin_color, int shirt, int pants, int head,
                     const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                     int package) {
    if (!ui) return;
    catalog_ui_shutdown(ui);
    memset(ui, 0, sizeof(*ui));
    ui->selected = -1;
    ui->open = true;
    ui->loading = true;
    ui->load_deferred = true;
    ui->view = 0;
    ui->tab = 0;
    ui->sort = 0;
    ui->page = 1;
    ui->press_armed = false;
    ui->press_outside = false;
    ui->cam_yaw = 200.0f;
    ui->cam_pitch = 10.0f;
    ui->cam_dist = 6.0f;
    ui->cam_target = (Vec3){0.0f, 2.5f, 0.0f};
    ui->auto_spin = false;
    ui->base_shirt = shirt;
    ui->base_pants = pants;
    ui->base_head = head;
    ui->base_package = package;
    if (accessories) memcpy(ui->base_accs, accessories, sizeof(ui->base_accs));
    strncpy(ui->skin_color, (skin_color && skin_color[0]) ? skin_color : "#eaeaea",
            sizeof(ui->skin_color) - 1);
    if (session_token) strncpy(ui->session_token, session_token, sizeof(ui->session_token) - 1);
    if (host && host[0]) strncpy(ui->host, host, sizeof(ui->host) - 1);
    else strncpy(ui->host, PW_SITE_ORIGIN, sizeof(ui->host) - 1);
}

void catalog_ui_close(CatalogUi* ui) {
    if (!ui) return;
    clear_thumbs(ui);
    ui->open = false;
    platform_set_cursor_grab(0);
}

bool catalog_ui_blocks_input(const CatalogUi* ui) {
    return ui && ui->open;
}

static float catalog_ui_scale(int sw, int sh) {
    float s = fminf((float)sw / 1100.0f, (float)sh / 700.0f);
    if (s < 1.0f) s = 1.0f;
    if (s > 2.25f) s = 2.25f;
    return s;
}

static void layout_panel(CatalogUi* ui, int sw, int sh) {
    ui->compact = catalog_compact(sw, sh);
    float s = catalog_ui_scale(sw, sh);
    ui->ui_s = s;
    float pw = ui->compact ? (float)sw : fminf(880.0f * s, (float)sw - 48.0f);
    float ph = ui->compact ? (float)sh : fminf(640.0f * s, (float)sh - 48.0f);
    ui->panel_w = pw;
    ui->panel_h = ph;
    ui->panel_x = ((float)sw - pw) * 0.5f;
    ui->panel_y = ((float)sh - ph) * 0.5f;
    float header_h = (ui->compact ? 56.0f : 52.0f) * s;
    ui->header_h = header_h;
    ui->tab_row_h = (ui->compact ? 44.0f : 36.0f) * s;
    float tabs_y = ui->panel_y + header_h + 8.0f * s;
    float tx = ui->panel_x + 16.0f * s;
    float tw_avail = pw - 32.0f * s;
    float gap = 8.0f * s;
    float tab_fs = (ui->compact ? 16.0f : 14.0f) * s;
    for (int t = 0; t < 5; t++) {
        float tw = font_text_width_scaled(TAB_LABELS[t], tab_fs) + (ui->compact ? 28.0f : 22.0f) * s;
        if (tx + tw > ui->panel_x + 16.0f * s + tw_avail && t > 0) {
            tabs_y += ui->tab_row_h + 6.0f * s;
            tx = ui->panel_x + 16.0f * s;
        }
        ui->tab_x[t] = tx;
        ui->tab_y[t] = tabs_y;
        ui->tab_w[t] = tw;
        tx += tw + gap;
    }
    ui->grid_x = ui->panel_x + 16.0f * s;
    ui->grid_y = tabs_y + ui->tab_row_h + 10.0f * s;
    ui->grid_w = pw - 32.0f * s;
    float bottom = (ui->compact ? 16.0f : 20.0f) * s;
    ui->grid_h = ui->panel_y + ph - bottom - ui->grid_y;
    ui->cell = (ui->compact ? 96.0f : 86.0f) * s;
    ui->gap = (ui->compact ? 12.0f : 10.0f) * s;
    ui->label_h = (ui->compact ? 36.0f : 32.0f) * s;
    ui->cols = (int)((ui->grid_w + ui->gap) / (ui->cell + ui->gap));
    if (ui->cols < 2) ui->cols = 2;
    if (ui->cols > 8) ui->cols = 8;

    float btn_h = (ui->compact ? 48.0f : 40.0f) * s;
    ui->close_w = (ui->compact ? 44.0f : 28.0f) * s;
    ui->close_h = ui->close_w;
    ui->close_x = ui->panel_x + pw - 12.0f * s - ui->close_w;
    ui->close_y = ui->panel_y + (header_h - ui->close_h) * 0.5f;
    {
        float title_fs = 22.0f * s;
        float chip_fs = (ui->compact ? 14.0f : 13.0f) * s;
        const char* sl = catalog_sort_label(ui->sort);
        ui->sort_h = (ui->compact ? 32.0f : 26.0f) * s;
        ui->sort_w = font_text_width_scaled(sl, chip_fs) + 20.0f * s;
        if (ui->compact) {
            ui->sort_x = ui->panel_x + 16.0f * s;
        } else {
            float title_w = font_text_width_scaled("Catalog", title_fs);
            ui->sort_x = ui->panel_x + 22.0f * s + title_w + 12.0f * s;
        }
        ui->sort_y = ui->panel_y + (header_h - ui->sort_h) * 0.5f;
        float sort_right = ui->close_x - (ui->compact ? 100.0f : 12.0f) * s;
        if (ui->sort_x + ui->sort_w > sort_right)
            ui->sort_w = fmaxf(64.0f * s, sort_right - ui->sort_x);
    }
    ui->back_w = (ui->compact ? 72.0f : 64.0f) * s;
    ui->back_h = (ui->compact ? 36.0f : 28.0f) * s;
    ui->back_x = ui->panel_x + 12.0f * s;
    ui->back_y = ui->panel_y + (header_h - ui->back_h) * 0.5f;
    ui->buy_w = ui->compact ? (pw - 32.0f * s) : 160.0f * s;
    ui->buy_h = btn_h;
    ui->buy_x = ui->compact ? ui->panel_x + 16.0f * s : ui->panel_x + pw - 16.0f * s - ui->buy_w;
    ui->buy_y = ui->panel_y + ph - 16.0f * s - ui->buy_h;

    float prev_h = ui->compact
        ? fminf(260.0f * s, ph * 0.40f)
        : fminf(360.0f * s, ph - header_h - ui->buy_h - 64.0f * s);
    if (prev_h < 120.0f * s) prev_h = 120.0f * s;
    float prev_w = ui->compact ? fminf(pw - 32.0f * s, prev_h * 0.88f)
                               : fminf(prev_h * 0.82f, pw * 0.42f);
    ui->preview_w = prev_w;
    ui->preview_h = prev_h;
    ui->preview_x = ui->compact ? ui->panel_x + (pw - prev_w) * 0.5f : ui->panel_x + 24.0f * s;
    ui->preview_y = ui->panel_y + header_h + 16.0f * s;
}

static float row_stride(const CatalogUi* ui) {
    float label = ui->label_h > 1.0f ? ui->label_h : 32.0f;
    return ui->cell + ui->gap + label;
}

static void scissor_ui_rect(float x, float y, float w, float h, int sw, int sh) {
    int sx = (int)floorf(x);
    int sy_top = (int)floorf(y);
    int swid = (int)ceilf(w);
    int shgt = (int)ceilf(h);
    if (sx < 0) { swid += sx; sx = 0; }
    if (sy_top < 0) { shgt += sy_top; sy_top = 0; }
    if (sx + swid > sw) swid = sw - sx;
    if (sy_top + shgt > sh) shgt = sh - sy_top;
    if (swid < 1 || shgt < 1) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sh - (sy_top + shgt), swid, shgt);
}

static void ellipsize(char* out, int out_n, const char* src, float max_w, float size) {
    if (!out || out_n < 2) return;
    out[0] = '\0';
    if (!src || !src[0]) return;
    if (font_text_width_scaled(src, size) <= max_w) {
        strncpy(out, src, (size_t)out_n - 1);
        out[out_n - 1] = '\0';
        return;
    }
    char tmp[64];
    int n = (int)strlen(src);
    if (n > 60) n = 60;
    memcpy(tmp, src, (size_t)n);
    tmp[n] = '\0';
    while (n > 1) {
        tmp[n] = '\0';
        char with[68];
        snprintf(with, sizeof(with), "%s...", tmp);
        if (font_text_width_scaled(with, size) <= max_w) {
            strncpy(out, with, (size_t)out_n - 1);
            out[out_n - 1] = '\0';
            return;
        }
        n--;
    }
    strncpy(out, "...", (size_t)out_n - 1);
    out[out_n - 1] = '\0';
}

static float draw_wrapped_text(const char* text, float x, float y, float max_w, float max_h,
                               float size, float lh,
                               float r, float g, float b, float a, int sw, int sh) {
    if (!text || !text[0] || max_w < 8.0f || max_h < size * 0.4f) return 0.0f;
    float y0 = y;
    const char* p = text;
    while (*p && y + size * 0.5f <= y0 + max_h) {
        while (*p == ' ') p++;
        if (*p == '\n') { p++; y += lh; continue; }
        if (!*p) break;
        const char* start = p;
        char line[160];
        int n = 0;
        int last_space = -1;
        while (*p && *p != '\n' && n < 158) {
            if (*p == ' ') last_space = n;
            line[n++] = *p++;
            line[n] = '\0';
            if (font_text_width_scaled(line, size) > max_w) {
                if (last_space > 0) {
                    line[last_space] = '\0';
                    p = start + last_space + 1;
                } else if (n > 1) {
                    n--;
                    line[n] = '\0';
                    p = start + n;
                } else {
                    p = start + n;
                }
                break;
            }
        }
        if (n > 0)
            font_draw_scaled(line, x, y, size, r, g, b, a, sw, sh);
        y += lh;
        if (*p == '\n') p++;
    }
    return y - y0;
}

static void price_label(const CatalogUiItem* it, char* buf, int n) {
    if (it->owned) snprintf(buf, (size_t)n, "Owned");
    else if (it->donate) snprintf(buf, (size_t)n, "Donate");
    else if (it->offsale && it->resale > 0) snprintf(buf, (size_t)n, "From %d", it->resale);
    else if (it->offsale) snprintf(buf, (size_t)n, "Offsale");
    else if (it->price == 0) snprintf(buf, (size_t)n, "Free");
    else snprintf(buf, (size_t)n, "%d Bricks", it->price);
}

static void catalog_fill_preview(const CatalogUi* ui, int* shirt, int* pants, int* head,
                                 int accs[PW_MAX_EQUIPPED_ACCESSORIES], int* flags) {
    *shirt = 0;
    *pants = 0;
    *head = 0;
    memset(accs, 0, sizeof(int) * PW_MAX_EQUIPPED_ACCESSORIES);
    *flags = 0;
    if (!ui || ui->view != 1) return;
    const CatalogUiItem* it = &ui->detail;
    bool neu = (strcmp(it->mesh_style, "new") == 0);
    if (strcmp(it->type, "shirt") == 0 || strcmp(it->type, "tshirt") == 0) {
        *shirt = it->id;
        if (neu) *flags |= 1;
    } else if (strcmp(it->type, "pants") == 0) {
        *pants = it->id;
        if (neu) *flags |= 2;
    } else if (strcmp(it->type, "head") == 0) {
        *head = it->id;
        if (neu) *flags |= 4;
    } else if (strcmp(it->type, "accessory") == 0) {
        accs[0] = it->id;
    }
}

static void catalog_sph_from_pos(float px, float py, float pz,
                                 float tx, float ty, float tz,
                                 float* yaw, float* pitch, float* dist) {
    float dx = px - tx, dy = py - ty, dz = pz - tz;
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < 0.05f) d = 1.0f;
    *dist = d;
    *yaw = atan2f(dx, dz) * (180.0f / (float)M_PI);
    float as = dy / d;
    if (as > 1.0f) as = 1.0f;
    if (as < -1.0f) as = -1.0f;
    *pitch = asinf(as) * (180.0f / (float)M_PI);
}

static float catalog_norm_yaw(float y) {
    while (y <= -180.0f) y += 360.0f;
    while (y > 180.0f) y -= 360.0f;
    return y;
}

static float catalog_yaw_ang_dist(float a, float b) {
    float d = fabsf(catalog_norm_yaw(a - b));
    return d;
}

static float catalog_yaw_delta_left(float from, float to) {
    float inc = fmodf(to - from, 360.0f);
    if (inc < 0.0f) inc += 360.0f;
    if (inc < 0.5f || inc > 359.5f) return -180.0f;
    return inc - 360.0f;
}

static float catalog_ease_in_out(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) * 0.5f;
}

static void catalog_cam_from_pos(CatalogUi* ui, float px, float py, float pz,
                                 float tx, float ty, float tz) {
    catalog_sph_from_pos(px, py, pz, tx, ty, tz,
                         &ui->front_yaw, &ui->front_pitch, &ui->front_dist);
    catalog_sph_from_pos(-px, py, -pz, tx, ty, tz,
                         &ui->back_yaw, &ui->back_pitch, &ui->back_dist);
    ui->cam_yaw = ui->front_yaw;
    ui->cam_pitch = ui->front_pitch;
    ui->cam_dist = ui->front_dist;
    ui->cam_target = (Vec3){tx, ty, tz};
    ui->facing_front = true;
    ui->flip_active = false;
    ui->auto_spin = false;
}

static void catalog_start_flip(CatalogUi* ui) {
    if (!ui || ui->flip_active) return;
    double now = platform_get_time();
    if (now - ui->last_flip_at < 0.55) return;
    ui->last_flip_at = now;

    ui->facing_front = catalog_yaw_ang_dist(ui->cam_yaw, ui->front_yaw)
                     <= catalog_yaw_ang_dist(ui->cam_yaw, ui->back_yaw);
    bool going_front = !ui->facing_front;
    float tyaw = going_front ? ui->front_yaw : ui->back_yaw;
    float tpitch = going_front ? ui->front_pitch : ui->back_pitch;
    float tdist = going_front ? ui->front_dist : ui->back_dist;
    float delta = catalog_yaw_delta_left(ui->cam_yaw, tyaw);
    if (delta < -270.0f) delta = -180.0f;
    ui->flip_from_yaw = ui->cam_yaw;
    ui->flip_to_yaw = ui->cam_yaw + delta;
    ui->flip_from_pitch = ui->cam_pitch;
    ui->flip_to_pitch = tpitch;
    ui->flip_from_dist = ui->cam_dist;
    ui->flip_to_dist = tdist;
    ui->flip_t = 0.0f;
    ui->flip_dur = 0.55f;
    ui->flip_active = true;
    ui->facing_front = going_front;
    ui->auto_spin = false;
}

static void catalog_tick_flip(CatalogUi* ui, float dt) {
    if (!ui || !ui->flip_active) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    ui->flip_t += dt;
    float u = ui->flip_t / (ui->flip_dur > 0.01f ? ui->flip_dur : 0.55f);
    if (u >= 1.0f) u = 1.0f;
    float e = catalog_ease_in_out(u);
    ui->cam_yaw = ui->flip_from_yaw + (ui->flip_to_yaw - ui->flip_from_yaw) * e;
    ui->cam_pitch = ui->flip_from_pitch + (ui->flip_to_pitch - ui->flip_from_pitch) * e;
    ui->cam_dist = ui->flip_from_dist + (ui->flip_to_dist - ui->flip_from_dist) * e;
    if (u >= 1.0f) ui->flip_active = false;
}

static void catalog_apply_item_camera(CatalogUi* ui, const char* type) {
    if (!ui) return;
    if (type && (strcmp(type, "shirt") == 0 || strcmp(type, "tshirt") == 0))
        catalog_cam_from_pos(ui, 3.0f, 3.5f, 4.0f, 0.0f, 3.0f, 0.0f);
    else if (type && strcmp(type, "pants") == 0)
        catalog_cam_from_pos(ui, 3.0f, 1.5f, 4.0f, 0.0f, 1.0f, 0.0f);
    else if (type && strcmp(type, "head") == 0)
        catalog_cam_from_pos(ui, 3.0f, 4.5f, 0.0f, 0.0f, 4.5f, 0.0f);
    else
        catalog_cam_from_pos(ui, 5.0f, 2.5f, 6.0f, 0.0f, 2.5f, 0.0f);
}

void catalog_ui_render(CatalogUi* ui, Renderer* renderer, int screen_w, int screen_h, float dt) {
    if (!ui || !ui->open) {
        platform_set_cursor_grab(0);
        return;
    }
    font_init();
    ensure_shaders();
    glViewport(0, 0, screen_w, screen_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    layout_panel(ui, screen_w, screen_h);
    if (ui->preview_dragging) {
        const InputState* in = input_get_state();
        if (in) {
            float dx = in->mouse_x - ui->preview_down_x;
            float dy = in->mouse_y - ui->preview_down_y;
            float lim = 6.0f * (ui->ui_s > 0.1f ? ui->ui_s : 1.0f);
            if (dx * dx + dy * dy >= lim * lim) {
                ui->preview_orbited = true;
                ui->flip_active = false;
            }
        }
    }
    catalog_tick_flip(ui, dt);

    if (ui->drag_active && !ui->preview_dragging) {
        const InputState* in = input_get_state();
        if (in) {
            float ddy = ui->drag_last_y - in->mouse_y;
            if (fabsf(in->mouse_x - ui->drag_start_x) > 10.0f * ui->ui_s ||
                fabsf(in->mouse_y - ui->drag_start_y) > 10.0f * ui->ui_s)
                ui->drag_moved = true;
            if (ui->drag_moved && ui->view == 0) {
                ui->scroll_y += ddy;
                if (ui->scroll_y < 0) ui->scroll_y = 0;
                if (ui->scroll_y > ui->scroll_max) ui->scroll_y = ui->scroll_max;
            }
            ui->drag_last_y = in->mouse_y;
        }
    }

    draw_rect(0, 0, (float)screen_w, (float)screen_h, 0.10f, 0.12f, 0.08f, ui->compact ? 0.72f : 0.45f,
              screen_w, screen_h);

    float px = ui->panel_x, py = ui->panel_y, pw = ui->panel_w, ph = ui->panel_h;
    float s = ui->ui_s > 0.1f ? ui->ui_s : 1.0f;
    if (!ui->compact) {
        draw_rect(px + 8, py + 10, pw, ph, 0, 0, 0, 0.10f, screen_w, screen_h);
    }
    draw_rect(px, py, pw, ph, COL_PANEL_R, COL_PANEL_G, COL_PANEL_B, 1, screen_w, screen_h);
    draw_rect(px, py, 4.0f * s, ph, COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
    float header_h = ui->header_h;
    draw_rect(px, py, pw, header_h, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
    draw_rect(px, py + header_h, pw, 1.0f, COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);

    float title_fs = 22.0f * s;
    float body_fs = 14.0f * s;
    if (ui->view == 1) {
        draw_round_rect(ui->back_x, ui->back_y, ui->back_w, ui->back_h, 8.0f * s,
                        COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);
        font_draw_scaled("Back", ui->back_x + 14.0f * s, ui->back_y + (ui->back_h - (ui->compact ? 16 : 14) * s) * 0.5f,
                         (ui->compact ? 16 : 14) * s, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        font_draw_scaled(ui->detail.name[0] ? ui->detail.name : "Item",
                         px + ui->back_w + 24.0f * s, py + (header_h - title_fs) * 0.5f,
                         title_fs, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
    } else {
        if (!ui->compact) {
            font_draw_scaled("Catalog", px + 22.0f * s, py + (header_h - title_fs) * 0.5f,
                             title_fs, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        }
        float chip_fs = (ui->compact ? 14.0f : 13.0f) * s;
        draw_round_rect(ui->sort_x, ui->sort_y, ui->sort_w, ui->sort_h, 8.0f * s,
                        COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);
        const char* sl = catalog_sort_label(ui->sort);
        float slw = font_text_width_scaled(sl, chip_fs);
        font_draw_scaled(sl, ui->sort_x + (ui->sort_w - slw) * 0.5f,
                         ui->sort_y + (ui->sort_h - chip_fs) * 0.5f,
                         chip_fs, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        char bal[48];
        snprintf(bal, sizeof(bal), "%d Bricks", ui->bricks);
        float bw = font_text_width_scaled(bal, body_fs);
        font_draw_scaled(bal, ui->close_x - 12.0f * s - bw, py + (header_h - body_fs) * 0.5f,
                         body_fs, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
    }
    draw_round_rect(ui->close_x, ui->close_y, ui->close_w, ui->close_h, 6.0f * s,
                    COL_LINE_R, COL_LINE_G, COL_LINE_B, 1, screen_w, screen_h);
    font_draw_scaled("X", ui->close_x + (ui->close_w - 10.0f * s) * 0.5f,
                     ui->close_y + (ui->close_h - 16.0f * s) * 0.5f,
                     16.0f * s, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);

    if (ui->loading) {
        ui->skeleton_t += (dt > 0.0f && dt < 0.1f) ? dt : 0.016f;
        float pulse = 0.55f + 0.12f * (0.5f + 0.5f * sinf(ui->skeleton_t * 4.0f));
        scissor_ui_rect(ui->grid_x, ui->grid_y, ui->grid_w, ui->grid_h, screen_w, screen_h);
        for (int i = 0; i < 8; i++) {
            int col = i % ui->cols;
            int row = i / ui->cols;
            float cx = ui->grid_x + (float)col * (ui->cell + ui->gap);
            float cy = ui->grid_y + (float)row * row_stride(ui);
            draw_round_rect(cx, cy, ui->cell, ui->cell, 10.0f,
                            COL_SOFT_R * pulse, COL_SOFT_G * pulse, COL_SOFT_B * pulse,
                            1, screen_w, screen_h);
        }
        glDisable(GL_SCISSOR_TEST);
        if (ui->load_deferred) {
            ui->load_deferred = false;
        } else {
            catalog_fetch(ui, false);
        }
        glEnable(GL_DEPTH_TEST);
        return;
    }

    if (ui->thumbs_pending) catalog_pump_thumbs(ui);

    if (ui->view == 0) {
        float tab_fs = (ui->compact ? 16.0f : 14.0f) * s;
        for (int t = 0; t < 5; t++) {
            bool on = (ui->tab == t);
            float th = ui->tab_row_h - 6.0f * s;
            if (on) {
                draw_round_rect(ui->tab_x[t], ui->tab_y[t], ui->tab_w[t], th, 10.0f * s,
                                COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
                draw_rect(ui->tab_x[t], ui->tab_y[t] + th - 3.0f * s, ui->tab_w[t], 3.0f * s,
                          COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
                font_draw_scaled(TAB_LABELS[t], ui->tab_x[t] + 12.0f * s, ui->tab_y[t] + (ui->tab_row_h - 6.0f * s - tab_fs) * 0.5f,
                                 tab_fs, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
            } else {
                draw_round_rect(ui->tab_x[t], ui->tab_y[t], ui->tab_w[t], th, 10.0f * s,
                                COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 0.7f, screen_w, screen_h);
                font_draw_scaled(TAB_LABELS[t], ui->tab_x[t] + 12.0f * s, ui->tab_y[t] + (ui->tab_row_h - 6.0f * s - tab_fs) * 0.5f,
                                 tab_fs, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
            }
        }

        int rows = (ui->item_count + ui->cols - 1) / (ui->cols > 0 ? ui->cols : 1);
        float stride = row_stride(ui);
        float content_h = (float)rows * stride;
        ui->scroll_max = fmaxf(0.0f, content_h - ui->grid_h);
        if (ui->scroll_y > ui->scroll_max) ui->scroll_y = ui->scroll_max;

        if (ui->item_count == 0) {
            font_draw_scaled(ui->sort == 0 ? "No recommendations yet. Buy a few items or follow creators."
                                           : "No items in this tab yet.", ui->grid_x, ui->grid_y + 24,
                             16, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        }

        scissor_ui_rect(ui->grid_x, ui->grid_y, ui->grid_w, ui->grid_h, screen_w, screen_h);
        for (int i = 0; i < ui->item_count; i++) {
            int col = i % ui->cols;
            int row = i / ui->cols;
            float cx = ui->grid_x + (float)col * (ui->cell + ui->gap);
            float cy = ui->grid_y + (float)row * stride - ui->scroll_y;
            if (cy + ui->cell + ui->label_h < ui->grid_y) continue;
            if (cy > ui->grid_y + ui->grid_h) break;
            draw_round_rect(cx, cy, ui->cell, ui->cell, 10.0f * s,
                            COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
            if (ui->items[i].thumb_loaded)
                draw_tex(ui->items[i].thumb_tex, cx + 8.0f * s, cy + 8.0f * s,
                         ui->cell - 16.0f * s, ui->cell - 16.0f * s,
                         screen_w, screen_h);
            float name_size = (ui->compact ? 13.0f : 12.0f) * s;
            char shown[48];
            ellipsize(shown, (int)sizeof(shown), ui->items[i].name, ui->cell - 6.0f * s, name_size);
            font_draw_scaled(shown[0] ? shown : "Item", cx + 3.0f * s, cy + ui->cell + 3.0f * s, name_size,
                             COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
            char pl[40];
            price_label(&ui->items[i], pl, (int)sizeof(pl));
            font_draw_scaled(pl, cx + 3.0f * s, cy + ui->cell + 3.0f * s + name_size + 2.0f * s, 11.0f * s,
                             ui->items[i].owned ? COL_ACCENT_R : COL_MUTED_R,
                             ui->items[i].owned ? COL_ACCENT_G : COL_MUTED_G,
                             ui->items[i].owned ? COL_ACCENT_B : COL_MUTED_B,
                             1, screen_w, screen_h);
        }
        glDisable(GL_SCISSOR_TEST);

        if (ui->has_more && ui->item_count < CATALOG_UI_MAX_ITEMS &&
            ui->scroll_max - ui->scroll_y < 80.0f * s && !ui->load_more) {
            ui->load_more = true;
            ui->page++;
            catalog_fetch(ui, true);
            ui->load_more = false;
        }
        if (ui->error[0])
            font_draw_scaled(ui->error, ui->grid_x, py + ph - 28.0f * s, 14.0f * s,
                             COL_ERR_R, COL_ERR_G, COL_ERR_B, 1, screen_w, screen_h);
    } else {
        CatalogUiItem* it = &ui->detail;
        draw_round_rect(ui->preview_x, ui->preview_y, ui->preview_w, ui->preview_h, 12.0f * s,
                        COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, 1, screen_w, screen_h);
        int shirt, pants, head, flags;
        int accs[PW_MAX_EQUIPPED_ACCESSORIES];
        catalog_fill_preview(ui, &shirt, &pants, &head, accs, &flags);
        if (renderer) {
            avatar_preview_set_outfit(ui->host, ui->skin_color, shirt, pants, head, accs, flags);
            avatar_preview_draw(renderer, ui->preview_x, ui->preview_y, ui->preview_w, ui->preview_h,
                                screen_w, screen_h, dt,
                                &ui->cam_yaw, &ui->cam_pitch, &ui->cam_dist,
                                &ui->cam_target, 45.0f,
                                ui->auto_spin && !ui->preview_dragging && !ui->flip_active,
                                ui->preview_dragging && ui->preview_orbited,
                                &ui->preview_drag_lx, &ui->preview_drag_ly);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glViewport(0, 0, screen_w, screen_h);
        } else if (it->thumb_loaded) {
            draw_tex(it->thumb_tex, ui->preview_x + 12.0f * s, ui->preview_y + 12.0f * s,
                     ui->preview_w - 24.0f * s, ui->preview_h - 24.0f * s, screen_w, screen_h);
        }

        float tx = ui->compact ? px + 20.0f * s : ui->preview_x + ui->preview_w + 24.0f * s;
        float ty = ui->compact ? ui->preview_y + ui->preview_h + 14.0f * s : ui->preview_y;
        float name_fs = 22.0f * s;
        font_draw_scaled(it->name, tx, ty, name_fs,
                         COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        char by[64];
        snprintf(by, sizeof(by), "by %s", it->creator[0] ? it->creator : "Unknown");
        font_draw_scaled(by, tx, ty + 28.0f * s, 14.0f * s, COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        char pl[48];
        price_label(it, pl, (int)sizeof(pl));
        font_draw_scaled(pl, tx, ty + 50.0f * s, 16.0f * s, COL_INK_R, COL_INK_G, COL_INK_B, 1, screen_w, screen_h);
        if (it->limited)
            font_draw_scaled("Limited", tx, ty + 72.0f * s, 13.0f * s,
                             COL_ACCENT_R, COL_ACCENT_G, COL_ACCENT_B, 1, screen_w, screen_h);
        float meta_bottom = ty + (it->limited ? 92.0f : 74.0f) * s;
        float desc_w = ui->compact ? (pw - 40.0f * s) : (px + pw - 20.0f * s - tx);
        if (desc_w < 80.0f * s) desc_w = 80.0f * s;
        float desc_max_h = ui->buy_y - 36.0f * s - meta_bottom;
        if (desc_max_h < 0.0f) desc_max_h = 0.0f;
        float desc_h = 0.0f;
        if (it->description[0] && desc_max_h > 12.0f * s) {
            desc_h = draw_wrapped_text(it->description, tx, meta_bottom, desc_w, desc_max_h,
                                       13.0f * s, 18.0f * s,
                                       COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        }
        if (!ui->compact) {
            float hint_y = meta_bottom + desc_h + (desc_h > 1.0f ? 8.0f * s : 0.0f);
            if (hint_y + 14.0f * s < ui->buy_y - 8.0f * s)
                font_draw_scaled("Click to flip  |  drag to orbit  |  scroll to zoom",
                                 tx, hint_y, 12.0f * s,
                                 COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        } else {
            float hint_y = meta_bottom + desc_h + (desc_h > 1.0f ? 6.0f * s : 0.0f);
            if (hint_y + 14.0f * s < ui->buy_y - 8.0f * s)
                font_draw_scaled("Tap to flip  |  drag to orbit",
                                 tx, hint_y, 12.0f * s,
                                 COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
        }

        const char* buy_lbl = "Buy";
        bool can_buy = !it->owned && !it->donate && !(it->offsale && it->resale <= 0);
        if (it->owned) buy_lbl = "Owned";
        else if (it->donate) buy_lbl = "Donate on site";
        else if (it->offsale) buy_lbl = "Offsale";
        else if (ui->buying) buy_lbl = "Buying...";
        else if (it->price == 0) buy_lbl = "Get free";
        draw_round_rect(ui->buy_x, ui->buy_y, ui->buy_w, ui->buy_h, 10.0f * s,
                        can_buy ? COL_ACCENT_R : COL_SOFT_R,
                        can_buy ? COL_ACCENT_G : COL_SOFT_G,
                        can_buy ? COL_ACCENT_B : COL_SOFT_B, 1, screen_w, screen_h);
        float buy_fs = 16.0f * s;
        float blw = font_text_width_scaled(buy_lbl, buy_fs);
        font_draw_scaled(buy_lbl, ui->buy_x + (ui->buy_w - blw) * 0.5f, ui->buy_y + (ui->buy_h - buy_fs) * 0.5f,
                         buy_fs,
                         can_buy ? COL_ON_LIME_R : COL_MUTED_R,
                         can_buy ? COL_ON_LIME_G : COL_MUTED_G,
                         can_buy ? COL_ON_LIME_B : COL_MUTED_B,
                         1, screen_w, screen_h);
        if (ui->error[0])
            font_draw_scaled(ui->error, px + 20.0f * s, ui->buy_y - 24.0f * s, 14.0f * s,
                             COL_ERR_R, COL_ERR_G, COL_ERR_B, 1, screen_w, screen_h);
        else if (ui->status[0])
            font_draw_scaled(ui->status, px + 20.0f * s, ui->buy_y - 24.0f * s, 14.0f * s,
                             COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, 1, screen_w, screen_h);
    }

    {
        const InputState* in = input_get_state();
        int grab = 0;
        if (ui->view == 1 && in) {
            bool over = hit(in->mouse_x, in->mouse_y,
                            ui->preview_x, ui->preview_y, ui->preview_w, ui->preview_h);
            if (ui->preview_dragging) grab = 2;
            else if (over) grab = 1;
        }
        platform_set_cursor_grab(grab);
    }

    glEnable(GL_DEPTH_TEST);
}

bool catalog_ui_on_mousedown(CatalogUi* ui, float x, float y) {
    if (!ui || !ui->open) return false;
    ui->press_armed = true;
    ui->press_outside = (ui->panel_w > 1.0f &&
                         !hit(x, y, ui->panel_x, ui->panel_y, ui->panel_w, ui->panel_h));
    ui->preview_dragging = (ui->view == 1 &&
                            hit(x, y, ui->preview_x, ui->preview_y, ui->preview_w, ui->preview_h));
    if (ui->preview_dragging) {
        ui->auto_spin = false;
        ui->flip_active = false;
        ui->preview_drag_lx = x;
        ui->preview_drag_ly = y;
        ui->preview_down_x = x;
        ui->preview_down_y = y;
        ui->preview_orbited = false;
        ui->drag_moved = false;
        ui->drag_active = false;
        return true;
    }
    ui->drag_active = true;
    ui->drag_moved = false;
    ui->drag_last_y = y;
    ui->drag_start_x = x;
    ui->drag_start_y = y;
    return true;
}

void catalog_ui_on_mouseup(CatalogUi* ui, float x, float y) {
    if (!ui || !ui->open) return;
    bool moved = ui->drag_moved;
    bool armed = ui->press_armed;
    bool outside = ui->press_outside;
    bool previewing = ui->preview_dragging;
    ui->drag_active = false;
    ui->drag_moved = false;
    ui->press_armed = false;
    ui->press_outside = false;
    ui->preview_dragging = false;
    if (!armed) return;
    if (previewing) {
        float dx = x - ui->preview_down_x;
        float dy = y - ui->preview_down_y;
        float lim = 6.0f * (ui->ui_s > 0.1f ? ui->ui_s : 1.0f);
        if (!ui->preview_orbited && dx * dx + dy * dy < lim * lim)
            catalog_start_flip(ui);
        return;
    }
    if (moved) return;

    if (hit(x, y, ui->close_x, ui->close_y, ui->close_w, ui->close_h)) {
        catalog_ui_close(ui);
        return;
    }
    if (ui->view == 0 && hit(x, y, ui->sort_x, ui->sort_y, ui->sort_w, ui->sort_h)) {
        ui->sort = (ui->sort + 1) % 3;
        ui->loading = true;
        ui->load_deferred = false;
        catalog_fetch(ui, false);
        return;
    }
    if (ui->view == 1 && hit(x, y, ui->back_x, ui->back_y, ui->back_w, ui->back_h)) {
        ui->view = 0;
        ui->error[0] = '\0';
        ui->status[0] = '\0';
        return;
    }
    if (ui->view == 0) {
        float th = ui->tab_row_h - 6.0f * (ui->ui_s > 0.1f ? ui->ui_s : 1.0f);
        for (int t = 0; t < 5; t++) {
            if (hit(x, y, ui->tab_x[t], ui->tab_y[t], ui->tab_w[t], th)) {
                if (ui->tab != t) {
                    ui->tab = t;
                    ui->loading = true;
                    ui->load_deferred = false;
                    catalog_fetch(ui, false);
                }
                return;
            }
        }
        float stride = row_stride(ui);
        if (hit(x, y, ui->grid_x, ui->grid_y, ui->grid_w, ui->grid_h)) {
            for (int i = 0; i < ui->item_count; i++) {
                int col = i % ui->cols;
                int row = i / ui->cols;
                float cx = ui->grid_x + (float)col * (ui->cell + ui->gap);
                float cy = ui->grid_y + (float)row * stride - ui->scroll_y;
                if (cy + ui->cell + ui->label_h < ui->grid_y) continue;
                if (cy > ui->grid_y + ui->grid_h) break;
                if (hit(x, y, cx, cy, ui->cell, ui->cell + ui->label_h)) {
                    ui->detail = ui->items[i];
                    ui->selected = i;
                    ui->view = 1;
                    ui->error[0] = '\0';
                    ui->status[0] = '\0';
                    catalog_apply_item_camera(ui, ui->detail.type);
                    return;
                }
            }
        }
        if (!ui->compact && outside && ui->panel_w > 1.0f &&
            !hit(x, y, ui->panel_x, ui->panel_y, ui->panel_w, ui->panel_h)) {
            catalog_ui_close(ui);
        }
        return;
    }
    if (hit(x, y, ui->buy_x, ui->buy_y, ui->buy_w, ui->buy_h)) {
        if (ui->detail.donate) {
            char url[256];
            snprintf(url, sizeof(url), "%s/donate/", ui->host[0] ? ui->host : PW_SITE_ORIGIN);
            platform_open_url(url);
            return;
        }
        catalog_try_buy(ui);
    }
}

bool catalog_ui_on_scroll(CatalogUi* ui, float x, float y, float delta) {
    if (!ui || !ui->open) return false;
    if (ui->view == 1) {
        if (hit(x, y, ui->preview_x, ui->preview_y, ui->preview_w, ui->preview_h) ||
            hit(x, y, ui->panel_x, ui->panel_y, ui->panel_w, ui->panel_h)) {
            if (ui->cam_dist < 0.5f) ui->cam_dist = 6.0f;
            ui->cam_dist += delta * 0.55f;
            if (ui->cam_dist < 2.5f) ui->cam_dist = 2.5f;
            if (ui->cam_dist > 12.0f) ui->cam_dist = 12.0f;
            return true;
        }
        return true;
    }
    ui->scroll_y += delta * 28.0f * (ui->ui_s > 0.1f ? ui->ui_s : 1.0f);
    if (ui->scroll_y < 0) ui->scroll_y = 0;
    if (ui->scroll_y > ui->scroll_max) ui->scroll_y = ui->scroll_max;
    return true;
}

bool catalog_ui_on_key(CatalogUi* ui, int keycode) {
    if (!ui || !ui->open) return false;
    if (keycode == 27) {
        if (ui->view == 1) {
            ui->view = 0;
            return true;
        }
        catalog_ui_close(ui);
        return true;
    }
    return true;
}

#endif
