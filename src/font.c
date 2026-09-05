/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: font.c                                                                              |
|   Purpose: TTF + Twemoji SVG, plus unicode fallbacks                                        |
\*-------------------------------------------------------------------------------------------*/

#include "font.h"
#include "log.h"
#include "platform.h"
#include "shader.h"
#include "pw_gles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define NANOSVG_IMPLEMENTATION
#include "../libs/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../libs/nanosvgrast.h"

#define FONT_ATLAS_SIZE 1024
#define FONT_COLOR_ATLAS 1024
#define FONT_BAKE_SIZE 64.0f
#define FONT_GLYPH_CACHE 1024
#define FONT_MAX_FACES 8

typedef struct {
    uint32_t cp;
    float x0, y0, x1, y1;
    float xoff, yoff, xadvance;
    bool used;
    bool is_color;
} GlyphEntry;

typedef struct {
    unsigned char* data;
    size_t data_len;
    stbtt_fontinfo info;
    float scale;
    bool ok;

    const unsigned char* svg_table;
    uint32_t svg_list_off;
    uint16_t svg_num_entries;
} FontFace;

static stbtt_bakedchar g_chars[96];
static unsigned int g_tex = 0;
static unsigned int g_color_tex = 0;
static unsigned int g_prog = 0;
static unsigned int g_color_prog = 0;
static bool g_ready = false;

static FontFace g_faces[FONT_MAX_FACES];
static int g_face_n = 0;
static int g_emoji_face = -1;
static int g_cjk_face = -1;

static unsigned char* g_atlas = NULL;
static unsigned char* g_color_atlas = NULL;
static GlyphEntry g_cache[FONT_GLYPH_CACHE];
static int g_pack_x = 0, g_pack_y = 0, g_pack_row_h = 0;
static int g_cpack_x = 0, g_cpack_y = 0, g_cpack_row_h = 0;

static NSVGrasterizer* g_svg_rast = NULL;

typedef struct {
    uint8_t* data;
    size_t len;
} FontFileBuf;

#if PW_MOBILE
static void on_font_file(const char* path, const uint8_t* data, size_t len, void* user) {
    FontFileBuf* buf = (FontFileBuf*)user;
    (void)path;
    if (!buf || !data || len == 0) return;
    buf->data = (uint8_t*)malloc(len);
    if (!buf->data) return;
    memcpy(buf->data, data, len);
    buf->len = len;
}
#endif

static const char* utf8_next(const char* s, unsigned int* out_cp) {
    const unsigned char* p = (const unsigned char*)s;
    if (!p || !*p) { *out_cp = 0; return s; }
    unsigned char c0 = p[0];
    if (c0 < 0x80) { *out_cp = c0; return s + 1; }
    if ((c0 & 0xE0) == 0xC0 && p[1]) {
        *out_cp = ((unsigned int)(c0 & 0x1F) << 6) | (p[1] & 0x3F);
        return s + 2;
    }
    if ((c0 & 0xF0) == 0xE0 && p[1] && p[2]) {
        *out_cp = ((unsigned int)(c0 & 0x0F) << 12) |
                  ((unsigned int)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return s + 3;
    }
    if ((c0 & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        *out_cp = ((unsigned int)(c0 & 0x07) << 18) |
                  ((unsigned int)(p[1] & 0x3F) << 12) |
                  ((unsigned int)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return s + 4;
    }
    *out_cp = 0xFFFD;
    return s + 1;
}

static bool cp_is_emoji(uint32_t cp) {
    if (cp >= 0x1F300 && cp <= 0x1FAFF) return true;
    if (cp >= 0x1F600 && cp <= 0x1F64F) return true;
    if (cp >= 0x1F680 && cp <= 0x1F6FF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    if (cp >= 0x2300 && cp <= 0x23FF) return true;
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    if (cp == 0x200D || cp == 0x20E3) return true;
    return false;
}

static bool cp_is_cjk(uint32_t cp) {
    if (cp >= 0x3040 && cp <= 0x30FF) return true;
    if (cp >= 0x3400 && cp <= 0x4DBF) return true;
    if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;
    if (cp >= 0xAC00 && cp <= 0xD7AF) return true;
    if (cp >= 0x20000 && cp <= 0x2FA1F) return true;
    return false;
}

static GlyphEntry* cache_find(uint32_t cp) {
    for (int i = 0; i < FONT_GLYPH_CACHE; i++) {
        if (g_cache[i].used && g_cache[i].cp == cp)
            return &g_cache[i];
    }
    return NULL;
}

static GlyphEntry* cache_alloc(uint32_t cp) {
    for (int i = 0; i < FONT_GLYPH_CACHE; i++) {
        if (!g_cache[i].used) {
            g_cache[i].used = true;
            g_cache[i].cp = cp;
            g_cache[i].is_color = false;
            return &g_cache[i];
        }
    }
    for (int i = 64; i < FONT_GLYPH_CACHE; i++) {
        g_cache[i].used = true;
        g_cache[i].cp = cp;
        g_cache[i].is_color = false;
        return &g_cache[i];
    }
    g_cache[0].used = true;
    g_cache[0].cp = cp;
    g_cache[0].is_color = false;
    return &g_cache[0];
}

static bool atlas_pack_ex(int* px, int* py, int* row_h, int atlas_size,
                          int w, int h, int* out_x, int* out_y) {
    int pad = 2;
    w += pad * 2;
    h += pad * 2;
    if (w > atlas_size || h > atlas_size) return false;
    if (*px + w > atlas_size) {
        *px = 0;
        *py += *row_h;
        *row_h = 0;
    }
    if (*py + h > atlas_size) return false;
    *out_x = *px + pad;
    *out_y = *py + pad;
    *px += w;
    if (h > *row_h) *row_h = h;
    return true;
}

static unsigned char* load_font_bytes(const char* path, size_t* out_len) {
    *out_len = 0;
#if PW_MOBILE
    FontFileBuf fbuf = {0};
    platform_load_file(path, on_font_file, &fbuf);
    if (!fbuf.data) return NULL;
    *out_len = fbuf.len;
    return fbuf.data;
#else
    FILE* f = fopen(path, "rb");
#if !PW_USE_GLES
    if (!f) {
#ifdef _WIN32
        char exe[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe, MAX_PATH)) {
            char* slash = strrchr(exe, '\\');
            if (!slash) slash = strrchr(exe, '/');
            if (slash) *slash = '\0';
            char try_path[MAX_PATH + 128];
            snprintf(try_path, sizeof(try_path), "%s\\%s", exe, path);
            f = fopen(try_path, "rb");
            if (!f) {
                snprintf(try_path, sizeof(try_path), "%s\\..\\%s", exe, path);
                f = fopen(try_path, "rb");
            }
        }
#else
        char exe[512];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            char* slash = strrchr(exe, '/');
            if (slash) *slash = '\0';
            char try_path[640];
            snprintf(try_path, sizeof(try_path), "%s/%s", exe, path);
            f = fopen(try_path, "rb");
            if (!f) {
                snprintf(try_path, sizeof(try_path), "%s/../%s", exe, path);
                f = fopen(try_path, "rb");
            }
        }
#endif
    }
#endif
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    unsigned char* data = (unsigned char*)malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return data;
#endif
}

static void face_bind_svg(FontFace* face) {
    face->svg_table = NULL;
    face->svg_num_entries = 0;
    if (!face->data || face->data_len < 12) return;
    uint16_t num_tables = (uint16_t)((face->data[4] << 8) | face->data[5]);
    for (uint16_t i = 0; i < num_tables; i++) {
        const unsigned char* e = face->data + 12 + i * 16;
        if (memcmp(e, "SVG ", 4) != 0) continue;
        uint32_t offset = ((uint32_t)e[8] << 24) | ((uint32_t)e[9] << 16) |
                          ((uint32_t)e[10] << 8) | (uint32_t)e[11];
        uint32_t length = ((uint32_t)e[12] << 24) | ((uint32_t)e[13] << 16) |
                          ((uint32_t)e[14] << 8) | (uint32_t)e[15];
        if (offset + length > face->data_len || length < 10) return;
        const unsigned char* svg = face->data + offset;
        uint32_t list_off = ((uint32_t)svg[2] << 24) | ((uint32_t)svg[3] << 16) |
                            ((uint32_t)svg[4] << 8) | (uint32_t)svg[5];
        if (list_off + 2 > length) return;
        const unsigned char* list = svg + list_off;
        uint16_t nent = (uint16_t)((list[0] << 8) | list[1]);
        face->svg_table = svg;
        face->svg_list_off = list_off;
        face->svg_num_entries = nent;
        return;
    }
}

static char* svg_copy_for_glyph(FontFace* face, int glyph_id) {
    if (!face->svg_table || face->svg_num_entries == 0) return NULL;
    const unsigned char* list = face->svg_table + face->svg_list_off;
    for (uint16_t i = 0; i < face->svg_num_entries; i++) {
        const unsigned char* e = list + 2 + i * 12;
        uint16_t start = (uint16_t)((e[0] << 8) | e[1]);
        uint16_t end = (uint16_t)((e[2] << 8) | e[3]);
        if (glyph_id < start || glyph_id > end) continue;
        uint32_t doc_off = ((uint32_t)e[4] << 24) | ((uint32_t)e[5] << 16) |
                           ((uint32_t)e[6] << 8) | (uint32_t)e[7];
        uint32_t doc_len = ((uint32_t)e[8] << 24) | ((uint32_t)e[9] << 16) |
                           ((uint32_t)e[10] << 8) | (uint32_t)e[11];
        const unsigned char* doc = face->svg_table + doc_off;
        const unsigned char* start_p = NULL;
        for (uint32_t k = 0; k + 4 < doc_len; k++) {
            if (doc[k] == '<' && doc[k + 1] == 's' && doc[k + 2] == 'v' && doc[k + 3] == 'g') {
                start_p = doc + k;
                break;
            }
        }
        if (!start_p) return NULL;
        const unsigned char* end_p = NULL;
        for (const unsigned char* q = start_p; q + 6 < doc + doc_len; q++) {
            if (q[0] == '<' && q[1] == '/' && q[2] == 's' && q[3] == 'v' && q[4] == 'g' && q[5] == '>') {
                end_p = q + 6;
                break;
            }
        }
        if (!end_p) end_p = doc + doc_len;
        size_t n = (size_t)(end_p - start_p);
        char* out = (char*)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, start_p, n);
        out[n] = '\0';
        return out;
    }
    return NULL;
}

static unsigned char* rasterize_twemoji_svg(FontFace* face, int glyph_id,
                                            int* gw, int* gh, int* gox, int* goy) {
    *gw = *gh = *gox = *goy = 0;
    char* svg = svg_copy_for_glyph(face, glyph_id);
    if (!svg) return NULL;
    NSVGimage* image = nsvgParse(svg, "px", 96.0f);
    free(svg);
    if (!image || image->width <= 0 || image->height <= 0) {
        if (image) nsvgDelete(image);
        return NULL;
    }
    if (!g_svg_rast) g_svg_rast = nsvgCreateRasterizer();
    if (!g_svg_rast) { nsvgDelete(image); return NULL; }

    int size = (int)FONT_BAKE_SIZE;
    if (size < 16) size = 16;
    float max_dim = image->width > image->height ? image->width : image->height;
    float scale = (float)size / max_dim;
    int w = (int)ceilf(image->width * scale);
    int h = (int)ceilf(image->height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 128) w = 128;
    if (h > 128) h = 128;

    unsigned char* rgba = (unsigned char*)calloc((size_t)w * (size_t)h * 4, 1);
    if (!rgba) { nsvgDelete(image); return NULL; }
    nsvgRasterize(g_svg_rast, image, 0, 0, scale, rgba, w, h, w * 4);
    nsvgDelete(image);

    unsigned sum = 0;
    for (int i = 0; i < w * h; i++) sum += rgba[i * 4 + 3];
    if (sum < 32) { free(rgba); return NULL; }

    *gw = w;
    *gh = h;
    *gox = 0;
    *goy = -(int)(FONT_BAKE_SIZE * 0.85f);
    return rgba;
}

static bool add_face_from_data(unsigned char* data, size_t len, const char* tag) {
    if (!data || len == 0 || g_face_n >= FONT_MAX_FACES) {
        free(data);
        return false;
    }
    FontFace* face = &g_faces[g_face_n];
    memset(face, 0, sizeof(*face));
    face->data = data;
    face->data_len = len;
    if (!stbtt_InitFont(&face->info, face->data, stbtt_GetFontOffsetForIndex(face->data, 0))) {
        PW_ERR(ERR_FILE, "font face init failed: %s\n", tag ? tag : "?");
        free(data);
        face->data = NULL;
        return false;
    }
    face->scale = stbtt_ScaleForPixelHeight(&face->info, FONT_BAKE_SIZE);
    face->ok = true;
    face_bind_svg(face);
    if (tag) {
        if (strstr(tag, "Twemoji") || strstr(tag, "twemoji"))
            g_emoji_face = g_face_n;
        else if (g_emoji_face < 0 && (strstr(tag, "Emoji") || strstr(tag, "emoji")))
            g_emoji_face = g_face_n;
        if (strstr(tag, "Droid") || strstr(tag, "CJK") || strstr(tag, "Fallback"))
            g_cjk_face = g_face_n;
    }
    g_face_n++;
    PW_LOG("[font] loaded face %s (%zu bytes)%s\n", tag ? tag : "?", len,
           face->svg_table ? " [SVG]" : "");
    return true;
}

static bool add_face_path(const char* path) {
    size_t len = 0;
    unsigned char* data = load_font_bytes(path, &len);
    if (!data) return false;
    return add_face_from_data(data, len, path);
}

static int build_face_order(uint32_t cp, int* order, int maxn) {
    int n = 0;
    int used[FONT_MAX_FACES];
    memset(used, 0, sizeof(used));
    #define PUSH(i) do { \
        int _i = (i); \
        if (_i >= 0 && _i < g_face_n && !used[_i] && n < maxn) { \
            used[_i] = 1; order[n++] = _i; \
        } \
    } while (0)

    if (cp_is_emoji(cp)) PUSH(g_emoji_face);
    if (cp_is_cjk(cp)) PUSH(g_cjk_face);
    for (int i = 0; i < g_face_n; i++) PUSH(i);
    #undef PUSH
    return n;
}

static bool rasterize_from_faces(uint32_t cp, unsigned char** out_bmp, bool* out_color,
                                 int* gw, int* gh, int* gox, int* goy, float* xadvance) {
    *out_bmp = NULL;
    *out_color = false;
    *gw = *gh = *gox = *goy = 0;
    *xadvance = FONT_BAKE_SIZE * 0.5f;

    int order[FONT_MAX_FACES];
    int n = build_face_order(cp, order, FONT_MAX_FACES);
    for (int oi = 0; oi < n; oi++) {
        FontFace* face = &g_faces[order[oi]];
        if (!face->ok) continue;
        int glyph = stbtt_FindGlyphIndex(&face->info, (int)cp);
        if (glyph == 0) continue;

        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&face->info, (int)cp, &advance, &lsb);
        *xadvance = (float)advance * face->scale;

        if (face->svg_table) {
            unsigned char* rgba = rasterize_twemoji_svg(face, glyph, gw, gh, gox, goy);
            if (rgba) {
                if (*xadvance < FONT_BAKE_SIZE * 0.5f)
                    *xadvance = FONT_BAKE_SIZE;
                *out_bmp = rgba;
                *out_color = true;
                return true;
            }
        }

        unsigned char* bmp = stbtt_GetCodepointBitmap(&face->info, face->scale, face->scale,
                                                      (int)cp, gw, gh, gox, goy);
        if (!bmp || *gw <= 1 || *gh <= 1) {
            if (bmp) stbtt_FreeBitmap(bmp, NULL);
            continue;
        }
        *out_bmp = bmp;
        *out_color = false;
        return true;
    }
    return false;
}

static void upload_mono_rows(int ax, int ay, int gw, int gh) {
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int yy = 0; yy < gh; yy++) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, ax, ay + yy, gw, 1, GL_RED, GL_UNSIGNED_BYTE,
                        g_atlas + (ay + yy) * FONT_ATLAS_SIZE + ax);
    }
}

static void upload_color_rows(int ax, int ay, int gw, int gh) {
    glBindTexture(GL_TEXTURE_2D, g_color_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int yy = 0; yy < gh; yy++) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, ax, ay + yy, gw, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        g_color_atlas + ((ay + yy) * FONT_COLOR_ATLAS + ax) * 4);
    }
}

static bool glyph_get(uint32_t cp, float* x0, float* y0, float* x1, float* y1,
                      float* xoff, float* yoff, float* xadvance, bool* is_color) {
    if (is_color) *is_color = false;
    if (cp >= 32 && cp < 128) {
        stbtt_bakedchar* bc = &g_chars[cp - 32];
        *x0 = (float)bc->x0; *y0 = (float)bc->y0;
        *x1 = (float)bc->x1; *y1 = (float)bc->y1;
        *xoff = bc->xoff; *yoff = bc->yoff;
        *xadvance = bc->xadvance;
        return true;
    }
    if (!g_atlas || !g_tex || g_face_n <= 0) return false;

    GlyphEntry* ge = cache_find(cp);
    if (ge) {
        *x0 = ge->x0; *y0 = ge->y0; *x1 = ge->x1; *y1 = ge->y1;
        *xoff = ge->xoff; *yoff = ge->yoff; *xadvance = ge->xadvance;
        if (is_color) *is_color = ge->is_color;
        return true;
    }

    if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F)) {
        ge = cache_alloc(cp);
        ge->x0 = ge->y0 = ge->x1 = ge->y1 = 0;
        ge->xoff = ge->yoff = ge->xadvance = 0;
        *x0 = *y0 = *x1 = *y1 = 0;
        *xoff = *yoff = *xadvance = 0;
        return true;
    }

    unsigned char* bmp = NULL;
    bool color = false;
    int gw = 0, gh = 0, gox = 0, goy = 0;
    float adv = FONT_BAKE_SIZE * 0.5f;
    bool ok = rasterize_from_faces(cp, &bmp, &color, &gw, &gh, &gox, &goy, &adv);

    if (!ok || !bmp) {
        int tw = (int)(FONT_BAKE_SIZE * 0.5f);
        if (tw < 8) tw = 8;
        int ax, ay;
        if (!atlas_pack_ex(&g_pack_x, &g_pack_y, &g_pack_row_h, FONT_ATLAS_SIZE, tw, tw, &ax, &ay)) {
            *x0 = *y0 = *x1 = *y1 = 0;
            *xoff = 0; *yoff = -FONT_BAKE_SIZE * 0.75f;
            *xadvance = (float)tw;
            return true;
        }
        for (int yy = 0; yy < tw; yy++)
            for (int xx = 0; xx < tw; xx++) {
                unsigned char v = (xx == 0 || yy == 0 || xx == tw - 1 || yy == tw - 1) ? 200 : 40;
                g_atlas[(ay + yy) * FONT_ATLAS_SIZE + (ax + xx)] = v;
            }
        upload_mono_rows(ax, ay, tw, tw);
        ge = cache_alloc(cp);
        ge->x0 = (float)ax; ge->y0 = (float)ay;
        ge->x1 = (float)(ax + tw); ge->y1 = (float)(ay + tw);
        ge->xoff = 0; ge->yoff = -FONT_BAKE_SIZE * 0.8f;
        ge->xadvance = (float)tw + 2.0f;
        *x0 = ge->x0; *y0 = ge->y0; *x1 = ge->x1; *y1 = ge->y1;
        *xoff = ge->xoff; *yoff = ge->yoff; *xadvance = ge->xadvance;
        return true;
    }

    if (color) {
        int ax, ay;
        if (!g_color_atlas || !g_color_tex ||
            !atlas_pack_ex(&g_cpack_x, &g_cpack_y, &g_cpack_row_h, FONT_COLOR_ATLAS,
                           gw, gh, &ax, &ay)) {
            free(bmp);
            *x0 = *y0 = *x1 = *y1 = 0;
            *xoff = (float)gox; *yoff = (float)goy;
            *xadvance = adv;
            return true;
        }
        for (int yy = 0; yy < gh; yy++) {
            memcpy(g_color_atlas + ((ay + yy) * FONT_COLOR_ATLAS + ax) * 4,
                   bmp + yy * gw * 4, (size_t)gw * 4);
        }
        upload_color_rows(ax, ay, gw, gh);
        free(bmp);
        ge = cache_alloc(cp);
        ge->is_color = true;
        ge->x0 = (float)ax; ge->y0 = (float)ay;
        ge->x1 = (float)(ax + gw); ge->y1 = (float)(ay + gh);
        ge->xoff = (float)gox; ge->yoff = (float)goy;
        ge->xadvance = adv;
        *x0 = ge->x0; *y0 = ge->y0; *x1 = ge->x1; *y1 = ge->y1;
        *xoff = ge->xoff; *yoff = ge->yoff; *xadvance = ge->xadvance;
        if (is_color) *is_color = true;
        return true;
    }

    int ax, ay;
    if (!atlas_pack_ex(&g_pack_x, &g_pack_y, &g_pack_row_h, FONT_ATLAS_SIZE, gw, gh, &ax, &ay)) {
        stbtt_FreeBitmap(bmp, NULL);
        *x0 = *y0 = *x1 = *y1 = 0;
        *xoff = (float)gox; *yoff = (float)goy;
        *xadvance = adv;
        return true;
    }
    for (int yy = 0; yy < gh; yy++)
        memcpy(g_atlas + (ay + yy) * FONT_ATLAS_SIZE + ax, bmp + yy * gw, (size_t)gw);
    upload_mono_rows(ax, ay, gw, gh);
    stbtt_FreeBitmap(bmp, NULL);

    ge = cache_alloc(cp);
    ge->x0 = (float)ax; ge->y0 = (float)ay;
    ge->x1 = (float)(ax + gw); ge->y1 = (float)(ay + gh);
    ge->xoff = (float)gox; ge->yoff = (float)goy;
    ge->xadvance = adv;
    *x0 = ge->x0; *y0 = ge->y0; *x1 = ge->x1; *y1 = ge->y1;
    *xoff = ge->xoff; *yoff = ge->yoff; *xadvance = ge->xadvance;
    return true;
}

bool font_init(void) {
    if (!g_ready && g_atlas && g_color_atlas && g_face_n > 0) {
        glGenTextures(1, &g_tex);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0,
                     GL_RED, GL_UNSIGNED_BYTE, g_atlas);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        glGenTextures(1, &g_color_tex);
        glBindTexture(GL_TEXTURE_2D, g_color_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FONT_COLOR_ATLAS, FONT_COLOR_ATLAS, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_color_atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        g_prog = shader_load_program("font_mono");
        g_color_prog = shader_load_program("font_color");
        if (!g_prog || !g_color_prog) return false;
        g_ready = true;
        return true;
    }

    if (g_ready) return true;

    memset(g_faces, 0, sizeof(g_faces));
    g_face_n = 0;
    g_emoji_face = g_cjk_face = -1;
    memset(g_cache, 0, sizeof(g_cache));

    size_t primary_len = 0;
    unsigned char* primary = load_font_bytes("assets/font.ttf", &primary_len);
    if (!primary)
        primary = load_font_bytes("desktop/font.ttf", &primary_len);
    if (!primary) {
        PW_ERR(ERR_FILE, "font.ttf not found\n");
        return false;
    }

    g_atlas = (unsigned char*)calloc((size_t)FONT_ATLAS_SIZE * (size_t)FONT_ATLAS_SIZE, 1);
    g_color_atlas = (unsigned char*)calloc((size_t)FONT_COLOR_ATLAS * (size_t)FONT_COLOR_ATLAS * 4, 1);
    if (!g_atlas || !g_color_atlas) {
        free(primary); free(g_atlas); free(g_color_atlas);
        g_atlas = g_color_atlas = NULL;
        return false;
    }
    stbtt_BakeFontBitmap(primary, 0, FONT_BAKE_SIZE, g_atlas,
                         FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 32, 96, g_chars);

    if (!add_face_from_data(primary, primary_len, "assets/font.ttf")) {
        free(g_atlas); free(g_color_atlas);
        g_atlas = g_color_atlas = NULL;
        return false;
    }

    add_face_path("assets/fonts/TwemojiColor.ttf");
    add_face_path("assets/fonts/NotoSans-Regular.ttf");
    add_face_path("assets/fonts/NotoSansSymbols2-Regular.ttf");
    add_face_path("assets/fonts/NotoEmoji-Regular.ttf");
    add_face_path("assets/fonts/DroidSansFallback.ttf");

#if !PW_MOBILE && !defined(__EMSCRIPTEN__)
    if (g_cjk_face < 0) {
        add_face_path("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf");
        add_face_path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    }
#endif

    int bake_bottom = 0;
    for (int i = 0; i < 96; i++)
        if (g_chars[i].y1 > bake_bottom) bake_bottom = g_chars[i].y1;
    g_pack_x = 0;
    g_pack_y = bake_bottom + 2;
    g_pack_row_h = 0;
    g_cpack_x = g_cpack_y = g_cpack_row_h = 0;

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_ATLAS_SIZE, FONT_ATLAS_SIZE, 0,
                 GL_RED, GL_UNSIGNED_BYTE, g_atlas);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    glGenTextures(1, &g_color_tex);
    glBindTexture(GL_TEXTURE_2D, g_color_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FONT_COLOR_ATLAS, FONT_COLOR_ATLAS, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_color_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    g_prog = shader_load_program("font_mono");
    g_color_prog = shader_load_program("font_color");
    if (!g_prog || !g_color_prog) return false;

    g_ready = true;
    return true;
}

void font_invalidate_gl(bool context_alive) {
    if (context_alive) {
        if (g_tex) glDeleteTextures(1, &g_tex);
        if (g_color_tex) glDeleteTextures(1, &g_color_tex);
        if (g_prog) glDeleteProgram(g_prog);
        if (g_color_prog) glDeleteProgram(g_color_prog);
    }
    g_tex = 0;
    g_color_tex = 0;
    g_prog = 0;
    g_color_prog = 0;
    g_ready = false;
}

static void draw_batch(unsigned int prog, unsigned int tex, const float* verts, int vc,
                       float r, float g, float b, float a) {
    if (vc <= 0) return;
    glUseProgram(prog);
    glUniform4f(glGetUniformLocation(prog, "u_color"), r, g, b, a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)(vc * 4 * sizeof(float)), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, vc);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static float draw_internal(const char* text, float x, float y, float pixel_height,
                           float r, float g, float b, float a, int sw, int sh) {
    if (!g_ready || !text || !text[0]) return 0;

    float scale_factor = pixel_height / FONT_BAKE_SIZE;
    float mono_v[6 * 4 * 256];
    float color_v[6 * 4 * 128];
    int mono_vc = 0, color_vc = 0;
    float cx = x, cy = y;
    float fsw = (float)sw, fsh = (float)sh;

    for (const char* p = text; *p && mono_vc < 256 * 6 && color_vc < 128 * 6; ) {
        unsigned int cp = 0;
        p = utf8_next(p, &cp);
        if (cp == 0) break;
        if (cp < 32) continue;

        float gx0, gy0, gx1, gy1, xoff, yoff, xadv;
        bool is_color = false;
        if (!glyph_get(cp, &gx0, &gy0, &gx1, &gy1, &xoff, &yoff, &xadv, &is_color))
            continue;

        if (gx1 > gx0 && gy1 > gy0) {
            float px0 = cx + xoff * scale_factor;
            float py0 = cy + yoff * scale_factor + pixel_height;
            float px1 = px0 + (gx1 - gx0) * scale_factor;
            float py1 = py0 + (gy1 - gy0) * scale_factor;
            float nx0 = px0 / fsw * 2.0f - 1.0f, ny0 = 1.0f - py0 / fsh * 2.0f;
            float nx1 = px1 / fsw * 2.0f - 1.0f, ny1 = 1.0f - py1 / fsh * 2.0f;
            float atlas = is_color ? (float)FONT_COLOR_ATLAS : (float)FONT_ATLAS_SIZE;
            float u0 = gx0 / atlas, v0 = gy0 / atlas;
            float u1 = gx1 / atlas, v1 = gy1 / atlas;
        float q[] = { nx0,ny0,u0,v0, nx1,ny0,u1,v0, nx1,ny1,u1,v1,
                      nx0,ny0,u0,v0, nx1,ny1,u1,v1, nx0,ny1,u0,v1 };
            if (is_color) {
                memcpy(color_v + color_vc * 4, q, sizeof(q));
                color_vc += 6;
            } else {
                memcpy(mono_v + mono_vc * 4, q, sizeof(q));
                mono_vc += 6;
            }
        }
        cx += xadv * scale_factor;
    }
    if (mono_vc == 0 && color_vc == 0) return 0;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    draw_batch(g_prog, g_tex, mono_v, mono_vc, r, g, b, a);
    draw_batch(g_color_prog, g_color_tex, color_v, color_vc, 1, 1, 1, a);
    glDisable(GL_BLEND);
    return cx - x;
}

float font_draw(const char* text, float x, float y, float r, float g, float b, float a, int sw, int sh) {
    return draw_internal(text, x, y, 24.0f, r, g, b, a, sw, sh);
}

float font_draw_small(const char* text, float x, float y, float r, float g, float b, float a, int sw, int sh) {
    return draw_internal(text, x, y, 14.0f, r, g, b, a, sw, sh);
}

float font_draw_shadow(const char* text, float x, float y, float r, float g, float b, float a, int sw, int sh) {
    draw_internal(text, x+1, y+1, 24.0f, 0, 0, 0, a*0.7f, sw, sh);
    return draw_internal(text, x, y, 24.0f, r, g, b, a, sw, sh);
}

float font_draw_scaled(const char* text, float x, float y, float pixel_height,
                       float r, float g, float b, float a, int sw, int sh) {
    return draw_internal(text, x, y, pixel_height, r, g, b, a, sw, sh);
}

float font_text_width(const char* text) {
    return font_text_width_scaled(text, 24.0f);
}

float font_text_width_scaled(const char* text, float pixel_height) {
    if (!g_ready || !text) return 0;
    float scale_factor = pixel_height / FONT_BAKE_SIZE;
    float w = 0;
    for (const char* p = text; *p; ) {
        unsigned int cp = 0;
        p = utf8_next(p, &cp);
        if (cp == 0) break;
        if (cp < 32) continue;
        float gx0, gy0, gx1, gy1, xoff, yoff, xadv;
        bool is_color = false;
        if (!glyph_get(cp, &gx0, &gy0, &gx1, &gy1, &xoff, &yoff, &xadv, &is_color))
            continue;
        (void)is_color;
        w += xadv * scale_factor;
    }
    return w;
}
