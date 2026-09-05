/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: texture.c                                                                           |
|   Purpose: textures                                                                         |
\*-------------------------------------------------------------------------------------------*/

#include "texture.h"
#include "part_material.h"
#include "log.h"
#include "scene.h"
#include "platform.h"
#include "pw_gles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

TextureID texture_load_from_memory(const uint8_t* data, int width, int height, int channels) {
    if (!data || width <= 0 || height <= 0) return TEXTURE_INVALID;

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    GLenum internal = (channels == 4) ? GL_RGBA8 : GL_RGB8;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    return (TextureID)tex;
}

static void pw_downsample_rgba(const uint8_t* src, int sw, int sh,
                               uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int y0 = y * sh / dh;
        int y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw;
            int x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t* p = src + ((size_t)yy * (size_t)sw + (size_t)xx) * 4u;
                    r += p[0]; g += p[1]; b += p[2]; a += p[3];
                    n++;
                }
            }
            uint8_t* o = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4u;
            if (n == 0) n = 1;
            o[0] = (uint8_t)(r / n);
            o[1] = (uint8_t)(g / n);
            o[2] = (uint8_t)(b / n);
            o[3] = (uint8_t)(a / n);
        }
    }
}

TextureID texture_load_atlas_from_memory(const uint8_t* data, int width, int height, int channels) {
    if (!data || width <= 0 || height <= 0) return TEXTURE_INVALID;

    GLint gl_max = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max);
    if (gl_max < 64) gl_max = 2048;
    int max_dim = gl_max;
    if (max_dim > 2048) max_dim = 2048;

    const uint8_t* upload = data;
    int uw = width, uh = height;
    uint8_t* scaled = NULL;
    if (channels == 4 && (width > max_dim || height > max_dim)) {
        float scale = (float)max_dim / (float)(width > height ? width : height);
        uw = (int)(width * scale);
        uh = (int)(height * scale);
        if (uw < 1) uw = 1;
        if (uh < 1) uh = 1;
        scaled = (uint8_t*)malloc((size_t)uw * (size_t)uh * 4u);
        if (scaled) {
            pw_downsample_rgba(data, width, height, scaled, uw, uh);
            upload = scaled;
            PW_LOG("Texture atlas downscaled %dx%d -> %dx%d (GL max %d)\n",
                   width, height, uw, uh, (int)gl_max);
        } else {
            uw = width;
            uh = height;
            upload = data;
        }
    }

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    GLenum internal = (channels == 4) ? GL_RGBA8 : GL_RGB8;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, uw, uh, 0, format, GL_UNSIGNED_BYTE, upload);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PW_ERR(ERR_GENERIC, "Atlas glTexImage2D failed (0x%x) %dx%d\n", (unsigned)err, uw, uh);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &tex);
        free(scaled);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return TEXTURE_INVALID;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(scaled);

    return (TextureID)tex;
}

static TextureID decode_and_upload_png(const uint8_t* file_data, size_t file_len) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    uint8_t* pixels = stbi_load_from_memory(file_data, (int)file_len, &w, &h, &channels, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) {
        return TEXTURE_INVALID;
    }

    TextureID tex = texture_load_from_memory(pixels, w, h, 4);
    stbi_image_free(pixels);
    return tex;
}

static void on_stud_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    TextureManager* tm = (TextureManager*)user;
    (void)path;
    if (data && len > 0) {
        tm->tex_stud = decode_and_upload_png(data, len);
    }
}

static void on_inlet_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    TextureManager* tm = (TextureManager*)user;
    (void)path;
    if (data && len > 0) {
        tm->tex_inlet = decode_and_upload_png(data, len);
    }
}

static void on_stud_normal_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    TextureManager* tm = (TextureManager*)user;
    (void)path;
    if (data && len > 0) { tm->tex_stud_normal = decode_and_upload_png(data, len); }
}

static void on_inlet_normal_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    TextureManager* tm = (TextureManager*)user;
    (void)path;
    if (data && len > 0) { tm->tex_inlet_normal = decode_and_upload_png(data, len); }
}

#define MAT_LOAD_CB(fn, id, field) \
static void fn(const char* path, const uint8_t* data, size_t len, void* user) { \
    TextureManager* tm = (TextureManager*)user; \
    (void)path; \
    if (data && len > 0) tm->field[id] = decode_and_upload_png(data, len); \
}
MAT_LOAD_CB(on_mat_grass_a, PART_MATERIAL_GRASS, tex_mat_albedo)
MAT_LOAD_CB(on_mat_dirt_a,  PART_MATERIAL_DIRT,  tex_mat_albedo)
MAT_LOAD_CB(on_mat_rock_a,  PART_MATERIAL_ROCK,  tex_mat_albedo)
MAT_LOAD_CB(on_mat_sand_a,  PART_MATERIAL_SAND,  tex_mat_albedo)
MAT_LOAD_CB(on_mat_wood_a,  PART_MATERIAL_WOOD,  tex_mat_albedo)
MAT_LOAD_CB(on_mat_metal_a, PART_MATERIAL_METAL, tex_mat_albedo)
MAT_LOAD_CB(on_mat_grass_n, PART_MATERIAL_GRASS, tex_mat_normal)
MAT_LOAD_CB(on_mat_dirt_n,  PART_MATERIAL_DIRT,  tex_mat_normal)
MAT_LOAD_CB(on_mat_rock_n,  PART_MATERIAL_ROCK,  tex_mat_normal)
MAT_LOAD_CB(on_mat_sand_n,  PART_MATERIAL_SAND,  tex_mat_normal)
MAT_LOAD_CB(on_mat_wood_n,  PART_MATERIAL_WOOD,  tex_mat_normal)
MAT_LOAD_CB(on_mat_metal_n, PART_MATERIAL_METAL, tex_mat_normal)
MAT_LOAD_CB(on_mat_metal_s, PART_MATERIAL_METAL, tex_mat_specular)
#undef MAT_LOAD_CB

bool texture_manager_init(TextureManager* tm) {
    memset(tm, 0, sizeof(TextureManager));
    tm->initialized = true;

#ifdef __EMSCRIPTEN__
#define PW_TEX_URL(name) "https://polyworld.games/assets/wasm/" name
#else
#define PW_TEX_URL(name) "assets/" name
#endif
    platform_load_file(PW_TEX_URL("stud.png"), on_stud_loaded, tm);
    platform_load_file(PW_TEX_URL("inlet.png"), on_inlet_loaded, tm);
    platform_load_file(PW_TEX_URL("stud_normal.png"), on_stud_normal_loaded, tm);
    platform_load_file(PW_TEX_URL("inlet_normal.png"), on_inlet_normal_loaded, tm);
    platform_load_file(PW_TEX_URL("grass.png"), on_mat_grass_a, tm);
    platform_load_file(PW_TEX_URL("dirt.png"), on_mat_dirt_a, tm);
    platform_load_file(PW_TEX_URL("rock.png"), on_mat_rock_a, tm);
    platform_load_file(PW_TEX_URL("sand.png"), on_mat_sand_a, tm);
    platform_load_file(PW_TEX_URL("wood.png"), on_mat_wood_a, tm);
    platform_load_file(PW_TEX_URL("metal.png"), on_mat_metal_a, tm);
    platform_load_file(PW_TEX_URL("grass_normal.png"), on_mat_grass_n, tm);
    platform_load_file(PW_TEX_URL("dirt_normal.png"), on_mat_dirt_n, tm);
    platform_load_file(PW_TEX_URL("rock_normal.png"), on_mat_rock_n, tm);
    platform_load_file(PW_TEX_URL("sand_normal.png"), on_mat_sand_n, tm);
    platform_load_file(PW_TEX_URL("wood_normal.png"), on_mat_wood_n, tm);
    platform_load_file(PW_TEX_URL("metal_normal.png"), on_mat_metal_n, tm);
    platform_load_file(PW_TEX_URL("metal_specular.png"), on_mat_metal_s, tm);
#undef PW_TEX_URL

    {
        unsigned char px[4] = { 0, 0, 0, 255 };
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        tm->tex_black = (TextureID)t;
    }

    return true;
}

void texture_manager_shutdown(TextureManager* tm) {
    if (tm->tex_stud) { GLuint t = tm->tex_stud; glDeleteTextures(1, &t); }
    if (tm->tex_inlet) { GLuint t = tm->tex_inlet; glDeleteTextures(1, &t); }
    if (tm->tex_stud_normal) { GLuint t = tm->tex_stud_normal; glDeleteTextures(1, &t); }
    if (tm->tex_inlet_normal) { GLuint t = tm->tex_inlet_normal; glDeleteTextures(1, &t); }
    for (int i = 0; i < PART_MATERIAL_COUNT; i++) {
        if (tm->tex_mat_albedo[i]) { GLuint t = tm->tex_mat_albedo[i]; glDeleteTextures(1, &t); }
        if (tm->tex_mat_normal[i]) { GLuint t = tm->tex_mat_normal[i]; glDeleteTextures(1, &t); }
        if (tm->tex_mat_specular[i]) { GLuint t = tm->tex_mat_specular[i]; glDeleteTextures(1, &t); }
    }
    if (tm->tex_black) { GLuint t = tm->tex_black; glDeleteTextures(1, &t); }
    memset(tm, 0, sizeof(TextureManager));
}

TextureID texture_get_for_surface(const TextureManager* tm, int surface_type) {
    switch (surface_type) {
        case SURFACE_STUD: return tm->tex_stud;
        case SURFACE_INLET: return tm->tex_inlet;
        default: return TEXTURE_INVALID;
    }
}

TextureID texture_get_normal_for_surface(const TextureManager* tm, int surface_type) {
    switch (surface_type) {
        case SURFACE_STUD: return tm->tex_stud_normal;
        case SURFACE_INLET: return tm->tex_inlet_normal;
        default: return TEXTURE_INVALID;
    }
}

TextureID texture_get_mat_albedo(const TextureManager* tm, int material_id) {
    if (!tm || material_id <= 0 || material_id >= PART_MATERIAL_COUNT) return TEXTURE_INVALID;
    return tm->tex_mat_albedo[material_id];
}

TextureID texture_get_mat_normal(const TextureManager* tm, int material_id) {
    if (!tm || material_id <= 0 || material_id >= PART_MATERIAL_COUNT) return TEXTURE_INVALID;
    return tm->tex_mat_normal[material_id];
}

TextureID texture_get_mat_specular(const TextureManager* tm, int material_id) {
    if (!tm || material_id <= 0 || material_id >= PART_MATERIAL_COUNT) return TEXTURE_INVALID;
    return tm->tex_mat_specular[material_id];
}

static void on_png_sync_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    TextureID* out = (TextureID*)user;
    (void)path;
    if (!out) return;
    *out = TEXTURE_INVALID;
    if (data && len > 0) {
        *out = decode_and_upload_png(data, len);
    }
}

TextureID texture_load_png(const char* path) {
    TextureID tex = TEXTURE_INVALID;

    platform_load_file(path, on_png_sync_loaded, &tex);
    return tex;
}
