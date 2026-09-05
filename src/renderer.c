/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: renderer.c                                                                          |
|   Purpose: GL. desktop, web, gles. it's a lot                                               |
\*-------------------------------------------------------------------------------------------*/

#include "renderer.h"
#include "brick_batch.h"
#include "part_material.h"
#include "mesh_primitives.h"
#include "log.h"
#include "platform.h"
#include "pw_gles.h"
#if defined(PW_QUEST) && defined(VR)
#include "vr_openxr.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#if PW_USE_GLES
#include <GLES3/gl3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#else
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#endif

static void debug_ensure_init(void);

static GPUMesh g_curve_sph[MESH_CURVE_LOD_COUNT];
static GPUMesh g_curve_cyl[MESH_CURVE_LOD_COUNT];
static uint8_t g_curve_got[MESH_CURVE_LOD_COUNT];

void renderer_invalidate_curve_meshes(void) {
    memset(g_curve_sph, 0, sizeof(g_curve_sph));
    memset(g_curve_cyl, 0, sizeof(g_curve_cyl));
    memset(g_curve_got, 0, sizeof(g_curve_got));
}

GPUMesh* renderer_unit_curve_mesh(int prim_kind, int lod) {
    if (lod < 0) lod = 0;
    if (lod >= MESH_CURVE_LOD_COUNT) lod = MESH_CURVE_LOD_COUNT - 1;
    if (prim_kind == 2) {
        if (!(g_curve_got[lod] & 2)) {
            MeshData md;
            int segs = mesh_curve_lod_cylinder_segments(lod);
            if (!create_cylinder_mesh(&md, 0.5f, 1.0f, segs)) return NULL;
            if (!mesh_upload(&md, &g_curve_cyl[lod])) { mesh_data_free(&md); return NULL; }
            mesh_data_free(&md);
            g_curve_cyl[lod].prim_kind = 2;
            g_curve_got[lod] |= 2;
        }
        return &g_curve_cyl[lod];
    }
    if (!(g_curve_got[lod] & 1)) {
        MeshData md;
        int segs = mesh_curve_lod_sphere_segments(lod);
        if (!create_sphere_mesh(&md, 0.5f, segs, segs / 2)) return NULL;
        if (!mesh_upload(&md, &g_curve_sph[lod])) { mesh_data_free(&md); return NULL; }
        mesh_data_free(&md);
        g_curve_sph[lod].prim_kind = 1;
        g_curve_got[lod] |= 1;
    }
    return &g_curve_sph[lod];
}

static GPUMesh* renderer_entity_tess_mesh(Renderer* r, const Entity* e) {
    GPUMesh* m = e ? e->mesh : NULL;
    if (!m || (m->prim_kind != 1 && m->prim_kind != 2)) return m;
    if (r && r->is_studio) return m;
    if (m->bounding_radius > 1.2f) return m;
    float sx = fabsf(e->transform.scale.x);
    float sy = fabsf(e->transform.scale.y);
    float sz = fabsf(e->transform.scale.z);
    float rad = (m->prim_kind == 1)
        ? 0.5f * fmaxf(sx, fmaxf(sy, sz))
        : 0.5f * fmaxf(sx, sz);
    int q = r ? r->curve_tess_quality : 2;
    int lod = mesh_curve_lod_index(rad, q);
    GPUMesh* lodm = renderer_unit_curve_mesh((int)m->prim_kind, lod);
    return lodm ? lodm : m;
}

static bool renderer_shadow_fbo_complete(void) {
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void renderer_shadow_configure_depth_tex(unsigned int tex, int map,
                                                unsigned int internal, unsigned int type) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, map, map, 0, GL_DEPTH_COMPONENT, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static bool renderer_shadow_try_depth_tex(Renderer* r, int map, unsigned int internal, unsigned int type) {
    if (!r->shadow_depth_tex) glGenTextures(1, &r->shadow_depth_tex);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
    renderer_shadow_configure_depth_tex(r->shadow_depth_tex, map, internal, type);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->shadow_depth_tex, 0);
    if (!renderer_shadow_fbo_complete()) return false;
    r->shadow_depth_internal = internal;
    return true;
}

static bool renderer_shadow_attach_depth(Renderer* r, int map) {
    if (renderer_shadow_try_depth_tex(r, map, GL_DEPTH_COMPONENT24, GL_UNSIGNED_INT))
        return true;
#ifdef GL_DEPTH_COMPONENT32F
    if (renderer_shadow_try_depth_tex(r, map, GL_DEPTH_COMPONENT32F, GL_FLOAT))
        return true;
#endif
    if (renderer_shadow_try_depth_tex(r, map, GL_DEPTH_COMPONENT16, GL_UNSIGNED_SHORT))
        return true;
    return false;
}

static void renderer_shadow_set_color(Renderer* r, int map,
                                     unsigned int internal, unsigned int format, unsigned int type) {
    glBindTexture(GL_TEXTURE_2D, r->shadow_id_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, map, map, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->shadow_id_tex, 0);
#if !PW_USE_GLES
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
    r->shadow_color_internal = internal;
    r->shadow_color_format = format;
    r->shadow_color_type = type;
}

static bool renderer_setup_cascade_target(unsigned int* fbo, unsigned int* color, unsigned int* depth_tex,
                                          int map, unsigned int internal, unsigned int format,
                                          unsigned int type, unsigned int depth_internal, unsigned int depth_type) {
    if (!*color) glGenTextures(1, color);
    if (!*fbo) glGenFramebuffers(1, fbo);
    if (!*depth_tex) glGenTextures(1, depth_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glBindTexture(GL_TEXTURE_2D, *color);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, map, map, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *color, 0);
#if !PW_USE_GLES
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
    renderer_shadow_configure_depth_tex(*depth_tex, map, depth_internal, depth_type);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, *depth_tex, 0);
    return renderer_shadow_fbo_complete();
}

static bool renderer_setup_glow_evsm(Renderer* r);

static bool renderer_setup_sun_shadow_map(Renderer* r, int map) {
    if (!r || map < 16) return false;
    if (!r->shadow_id_tex) glGenTextures(1, &r->shadow_id_tex);
    if (!r->shadow_fbo) glGenFramebuffers(1, &r->shadow_fbo);

    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);

    static const unsigned int k_color[][3] = {
        { GL_RGBA32F, GL_RGBA, GL_FLOAT },
        { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT },
    };

    bool ok = false;
    for (size_t i = 0; i < sizeof(k_color) / sizeof(k_color[0]); i++) {
        renderer_shadow_set_color(r, map, k_color[i][0], k_color[i][1], k_color[i][2]);
        if (renderer_shadow_attach_depth(r, map)) {
            ok = true;
            break;
        }
    }

    if (ok && r->shadow_depth_tex) {
        unsigned int dtype = GL_UNSIGNED_INT;
        if (r->shadow_depth_internal == GL_DEPTH_COMPONENT16) dtype = GL_UNSIGNED_SHORT;
#ifdef GL_DEPTH_COMPONENT32F
        if (r->shadow_depth_internal == GL_DEPTH_COMPONENT32F) dtype = GL_FLOAT;
#endif
        if (!renderer_setup_cascade_target(&r->shadow_near_fbo, &r->shadow_near_id_tex,
                                           &r->shadow_near_depth_tex, map,
                                           r->shadow_color_internal, r->shadow_color_format,
                                           r->shadow_color_type, r->shadow_depth_internal, dtype)) {
            if (r->shadow_near_fbo) { glDeleteFramebuffers(1, &r->shadow_near_fbo); r->shadow_near_fbo = 0; }
            if (r->shadow_near_id_tex) { glDeleteTextures(1, &r->shadow_near_id_tex); r->shadow_near_id_tex = 0; }
            if (r->shadow_near_depth_tex) { glDeleteTextures(1, &r->shadow_near_depth_tex); r->shadow_near_depth_tex = 0; }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (ok) {
        r->shadow_map_size = map;

        r->shadow_esm_c = (r->shadow_color_internal == (unsigned int)GL_RGBA32F) ? 40.0f : 5.4f;
        r->shadow_cache_far_ok = false;
        r->shadow_cache_near_ok = false;
        renderer_setup_glow_evsm(r);
        return true;
    }
    PW_ERR(ERR_GENERIC, "Sun shadow FBO incomplete (map=%d)\n", map);
    return false;
}

static bool renderer_setup_glow_evsm(Renderer* r) {
    if (!r || !r->shadow_shader.program) return false;
    int face = RENDERER_GLOW_SHADOW_FACE;
    r->glow_shadow_face = face;
    int aw = face * 3, ah = face * 2;
    unsigned intern = r->shadow_color_internal ? r->shadow_color_internal : (unsigned)GL_RGBA16F;
    unsigned fmt = r->shadow_color_format ? r->shadow_color_format : (unsigned)GL_RGBA;
    unsigned type = r->shadow_color_type ? r->shadow_color_type : (unsigned)GL_FLOAT;
    unsigned dint = r->shadow_depth_internal ? r->shadow_depth_internal : (unsigned)GL_DEPTH_COMPONENT24;
    unsigned dtype = GL_UNSIGNED_INT;
    if (dint == GL_DEPTH_COMPONENT16) dtype = GL_UNSIGNED_SHORT;
#ifdef GL_DEPTH_COMPONENT32F
    if (dint == GL_DEPTH_COMPONENT32F) dtype = GL_FLOAT;
#endif

    if (!r->glow_shadow_fbo) glGenFramebuffers(1, &r->glow_shadow_fbo);
    if (!r->glow_shadow_depth_tex) glGenTextures(1, &r->glow_shadow_depth_tex);
    glBindTexture(GL_TEXTURE_2D, r->glow_shadow_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)dint, aw, ah, 0, GL_DEPTH_COMPONENT, dtype, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    for (int i = 0; i < RENDERER_MAX_GLOW_SHADOW_LIGHTS; i++) {
        if (!r->glow_shadow_tex[i]) glGenTextures(1, &r->glow_shadow_tex[i]);
        glBindTexture(GL_TEXTURE_2D, r->glow_shadow_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)intern, aw, ah, 0, fmt, type, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, r->glow_shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->glow_shadow_tex[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->glow_shadow_depth_tex, 0);
#if !PW_USE_GLES
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
    bool ok = renderer_shadow_fbo_complete();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    r->glow_shadow_cache_ok = false;
    r->glow_shadow_count = 0;
    if (!ok) {
        PW_ERR(ERR_GENERIC, "Glow EVSM atlas FBO incomplete\n");
        r->glow_shadow_face = 0;
        return false;
    }
    return true;
}

static float ssao_rand01(unsigned int* state) {
    *state = *state * 1664525u + 1013904223u;
    return (float)((*state >> 8) & 0xffffffu) * (1.0f / 16777216.0f);
}

static void renderer_init_ssao(Renderer* r) {
    r->ssao_noise_tex = 0;
    r->ssao_fbo = 0;
    r->ssao_tex = 0;
    r->ssao_internal = 0;
    r->ssao_format = 0;
    r->ssao_type = 0;
    memset(&r->ssao_shader, 0, sizeof(r->ssao_shader));
    memset(&r->ssao_blur_shader, 0, sizeof(r->ssao_blur_shader));
    if (!r || r->is_studio || !r->scene_fbo || !r->fog_quad_vao) return;
#if PW_MOBILE
    return;
#else
    if (!shader_compile_asset(&r->ssao_shader, "ssao")) {
        memset(&r->ssao_shader, 0, sizeof(r->ssao_shader));
        return;
    }
    unsigned int p = r->ssao_shader.program;
    r->ssao_u_depth = glGetUniformLocation(p, "u_depth");
    r->ssao_u_noise = glGetUniformLocation(p, "u_noise");
    r->ssao_u_projection = glGetUniformLocation(p, "u_projection");
    r->ssao_u_inv_projection = glGetUniformLocation(p, "u_inv_projection");
    r->ssao_u_noise_scale = glGetUniformLocation(p, "u_noise_scale");
    r->ssao_u_samples = glGetUniformLocation(p, "u_samples[0]");
    r->ssao_u_radius = glGetUniformLocation(p, "u_radius");
    r->ssao_u_bias = glGetUniformLocation(p, "u_bias");

    unsigned int seed = 0xA341316Cu;
    for (int i = 0; i < 24; i++) {
        Vec3 s = {
            ssao_rand01(&seed) * 2.0f - 1.0f,
            ssao_rand01(&seed) * 2.0f - 1.0f,
            ssao_rand01(&seed)
        };
        s = vec3_normalize(s);
        float scale = (float)(i + 1) / 24.0f;
        scale = 0.1f + scale * scale * 0.9f;
        s = vec3_scale(s, scale);
        r->ssao_kernel[i * 3 + 0] = s.x;
        r->ssao_kernel[i * 3 + 1] = s.y;
        r->ssao_kernel[i * 3 + 2] = s.z;
    }

    unsigned char pix[16 * 3];
    for (int i = 0; i < 16; i++) {
        pix[i * 3 + 0] = (unsigned char)(ssao_rand01(&seed) * 255.0f);
        pix[i * 3 + 1] = (unsigned char)(ssao_rand01(&seed) * 255.0f);
        pix[i * 3 + 2] = 128;
    }
    glGenTextures(1, &r->ssao_noise_tex);
    glBindTexture(GL_TEXTURE_2D, r->ssao_noise_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, pix);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!shader_compile_asset(&r->ssao_blur_shader, "ssao_blur")) {
        shader_destroy(&r->ssao_shader);
        memset(&r->ssao_shader, 0, sizeof(r->ssao_shader));
        glDeleteTextures(1, &r->ssao_noise_tex);
        r->ssao_noise_tex = 0;
        return;
    }
    unsigned int bp = r->ssao_blur_shader.program;
    r->ssao_blur_u_ssao = glGetUniformLocation(bp, "u_ssao");
    r->ssao_blur_u_depth = glGetUniformLocation(bp, "u_depth");
    r->ssao_blur_u_inv_projection = glGetUniformLocation(bp, "u_inv_projection");

    int tw = r->scene_w > 0 ? r->scene_w : r->canvas_width;
    int th = r->scene_h > 0 ? r->scene_h : r->canvas_height;
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    tw = (tw + 1) / 2;
    th = (th + 1) / 2;
    glGenTextures(1, &r->ssao_tex);
    glBindTexture(GL_TEXTURE_2D, r->ssao_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &r->ssao_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->ssao_fbo);
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }
    static const struct { unsigned intern; unsigned fmt; unsigned type; } ssao_fmts[] = {
        { GL_R16F, GL_RED, GL_FLOAT },
        { GL_RGBA16F, GL_RGBA, GL_FLOAT },
        { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE }
    };
    bool ssao_ok = false;
    for (int fi = 0; fi < 3; fi++) {
        glBindTexture(GL_TEXTURE_2D, r->ssao_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ssao_fmts[fi].intern, tw, th, 0,
                     ssao_fmts[fi].fmt, ssao_fmts[fi].type, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->ssao_tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            r->ssao_internal = ssao_fmts[fi].intern;
            r->ssao_format = ssao_fmts[fi].fmt;
            r->ssao_type = ssao_fmts[fi].type;
            ssao_ok = true;
            break;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!ssao_ok) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &r->ssao_fbo);
        glDeleteTextures(1, &r->ssao_tex);
        r->ssao_fbo = 0;
        r->ssao_tex = 0;
        shader_destroy(&r->ssao_shader);
        shader_destroy(&r->ssao_blur_shader);
        memset(&r->ssao_shader, 0, sizeof(r->ssao_shader));
        memset(&r->ssao_blur_shader, 0, sizeof(r->ssao_blur_shader));
        glDeleteTextures(1, &r->ssao_noise_tex);
        r->ssao_noise_tex = 0;
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
}

static bool renderer_alloc_present_fbo(Renderer* r, int width, int height) {
    if (!r || width < 1 || height < 1) return false;
    if (r->studio_fbo) return true;
    glGenFramebuffers(1, &r->studio_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->studio_fbo);
    glGenTextures(1, &r->studio_texture);
    glBindTexture(GL_TEXTURE_2D, r->studio_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->studio_texture, 0);
    glGenRenderbuffers(1, &r->studio_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, r->studio_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r->studio_rbo);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (!ok) {
        if (r->studio_fbo) glDeleteFramebuffers(1, &r->studio_fbo);
        if (r->studio_texture) glDeleteTextures(1, &r->studio_texture);
        if (r->studio_rbo) glDeleteRenderbuffers(1, &r->studio_rbo);
        r->studio_fbo = 0;
        r->studio_texture = 0;
        r->studio_rbo = 0;
        return false;
    }
    return true;
}

void renderer_set_host_present(Renderer* r, bool on) {
    if (!r) return;
    r->host_present = on;
    if (on)
        renderer_alloc_present_fbo(r, r->canvas_width > 0 ? r->canvas_width : 1280,
                                   r->canvas_height > 0 ? r->canvas_height : 720);
}

unsigned int renderer_host_fbo(const Renderer* r) {
    return r ? r->studio_fbo : 0;
}

unsigned int renderer_host_color_tex(const Renderer* r) {
    return r ? r->studio_texture : 0;
}

static unsigned int renderer_window_fbo(const Renderer* r) {
#if defined(PW_QUEST) && defined(VR) && defined(PW_OPENXR)

    unsigned xr = vr_openxr_game_fbo();
    if (xr) return xr;
#endif
    if (r && r->host_present && r->studio_fbo) return r->studio_fbo;
    return 0;
}

static void renderer_bind_window_fbo(const Renderer* r) {
    unsigned fbo = renderer_window_fbo(r);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (fbo) {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }
}

bool renderer_init(Renderer* r, int canvas_width, int canvas_height, bool studio) {
    unsigned char* old_voxel_occ = r ? r->voxel_occ : NULL;
    unsigned char* old_voxel_vis = r ? r->voxel_vis : NULL;
    memset(r, 0, sizeof(Renderer));
    free(old_voxel_occ);
    free(old_voxel_vis);
    r->canvas_width = canvas_width;
    r->canvas_height = canvas_height;
    r->is_studio = studio;
    r->curve_tess_quality = studio ? 4 : 2;

    r->light_dir = (Vec3){-0.70f, -0.58f, 0.40f};
    r->light_color = (Vec3){1.0f, 0.98f, 0.94f};
    for (int i = 0; i < RENDERER_MAX_GLOW_LIGHTS; i++) {
        r->glow_lights[i].id = ENTITY_INVALID;
        r->glow_lights[i].intensity = 0.0f;
        r->glow_lights[i].score = 0.0f;
    }
    r->glow_light_last_time = 0.0;
    r->mesh_fx_uv[0] = 0.0f;
    r->mesh_fx_uv[1] = 0.0f;
    r->mesh_fx_uv[2] = 1.0f;
    r->mesh_fx_uv[3] = 1.0f;

    r->clear_r = 0.53f;
    r->clear_g = 0.81f;
    r->clear_b = 0.92f;
    r->clear_a = 1.0f;
    r->render_scale = 1.0f;
    r->scene_w = canvas_width;
    r->scene_h = canvas_height;
    r->scale_active = false;
    r->scale_fbo = 0;
    r->scale_color_tex = 0;
    r->scale_depth_rb = 0;

#ifdef __EMSCRIPTEN__

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.antialias = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        return false;
    }
    emscripten_webgl_make_context_current(ctx);

    emscripten_webgl_enable_extension(ctx, "EXT_color_buffer_float");
    emscripten_webgl_enable_extension(ctx, "OES_texture_float_linear");
#elif !PW_USE_GLES

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {

        if (glCreateShader == NULL || glCreateProgram == NULL) {
            return false;
        }

        PW_ERR(ERR_GENERIC, "glew is a fricking idiot bro like wdym \"%s\" just let me use opengl\n", glewGetErrorString(err));
    }

    while (glGetError() != GL_NO_ERROR) {}
    glEnable(GL_MULTISAMPLE);
#endif

    if (!shader_compile_asset(&r->shader, "world")) {
        PW_ERR(ERR_SHADER, "No shaders :(\n");
        return false;
    }

    r->glow_leak_mode = 1;
    r->glow_light_max = RENDERER_MAX_GLOW_LIGHTS;
    r->ssao_enabled = 1;
    debug_ensure_init();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(r->clear_r, r->clear_g, r->clear_b, 1.0f);
    glViewport(0, 0, canvas_width, canvas_height);

    texture_manager_init(&r->textures);

    {
        r->shadow_map_size = 2048;
        r->shadows_enabled = true;
        r->shadow_soft = 0;
        r->extra_caster_count = 0;
        r->shadow_skip_count = 0;
        r->shadow_id_tex = 0;
        r->shadow_depth_rb = 0;
        r->shadow_fbo = 0;
        r->shadow_shader.program = 0;
        r->shadow_shader_u_id = -1;
        r->shadow_shader_u_face_expand = -1;
        r->shadow_shader_u_id_packed = -1;
        r->shadow_shader_u_light_dir = -1;
        r->shadow_shader_u_z_span = -1;
        r->current_shadow_id = 0;
        r->shadow_id_packed = false;
        r->shadow_depth_bias = 0.0f;
        r->shadow_esm_c = 14.0f;
        if (r->shadow_range <= 0.0f) r->shadow_range = 50.0f;

        r->shadow_near_fbo = 0;
        r->shadow_near_id_tex = 0;
        r->shadow_near_depth_rb = 0;
        r->shadow_near_range = 40.0f;
        r->shadow_z_span = 400.0f;
        r->shadow_z_span_near = 300.0f;
        r->light_space_matrix = mat4_identity();
        r->light_space_near = mat4_identity();
        r->shadow_view = mat4_identity();
        r->shadow_proj = mat4_identity();
        r->shadow_view_near = mat4_identity();
        r->shadow_proj_near = mat4_identity();

        if (shader_compile_asset(&r->shadow_shader, "shadow")) {
            r->shadow_shader_u_id = r->shadow_shader.u_shadow_id;
            r->shadow_shader_u_face_expand = glGetUniformLocation(r->shadow_shader.program, "u_caster_group");
            r->shadow_shader_u_z_span = glGetUniformLocation(r->shadow_shader.program, "u_evsm_c");
            int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
            if (!renderer_setup_sun_shadow_map(r, map)) {
                r->shadows_enabled = false;
                if (r->shadow_fbo) { glDeleteFramebuffers(1, &r->shadow_fbo); r->shadow_fbo = 0; }
                if (r->shadow_id_tex) { glDeleteTextures(1, &r->shadow_id_tex); r->shadow_id_tex = 0; }
                if (r->shadow_depth_tex) { glDeleteTextures(1, &r->shadow_depth_tex); r->shadow_depth_tex = 0; }
                if (r->shadow_near_fbo) { glDeleteFramebuffers(1, &r->shadow_near_fbo); r->shadow_near_fbo = 0; }
                if (r->shadow_near_id_tex) { glDeleteTextures(1, &r->shadow_near_id_tex); r->shadow_near_id_tex = 0; }
                if (r->shadow_near_depth_tex) { glDeleteTextures(1, &r->shadow_near_depth_tex); r->shadow_near_depth_tex = 0; }
            }
        } else {
            PW_ERR(ERR_SHADER, "Sun shadow shader failed to compile\n");
            r->shadows_enabled = false;
        }

        if (!r->shadow_id_tex) {
            glGenTextures(1, &r->shadow_id_tex);
            glBindTexture(GL_TEXTURE_2D, r->shadow_id_tex);
            float px[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (!r->shadow_depth_stub) {
            glGenTextures(1, &r->shadow_depth_stub);
            glBindTexture(GL_TEXTURE_2D, r->shadow_depth_stub);
            float px[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        r->voxel_dim = RENDERER_VOXEL_DIM;
        r->voxel_size = RENDERER_VOXEL_SIZE;
        r->voxel_range = 0.5f * RENDERER_VOXEL_SIZE * (float)RENDERER_VOXEL_DIM;
        r->voxel_enabled = false;
        r->voxel_origin = (Vec3){0.0f, 0.0f, 0.0f};
        {
            int cells = RENDERER_VOXEL_DIM * RENDERER_VOXEL_DIM * RENDERER_VOXEL_DIM;
            r->voxel_occ = (unsigned char*)malloc((size_t)cells);
            r->voxel_vis = (unsigned char*)malloc((size_t)cells);
            glGenTextures(1, &r->voxel_tex);
            glBindTexture(GL_TEXTURE_3D, r->voxel_tex);
            unsigned char vis_one = 255;
            glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, 1, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &vis_one);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_3D, 0);
        }
    }

    if (r->is_studio) {
        if (!renderer_alloc_present_fbo(r, canvas_width, canvas_height))
            return false;
    }

    r->fog_world_pass = false;
    r->scene_fbo = 0;
    r->scene_color_tex = 0;
    r->scene_depth_tex = 0;
    r->scene_fog_depth_tex = 0;
    r->fog_quad_vao = 0;
    r->fog_quad_vbo = 0;
    memset(&r->fog_shader, 0, sizeof(r->fog_shader));
    if (!r->is_studio) {
        glGenFramebuffers(1, &r->scene_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);

        glGenTextures(1, &r->scene_color_tex);
        glBindTexture(GL_TEXTURE_2D, r->scene_color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, canvas_width, canvas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->scene_color_tex, 0);

        glGenTextures(1, &r->scene_fog_depth_tex);
        glBindTexture(GL_TEXTURE_2D, r->scene_fog_depth_tex);
#if PW_USE_GLES

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, canvas_width, canvas_height, 0, GL_RGBA, GL_FLOAT, NULL);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, canvas_width, canvas_height, 0, GL_RED, GL_FLOAT, NULL);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, r->scene_fog_depth_tex, 0);

        glGenTextures(1, &r->scene_depth_tex);
        glBindTexture(GL_TEXTURE_2D, r->scene_depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, canvas_width, canvas_height,
                     0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->scene_depth_tex, 0);

        {
            GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, bufs);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &r->scene_fbo);
            glDeleteTextures(1, &r->scene_color_tex);
            glDeleteTextures(1, &r->scene_fog_depth_tex);
            glDeleteTextures(1, &r->scene_depth_tex);
            r->scene_fbo = 0;
            r->scene_color_tex = 0;
            r->scene_fog_depth_tex = 0;
            r->scene_depth_tex = 0;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (r->scene_fbo && shader_compile_asset(&r->fog_shader, "fog")) {
            r->fog_u_color = glGetUniformLocation(r->fog_shader.program, "u_scene_color");
            r->fog_u_depth = glGetUniformLocation(r->fog_shader.program, "u_scene_depth");
            r->fog_u_fog_depth = glGetUniformLocation(r->fog_shader.program, "u_fog_depth");
            r->fog_u_clear_color = glGetUniformLocation(r->fog_shader.program, "u_clear_color");
            r->fog_u_near = glGetUniformLocation(r->fog_shader.program, "u_near");
            r->fog_u_far = glGetUniformLocation(r->fog_shader.program, "u_far");
            r->fog_u_start = glGetUniformLocation(r->fog_shader.program, "u_fog_start");
            r->fog_u_end = glGetUniformLocation(r->fog_shader.program, "u_fog_end");

            float quad[] = { -1.f, -1.f,  1.f, -1.f,  -1.f, 1.f,  1.f, 1.f };
            glGenVertexArrays(1, &r->fog_quad_vao);
            glGenBuffers(1, &r->fog_quad_vbo);
            glBindVertexArray(r->fog_quad_vao);
            glBindBuffer(GL_ARRAY_BUFFER, r->fog_quad_vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glBindVertexArray(0);
            renderer_init_ssao(r);
        } else if (r->scene_fbo) {
            glDeleteFramebuffers(1, &r->scene_fbo);
            glDeleteTextures(1, &r->scene_color_tex);
            glDeleteTextures(1, &r->scene_fog_depth_tex);
            glDeleteTextures(1, &r->scene_depth_tex);
            r->scene_fbo = 0;
            r->scene_color_tex = 0;
            r->scene_fog_depth_tex = 0;
            r->scene_depth_tex = 0;
        }

        glGenFramebuffers(1, &r->scale_fbo);
        glGenTextures(1, &r->scale_color_tex);
        glGenRenderbuffers(1, &r->scale_depth_rb);
        glBindTexture(GL_TEXTURE_2D, r->scale_color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, canvas_width, canvas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, r->scale_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, canvas_width, canvas_height);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scale_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->scale_color_tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r->scale_depth_rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &r->scale_fbo);
            glDeleteTextures(1, &r->scale_color_tex);
            glDeleteRenderbuffers(1, &r->scale_depth_rb);
            r->scale_fbo = 0;
            r->scale_color_tex = 0;
            r->scale_depth_rb = 0;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    return true;
}

static void renderer_bind_draw_target(Renderer* r) {
    int vw = r->canvas_width;
    int vh = r->canvas_height;
    if (r->is_studio) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->studio_fbo);
    } else if (r->fog_world_pass && r->scene_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
#if PW_USE_GLES

        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
#else
        GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, bufs);
#endif
        vw = r->scene_w > 0 ? r->scene_w : r->canvas_width;
        vh = r->scene_h > 0 ? r->scene_h : r->canvas_height;
    } else if (r->scale_active && r->scale_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->scale_fbo);
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
        vw = r->scene_w > 0 ? r->scene_w : r->canvas_width;
        vh = r->scene_h > 0 ? r->scene_h : r->canvas_height;
    } else {
        renderer_bind_window_fbo(r);
    }
    glViewport(0, 0, vw, vh);
}

void renderer_shutdown(Renderer* r) {
    shader_destroy(&r->shader);
    if (r->shadow_shader.program)
        shader_destroy(&r->shadow_shader);
    if (r->fog_shader.program)
        shader_destroy(&r->fog_shader);
    if (r->ssao_shader.program)
        shader_destroy(&r->ssao_shader);
    if (r->ssao_blur_shader.program)
        shader_destroy(&r->ssao_blur_shader);
    if (r->ssao_noise_tex) glDeleteTextures(1, &r->ssao_noise_tex);
    if (r->ssao_fbo) glDeleteFramebuffers(1, &r->ssao_fbo);
    if (r->ssao_tex) glDeleteTextures(1, &r->ssao_tex);
    if (r->shadow_fbo) glDeleteFramebuffers(1, &r->shadow_fbo);
    if (r->shadow_id_tex) glDeleteTextures(1, &r->shadow_id_tex);
    if (r->shadow_depth_tex) glDeleteTextures(1, &r->shadow_depth_tex);
    if (r->shadow_depth_stub) glDeleteTextures(1, &r->shadow_depth_stub);
    if (r->shadow_depth_rb) glDeleteRenderbuffers(1, &r->shadow_depth_rb);
    if (r->shadow_near_fbo) glDeleteFramebuffers(1, &r->shadow_near_fbo);
    if (r->shadow_near_id_tex) glDeleteTextures(1, &r->shadow_near_id_tex);
    if (r->shadow_near_depth_tex) glDeleteTextures(1, &r->shadow_near_depth_tex);
    if (r->shadow_near_depth_rb) glDeleteRenderbuffers(1, &r->shadow_near_depth_rb);
    if (r->glow_shadow_fbo) glDeleteFramebuffers(1, &r->glow_shadow_fbo);
    if (r->glow_shadow_depth_tex) glDeleteTextures(1, &r->glow_shadow_depth_tex);
    for (int i = 0; i < RENDERER_MAX_GLOW_SHADOW_LIGHTS; i++) {
        if (r->glow_shadow_tex[i]) glDeleteTextures(1, &r->glow_shadow_tex[i]);
    }
    if (r->scene_fbo) glDeleteFramebuffers(1, &r->scene_fbo);
    if (r->scene_color_tex) glDeleteTextures(1, &r->scene_color_tex);
    if (r->scene_fog_depth_tex) glDeleteTextures(1, &r->scene_fog_depth_tex);
    if (r->scene_depth_tex) glDeleteTextures(1, &r->scene_depth_tex);
    if (r->fog_quad_vbo) glDeleteBuffers(1, &r->fog_quad_vbo);
    if (r->fog_quad_vao) glDeleteVertexArrays(1, &r->fog_quad_vao);
    if (r->scale_fbo) glDeleteFramebuffers(1, &r->scale_fbo);
    if (r->scale_color_tex) glDeleteTextures(1, &r->scale_color_tex);
    if (r->scale_depth_rb) glDeleteRenderbuffers(1, &r->scale_depth_rb);
    if (r->voxel_tex) glDeleteTextures(1, &r->voxel_tex);
    free(r->voxel_occ);
    free(r->voxel_vis);
    r->voxel_occ = NULL;
    r->voxel_vis = NULL;
    free(r->shadow_pose_pos);
    free(r->shadow_pose_hint);
    free(r->shadow_pose_rad);
    free(r->shadow_pose_on);
    free(r->shadow_pose_mesh);
    r->shadow_pose_pos = NULL;
    r->shadow_pose_hint = NULL;
    r->shadow_pose_rad = NULL;
    r->shadow_pose_on = NULL;
    r->shadow_pose_mesh = NULL;
    renderer_invalidate_curve_meshes();
    memset(r, 0, sizeof(Renderer));
}

bool renderer_recreate_gl(Renderer* r) {
    if (!r) return false;
    int w = r->canvas_width > 0 ? r->canvas_width : 1280;
    int h = r->canvas_height > 0 ? r->canvas_height : 720;
    bool studio = r->is_studio;
    Vec3 light_dir = r->light_dir;
    Vec3 light_color = r->light_color;
    float clear_r = r->clear_r, clear_g = r->clear_g, clear_b = r->clear_b, clear_a = r->clear_a;
    float shadow_range = r->shadow_range;
    float shadow_near_range = r->shadow_near_range;
    int shadow_soft = r->shadow_soft;
    int glow_leak = r->glow_leak_mode;
    int glow_light_max = r->glow_light_max;
    int ssao_enabled = r->ssao_enabled;
    float render_scale = r->render_scale;
    bool fog_enabled = r->fog_enabled;
    bool shadows_enabled = r->shadows_enabled;
    bool voxel_enabled = r->voxel_enabled;
    float voxel_range = r->voxel_range;
    int curve_tess_quality = r->curve_tess_quality;

    renderer_invalidate_curve_meshes();

    if (!renderer_init(r, w, h, studio)) return false;

    r->light_dir = light_dir;
    r->light_color = light_color;
    r->clear_r = clear_r;
    r->clear_g = clear_g;
    r->clear_b = clear_b;
    r->clear_a = clear_a;
    if (shadow_range > 0.0f) r->shadow_range = shadow_range;
    if (shadow_near_range > 0.0f) r->shadow_near_range = shadow_near_range;
    r->shadow_soft = shadow_soft;
    r->glow_leak_mode = glow_leak;
    r->glow_light_max = glow_light_max;
    r->ssao_enabled = ssao_enabled;
    r->fog_enabled = fog_enabled;
    r->shadows_enabled = shadows_enabled;
    r->voxel_enabled = voxel_enabled;
    if (voxel_range > 1.0f)
        renderer_set_voxel_range(r, voxel_range);
    r->curve_tess_quality = curve_tess_quality;
    if (render_scale > 0.01f && render_scale < 0.999f)
        renderer_set_render_scale(r, render_scale);
    return true;
}

static void renderer_realloc_scene_targets(Renderer* r, int w, int h) {
    if (!r || r->is_studio || w <= 0 || h <= 0) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (r->scene_color_tex && r->scene_depth_tex) {
        glBindTexture(GL_TEXTURE_2D, r->scene_color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (r->scene_fog_depth_tex) {
            glBindTexture(GL_TEXTURE_2D, r->scene_fog_depth_tex);
#if PW_USE_GLES
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, NULL);
#endif
        }
        glBindTexture(GL_TEXTURE_2D, r->scene_depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h,
                     0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (r->ssao_tex) {
        int sw = w > 0 ? w : 1;
        int sh = h > 0 ? h : 1;
        sw = (sw + 1) / 2;
        sh = (sh + 1) / 2;
        unsigned intern = r->ssao_internal ? r->ssao_internal : (unsigned)GL_RGBA8;
        unsigned fmt = r->ssao_format ? r->ssao_format : (unsigned)GL_RGBA;
        unsigned type = r->ssao_type ? r->ssao_type : (unsigned)GL_UNSIGNED_BYTE;
        glBindTexture(GL_TEXTURE_2D, r->ssao_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)intern, sw, sh, 0, fmt, type, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (r->scale_color_tex && r->scale_depth_rb) {
        glBindTexture(GL_TEXTURE_2D, r->scale_color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, r->scale_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    if (r->scene_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {

            r->fog_enabled = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    if (r->scale_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->scale_fbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            r->scale_active = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    if (prev_fbo > 0)
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
}

void renderer_set_render_scale(Renderer* r, float scale) {
    if (!r || r->is_studio) return;
    if (scale < 0.28f) scale = 0.28f;
    if (scale > 1.0f) scale = 1.0f;
    int sw = (int)((float)r->canvas_width * scale + 0.5f);
    int sh = (int)((float)r->canvas_height * scale + 0.5f);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;

    if (fabsf(r->render_scale - scale) < 0.001f && r->scene_w == sw && r->scene_h == sh)
        return;
    r->render_scale = scale;
    r->scene_w = sw;
    r->scene_h = sh;
    renderer_realloc_scene_targets(r, sw, sh);
}

void renderer_present_scaled_3d(Renderer* r) {
    if (!r || !r->scale_active || !r->scale_fbo) return;
    unsigned int dest = renderer_window_fbo(r);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->scale_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest);
    glBlitFramebuffer(0, 0, r->scene_w, r->scene_h,
                      0, 0, r->canvas_width, r->canvas_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    renderer_bind_window_fbo(r);
    glViewport(0, 0, r->canvas_width, r->canvas_height);
    r->scale_active = false;
}

void renderer_resize(Renderer* r, int width, int height) {

    if (width <= 0 || height <= 0) return;
    if (width == r->canvas_width && height == r->canvas_height) return;

    r->canvas_width = width;
    r->canvas_height = height;

    float scale = r->render_scale > 0.01f ? r->render_scale : 1.0f;
    r->scene_w = (int)((float)width * scale + 0.5f);
    r->scene_h = (int)((float)height * scale + 0.5f);
    if (r->scene_w < 1) r->scene_w = 1;
    if (r->scene_h < 1) r->scene_h = 1;

    glViewport(0, 0, width, height);

    if (r->is_studio || r->host_present) {
        if (r->studio_texture) {
            glBindTexture(GL_TEXTURE_2D, r->studio_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (r->studio_rbo) {
            glBindRenderbuffer(GL_RENDERBUFFER, r->studio_rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
    }
    if (!r->is_studio)
        renderer_realloc_scene_targets(r, r->scene_w, r->scene_h);
}

void renderer_begin_frame(Renderer* r) {

    r->fog_world_pass = false;
    r->scale_active = (!r->is_studio && r->render_scale < 0.999f && r->scale_fbo != 0);
    renderer_bind_draw_target(r);

    if (r->is_studio) {

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);

    glClearColor(r->clear_r, r->clear_g, r->clear_b, r->clear_a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void renderer_begin_world_pass(Renderer* r) {
    if (!r || r->is_studio) return;

    bool need_rt = r->scene_fbo && (r->fog_enabled || (r->ssao_enabled && r->ssao_shader.program));
    if (!need_rt) {

        r->fog_world_pass = false;
        renderer_bind_draw_target(r);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
        glClear(GL_DEPTH_BUFFER_BIT);
        return;
    }
    r->fog_world_pass = true;
    renderer_bind_draw_target(r);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (r->scene_fog_depth_tex) {
        const float ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glClearBufferfv(GL_COLOR, 1, ones);
    }
}

void renderer_set_shadow_id(Renderer* r, uint32_t shadow_id) {
    if (!r) return;
    r->current_shadow_id = shadow_id;
}

void renderer_clear_fx_lights(Renderer* r) {
    if (!r) return;
    r->fx_light_count = 0;
    r->acc_glow_count = 0;
}

void renderer_reset_lights(Renderer* r) {
    if (!r) return;
    renderer_clear_fx_lights(r);
    memset(r->fx_lights, 0, sizeof(r->fx_lights));
    memset(r->acc_glow_lights, 0, sizeof(r->acc_glow_lights));
    for (int i = 0; i < RENDERER_MAX_GLOW_LIGHTS; i++) {
        r->glow_lights[i].id = ENTITY_INVALID;
        r->glow_lights[i].pos = (Vec3){0.0f, 0.0f, 0.0f};
        r->glow_lights[i].color = (Vec3){0.0f, 0.0f, 0.0f};
        r->glow_lights[i].range = 0.0f;
        r->glow_lights[i].intensity = 0.0f;
        r->glow_lights[i].score = 0.0f;
    }
    r->glow_shadow_count = 0;
    r->glow_shadow_cache_ok = false;
    r->glow_cache_extras = 0;
    r->glow_cache_extra0 = (Vec3){0.0f, 0.0f, 0.0f};
    for (int i = 0; i < RENDERER_MAX_GLOW_SHADOW_LIGHTS; i++) {
        r->glow_cache_id[i] = ENTITY_INVALID;
        r->glow_cache_pos[i] = (Vec3){0.0f, 0.0f, 0.0f};
        r->glow_cache_range[i] = 0.0f;
    }
}

void renderer_add_fx_light(Renderer* r, Vec3 pos, Vec3 color, float range, float intensity) {
    if (!r || intensity <= 0.001f || range <= 0.01f) return;
    if (r->fx_light_count >= RENDERER_MAX_FX_LIGHTS) return;
    RendererFxLight* fx = &r->fx_lights[r->fx_light_count++];
    fx->pos = pos;
    fx->color = color;
    fx->range = range;
    fx->intensity = intensity;
}

void renderer_add_acc_glow_light(Renderer* r, Vec3 pos, Vec3 color, float range, float intensity) {
    if (!r || intensity <= 0.001f || range <= 0.01f) return;
    if (r->acc_glow_count >= RENDERER_MAX_ACC_GLOW_LIGHTS) return;
    RendererFxLight* fx = &r->acc_glow_lights[r->acc_glow_count++];
    fx->pos = pos;
    fx->color = color;
    fx->range = range;
    fx->intensity = intensity;
}

void renderer_set_mesh_fx(Renderer* r, float glow, int additive, int no_cull) {
    if (!r) return;
    r->mesh_fx_glow = glow;
    r->mesh_fx_additive = additive ? 1 : 0;
    r->mesh_fx_no_cull = no_cull;
}

void renderer_set_mesh_uv_rect(Renderer* r, float u, float v, float su, float sv) {
    if (!r) return;
    r->mesh_fx_uv[0] = u;
    r->mesh_fx_uv[1] = v;
    r->mesh_fx_uv[2] = su;
    r->mesh_fx_uv[3] = sv;
}

void renderer_shadow_skip_reset(Renderer* r) {
    if (r) r->shadow_skip_count = 0;
}

void renderer_shadow_skip_add(Renderer* r, EntityID id) {
    if (!r || id == ENTITY_INVALID) return;
    if (r->shadow_skip_count >= RENDERER_SHADOW_SKIP_MAX) return;
    for (int i = 0; i < r->shadow_skip_count; i++) {
        if (r->shadow_skip[i] == id) return;
    }
    r->shadow_skip[r->shadow_skip_count++] = id;
}

static bool renderer_shadow_skip_has(const Renderer* r, EntityID id) {
    if (!r || id == ENTITY_INVALID) return false;
    for (int i = 0; i < r->shadow_skip_count; i++) {
        if (r->shadow_skip[i] == id) return true;
    }
    return false;
}

static float shadow_model_radius(const GPUMesh* mesh, const Mat4* m);

static void shadow_draw_mesh(Renderer* r, const GPUMesh* mesh, const Mat4* model) {
    if (!mesh || !model || !mesh->vao || mesh->index_count == 0) return;
    if (r->shadow_shader.u_shadow_id >= 0)
        glUniform1ui(r->shadow_shader.u_shadow_id, r->current_shadow_id);
    glUniformMatrix4fv(r->shadow_shader.u_model, 1, GL_FALSE, model->m);
    glBindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count, GL_UNSIGNED_INT, 0);
}

static void shadow_expand_view_aabb(const Mat4* view, Vec3 pos, float radius,
                                    float* xmin, float* xmax, float* ymin, float* ymax,
                                    float* zmin, float* zmax, int* n) {
    Vec4 lp = mat4_mul_vec4(*view, (Vec4){ pos.x, pos.y, pos.z, 1.0f });
    float x0 = lp.x - radius, x1 = lp.x + radius;
    float y0 = lp.y - radius, y1 = lp.y + radius;
    float z = -lp.z;
    float z0 = z - radius, z1 = z + radius;
    if (*n == 0) {
        *xmin = x0; *xmax = x1;
        *ymin = y0; *ymax = y1;
        *zmin = z0; *zmax = z1;
    } else {
        if (x0 < *xmin) *xmin = x0;
        if (x1 > *xmax) *xmax = x1;
        if (y0 < *ymin) *ymin = y0;
        if (y1 > *ymax) *ymax = y1;
        if (z0 < *zmin) *zmin = z0;
        if (z1 > *zmax) *zmax = z1;
    }
    (*n)++;
}

static int shadow_collect_view_aabb(Renderer* r, const Scene* scene, const Mat4* view,
                                    float xy_lim, float z_lim,
                                    float* xmin, float* xmax, float* ymin, float* ymax,
                                    float* zmin, float* zmax) {
    int n = 0;
    if (xy_lim < 8.0f) xy_lim = 8.0f;
    if (z_lim < xy_lim) z_lim = xy_lim;
    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh || !e->mesh->vao) continue;
        if (e->material.alpha < 0.99f) continue;
        if (renderer_shadow_skip_has(r, e->id)) continue;
        Mat4 model = scene_get_world_matrix(scene, e->id);
        Vec3 pos = { model.m[12], model.m[13], model.m[14] };
        float radius = shadow_model_radius(e->mesh, &model);
        Vec4 lp = mat4_mul_vec4(*view, (Vec4){ pos.x, pos.y, pos.z, 1.0f });
        float pad = radius + 1.0f;
        if (lp.x + pad < -xy_lim || lp.x - pad > xy_lim) continue;
        if (lp.y + pad < -xy_lim || lp.y - pad > xy_lim) continue;
        float vz = -lp.z;
        if (vz + pad < 0.0f || vz - pad > z_lim) continue;
        shadow_expand_view_aabb(view, pos, radius, xmin, xmax, ymin, ymax, zmin, zmax, &n);
    }
    for (int i = 0; i < r->extra_caster_count; i++) {
        const Mat4* m = &r->extra_casters[i].model;
        Vec3 pos = { m->m[12], m->m[13], m->m[14] };
        float radius = shadow_model_radius(r->extra_casters[i].mesh, m);
        Vec4 lp = mat4_mul_vec4(*view, (Vec4){ pos.x, pos.y, pos.z, 1.0f });
        float pad = radius + 1.0f;
        if (lp.x + pad < -xy_lim || lp.x - pad > xy_lim) continue;
        if (lp.y + pad < -xy_lim || lp.y - pad > xy_lim) continue;
        float vz = -lp.z;
        if (vz + pad < 0.0f || vz - pad > z_lim) continue;
        shadow_expand_view_aabb(view, pos, radius, xmin, xmax, ymin, ymax, zmin, zmax, &n);
    }
    return n;
}

static void shadow_build_cascade(Renderer* r, const Scene* scene, Vec3 focus, float range, int map,
                                 Mat4* view, Mat4* proj, Mat4* light_space, float* zspan,
                                 float* out_near, float* out_far, float* out_xy) {
    Vec3 ld = vec3_normalize(r->light_dir);

    float pullback = range * 4.0f;
    float near_z = 0.5f;
    float far_keep = pullback + range * 2.25f;
    float far_z = far_keep;
    Vec3 eye = {
        focus.x - ld.x * pullback,
        focus.y - ld.y * pullback,
        focus.z - ld.z * pullback
    };
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    if (fabsf(ld.y) > 0.92f) up = (Vec3){ 0.0f, 0.0f, 1.0f };
    *view = mat4_look_at(eye, focus, up);

    float xy = range;
    if (scene) {
        float xmin = 0, xmax = 0, ymin = 0, ymax = 0, zmin = 0, zmax = 0;
        int got = shadow_collect_view_aabb(r, scene, view, range, far_keep + range,
                                           &xmin, &xmax, &ymin, &ymax, &zmin, &zmax);
        if (got > 0) {
            float pad_z = 8.0f + range * 0.12f;
            float zn = zmin - pad_z;
            if (zn < 0.5f) zn = 0.5f;
            if (zn < far_keep - 2.0f)
                near_z = zn;
            if (zmax + pad_z > far_z)
                far_z = zmax + pad_z;
        }
    }

    float texel = (2.0f * xy) / (float)(map > 0 ? map : 2048);
    if (texel > 1e-5f) {
        float ls_x = view->m[12], ls_y = view->m[13];
        float snap_x = floorf(ls_x / texel + 0.5f) * texel;
        float snap_y = floorf(ls_y / texel + 0.5f) * texel;
        view->m[12] += snap_x - ls_x;
        view->m[13] += snap_y - ls_y;
    }
    *proj = mat4_ortho(-xy, xy, -xy, xy, near_z, far_z);
    if (zspan) *zspan = far_z - near_z;
    if (out_near) *out_near = near_z;
    if (out_far) *out_far = far_z;
    if (out_xy) *out_xy = xy;
    *light_space = mat4_multiply(*proj, *view);
}

static bool shadow_in_cascade(const Mat4* view, Vec3 pos, float radius,
                              float range, float near_z, float far_z) {
    Vec4 lp = mat4_mul_vec4(*view, (Vec4){ pos.x, pos.y, pos.z, 1.0f });
    float pad = radius + 0.5f;
    float xy = range + pad;
    if (lp.x < -xy || lp.x > xy || lp.y < -xy || lp.y > xy) return false;
    float vz = -lp.z;
    if (vz + pad < near_z || vz - pad > far_z) return false;
    return true;
}

static void shadow_set_caster_group(Renderer* r, float group) {
    if (r->shadow_shader_u_face_expand >= 0)
        glUniform1f(r->shadow_shader_u_face_expand, group);
}

static void shadow_set_evsm_c(Renderer* r) {
    if (r->shadow_shader_u_z_span >= 0)
        glUniform1f(r->shadow_shader_u_z_span, r->shadow_esm_c);
}

static void shadow_clear_moments(Renderer* r) {
    float c = r->shadow_esm_c;
    if (c > 0.5f) {
        float e = expf(c);
        glClearColor(e, e * e, 0.0f, 1.0f);
    } else {
        glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    }
}

typedef struct {
    float u0, v0, u1, v1;
    bool any;
} ShadowDirtyUV;

static float shadow_model_radius(const GPUMesh* mesh, const Mat4* m) {
    float sx = sqrtf(m->m[0] * m->m[0] + m->m[1] * m->m[1] + m->m[2] * m->m[2]);
    float sy = sqrtf(m->m[4] * m->m[4] + m->m[5] * m->m[5] + m->m[6] * m->m[6]);
    float sz = sqrtf(m->m[8] * m->m[8] + m->m[9] * m->m[9] + m->m[10] * m->m[10]);
    float sm = sx;
    if (sy > sm) sm = sy;
    if (sz > sm) sm = sz;
    return mesh ? mesh->bounding_radius * sm : sm;
}

static void shadow_dirty_init(ShadowDirtyUV* d) {
    d->u0 = d->v0 = 1.0f;
    d->u1 = d->v1 = 0.0f;
    d->any = false;
}

static bool shadow_sphere_uv(const Mat4* ls, float range, Vec3 pos, float radius,
                             float* u0, float* v0, float* u1, float* v1) {
    Vec4 c = mat4_mul_vec4(*ls, (Vec4){ pos.x, pos.y, pos.z, 1.0f });
    if (fabsf(c.w) < 1e-8f) return false;
    float ux = c.x / c.w * 0.5f + 0.5f;
    float vy = c.y / c.w * 0.5f + 0.5f;
    float ruv = (radius + 1.0f) / (2.0f * (range > 1.0f ? range : 1.0f));
    if (ruv < 0.003f) ruv = 0.003f;
    *u0 = ux - ruv;
    *v0 = vy - ruv;
    *u1 = ux + ruv;
    *v1 = vy + ruv;
    return *u1 > 0.0f && *u0 < 1.0f && *v1 > 0.0f && *v0 < 1.0f;
}

static void shadow_dirty_add_sphere(ShadowDirtyUV* d, const Mat4* ls, float range,
                                    Vec3 pos, float radius) {
    float u0, v0, u1, v1;

    if (!shadow_sphere_uv(ls, range, pos, radius + 2.5f, &u0, &v0, &u1, &v1)) return;
    if (!d->any) {
        d->u0 = u0;
        d->v0 = v0;
        d->u1 = u1;
        d->v1 = v1;
        d->any = true;
        return;
    }
    if (u0 < d->u0) d->u0 = u0;
    if (v0 < d->v0) d->v0 = v0;
    if (u1 > d->u1) d->u1 = u1;
    if (v1 > d->v1) d->v1 = v1;
}

static bool shadow_dirty_too_big(const ShadowDirtyUV* d) {
    if (!d->any) return false;
    float w = d->u1 - d->u0;
    float h = d->v1 - d->v0;
    if (w < 0.0f) w = 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (w > 1.0f) w = 1.0f;
    if (h > 1.0f) h = 1.0f;
    return w * h > 0.28f;
}

static bool shadow_in_dirty_uv(Vec3 pos, float radius, const Mat4* ls, float range,
                               const ShadowDirtyUV* d) {
    if (!d || !d->any) return true;
    float u0, v0, u1, v1;
    if (!shadow_sphere_uv(ls, range, pos, radius, &u0, &v0, &u1, &v1)) return false;
    return u0 < d->u1 && u1 > d->u0 && v0 < d->v1 && v1 > d->v0;
}

static bool shadow_focus_stuck(Vec3 a, Vec3 b, float range) {
    float lim = range * 0.28f;
    if (lim < 6.0f) lim = 6.0f;
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= lim * lim;
}

static bool shadow_extra_slot_moved(const Renderer* r, int i) {
    const Mat4* m = &r->extra_casters[i].model;
    float dx = m->m[12] - r->shadow_extra_prev[i].pos.x;
    float dy = m->m[13] - r->shadow_extra_prev[i].pos.y;
    float dz = m->m[14] - r->shadow_extra_prev[i].pos.z;
    if (r->extra_casters[i].mesh != r->shadow_extra_prev[i].mesh) return true;
    return dx * dx + dy * dy + dz * dz > 0.0004f;
}

static bool shadow_pose_ensure(Renderer* r, uint32_t n) {
    if (n == 0) return true;
    if (r->shadow_pose_pos && r->shadow_pose_hint && r->shadow_pose_rad &&
        r->shadow_pose_on && r->shadow_pose_mesh && r->shadow_pose_cap >= n)
        return true;
    uint32_t cap = n < 256u ? 256u : n;
    if (r->shadow_pose_cap && cap < r->shadow_pose_cap * 2u)
        cap = r->shadow_pose_cap * 2u;
    Vec3* pos = (Vec3*)realloc(r->shadow_pose_pos, (size_t)cap * sizeof(Vec3));
    Vec3* hint = (Vec3*)realloc(r->shadow_pose_hint, (size_t)cap * sizeof(Vec3));
    float* rad = (float*)realloc(r->shadow_pose_rad, (size_t)cap * sizeof(float));
    unsigned char* on = (unsigned char*)realloc(r->shadow_pose_on, (size_t)cap);
    const GPUMesh** mesh = (const GPUMesh**)realloc(r->shadow_pose_mesh, (size_t)cap * sizeof(const GPUMesh*));
    if (!pos || !hint || !rad || !on || !mesh) {
        if (pos) r->shadow_pose_pos = pos;
        if (hint) r->shadow_pose_hint = hint;
        if (rad) r->shadow_pose_rad = rad;
        if (on) r->shadow_pose_on = on;
        if (mesh) r->shadow_pose_mesh = mesh;
        return false;
    }
    if (cap > r->shadow_pose_cap) {
        memset(pos + r->shadow_pose_cap, 0, (size_t)(cap - r->shadow_pose_cap) * sizeof(Vec3));
        memset(hint + r->shadow_pose_cap, 0, (size_t)(cap - r->shadow_pose_cap) * sizeof(Vec3));
        memset(rad + r->shadow_pose_cap, 0, (size_t)(cap - r->shadow_pose_cap) * sizeof(float));
        memset(on + r->shadow_pose_cap, 0, (size_t)(cap - r->shadow_pose_cap));
        memset(mesh + r->shadow_pose_cap, 0, (size_t)(cap - r->shadow_pose_cap) * sizeof(const GPUMesh*));
    }
    r->shadow_pose_pos = pos;
    r->shadow_pose_hint = hint;
    r->shadow_pose_rad = rad;
    r->shadow_pose_on = on;
    r->shadow_pose_mesh = mesh;
    r->shadow_pose_cap = cap;
    return true;
}

static bool shadow_collect_dirty(Renderer* r, const Scene* scene, const Mat4* ls, float range,
                                 ShadowDirtyUV* dirty) {
    shadow_dirty_init(dirty);
    int n = r->extra_caster_count;
    if (n > RENDERER_MAX_EXTRA_CASTERS) n = RENDERER_MAX_EXTRA_CASTERS;
    bool extras_moved = (n != r->shadow_extra_prev_count);
    if (!extras_moved) {
        for (int i = 0; i < n; i++) {
            if (shadow_extra_slot_moved(r, i)) { extras_moved = true; break; }
        }
    }

    if (extras_moved) {
        for (int i = 0; i < n; i++) {
            const Mat4* m = &r->extra_casters[i].model;
            Vec3 pos = { m->m[12], m->m[13], m->m[14] };
            shadow_dirty_add_sphere(dirty, ls, range, pos,
                                    shadow_model_radius(r->extra_casters[i].mesh, m));
        }
        for (int i = 0; i < r->shadow_extra_prev_count; i++) {
            shadow_dirty_add_sphere(dirty, ls, range, r->shadow_extra_prev[i].pos,
                                    r->shadow_extra_prev[i].radius);
        }
    }

    if (!r->shadow_pose_pos || !r->shadow_pose_mesh || r->shadow_pose_n != scene->count)
        return false;

    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        bool now = e->active && e->mesh && e->mesh->vao && e->material.alpha >= 0.99f &&
                   !renderer_shadow_skip_has(r, e->id);
        bool was = r->shadow_pose_on[i] != 0;
        if (!now && !was) continue;
        Vec3 pos = r->shadow_pose_pos[i];
        Vec3 hint = r->shadow_pose_hint[i];
        float radius = r->shadow_pose_rad[i];
        const GPUMesh* tess = NULL;
        if (now) {
            Mat4 model = scene_get_world_matrix(scene, e->id);
            pos = (Vec3){ model.m[12], model.m[13], model.m[14] };
            hint = (Vec3){ model.m[0], model.m[5], model.m[10] };
            tess = renderer_entity_tess_mesh(r, e);
            radius = shadow_model_radius(tess ? tess : e->mesh, &model);
        }
        bool moved = now != was;
        if (!moved && now) {
            Vec3 op = r->shadow_pose_pos[i];
            Vec3 oh = r->shadow_pose_hint[i];
            float dx = pos.x - op.x, dy = pos.y - op.y, dz = pos.z - op.z;
            float hx = hint.x - oh.x, hy = hint.y - oh.y, hz = hint.z - oh.z;
            float dr = radius - r->shadow_pose_rad[i];
            moved = dx * dx + dy * dy + dz * dz > 0.0009f ||
                    hx * hx + hy * hy + hz * hz > 1.0e-6f ||
                    dr * dr > 0.0004f ||
                    tess != r->shadow_pose_mesh[i];
        }
        if (!moved) continue;
        if (was)
            shadow_dirty_add_sphere(dirty, ls, range, r->shadow_pose_pos[i],
                                    r->shadow_pose_rad[i]);
        if (now)
            shadow_dirty_add_sphere(dirty, ls, range, pos, radius);
    }
    return true;
}

static void shadow_snapshot_casters(Renderer* r, const Scene* scene) {
    int n = r->extra_caster_count;
    if (n > RENDERER_MAX_EXTRA_CASTERS) n = RENDERER_MAX_EXTRA_CASTERS;
    r->shadow_extra_prev_count = n;
    for (int i = 0; i < n; i++) {
        const Mat4* m = &r->extra_casters[i].model;
        r->shadow_extra_prev[i].pos = (Vec3){ m->m[12], m->m[13], m->m[14] };
        r->shadow_extra_prev[i].radius = shadow_model_radius(r->extra_casters[i].mesh, m);
        r->shadow_extra_prev[i].mesh = r->extra_casters[i].mesh;
    }
    if (!shadow_pose_ensure(r, scene->count)) return;
    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        bool now = e->active && e->mesh && e->mesh->vao && e->material.alpha >= 0.99f &&
                   !renderer_shadow_skip_has(r, e->id);
        r->shadow_pose_on[i] = now ? 1 : 0;
        if (!now) {
            r->shadow_pose_mesh[i] = NULL;
            continue;
        }
        Mat4 model = scene_get_world_matrix(scene, e->id);
        const GPUMesh* tess = renderer_entity_tess_mesh(r, e);
        r->shadow_pose_pos[i] = (Vec3){ model.m[12], model.m[13], model.m[14] };
        r->shadow_pose_hint[i] = (Vec3){ model.m[0], model.m[5], model.m[10] };
        r->shadow_pose_rad[i] = shadow_model_radius(tess ? tess : e->mesh, &model);
        r->shadow_pose_mesh[i] = tess;
    }
    r->shadow_pose_n = scene->count;
}

static void shadow_fill_begin(Renderer* r, unsigned int fbo, const Mat4* view, const Mat4* proj,
                              bool scissor, int sx, int sy, int sw, int sh) {
    int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, map, map);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (scissor && sw > 0 && sh > 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    shadow_clear_moments(r);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_use(&r->shadow_shader);
    glUniformMatrix4fv(r->shadow_shader.u_view, 1, GL_FALSE, view->m);
    glUniformMatrix4fv(r->shadow_shader.u_projection, 1, GL_FALSE, proj->m);
    shadow_set_evsm_c(r);
    shadow_set_caster_group(r, 0.0f);
}

static void shadow_fill_world(Renderer* r, const Scene* scene, const Mat4* view, const Mat4* ls,
                              float range, float near_z, float far_z,
                              const ShadowDirtyUV* dirty) {
    shadow_set_caster_group(r, 0.0f);
    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh || !e->mesh->vao) continue;
        if (e->material.alpha < 0.99f) continue;
        if (renderer_shadow_skip_has(r, e->id)) continue;

        Mat4 model = scene_get_world_matrix(scene, e->id);
        Vec3 pos = { model.m[12], model.m[13], model.m[14] };
        float radius = shadow_model_radius(e->mesh, &model);
        if (dirty && dirty->any && !shadow_in_dirty_uv(pos, radius + 3.0f, ls, range, dirty))
            continue;
        if (!shadow_in_cascade(view, pos, radius, range, near_z, far_z))
            continue;
        r->current_shadow_id = renderer_shadow_id_entity(e->id);
        shadow_draw_mesh(r, renderer_entity_tess_mesh(r, e), &model);
    }
    shadow_set_caster_group(r, 1.0f);
    for (int i = 0; i < r->extra_caster_count; i++) {
        const Mat4* m = &r->extra_casters[i].model;
        Vec3 pos = { m->m[12], m->m[13], m->m[14] };
        float radius = shadow_model_radius(r->extra_casters[i].mesh, m);
        if (dirty && dirty->any && !shadow_in_dirty_uv(pos, radius + 3.0f, ls, range, dirty))
            continue;
        if (!shadow_in_cascade(view, pos, radius, range, near_z, far_z))
            continue;
        shadow_draw_mesh(r, r->extra_casters[i].mesh, m);
    }
}

static void shadow_uv_to_scissor(const ShadowDirtyUV* d, int map, int pad,
                                int* sx, int* sy, int* sw, int* sh) {
    float pu = (float)pad / (float)(map > 0 ? map : 1);
    float u0 = d->u0 - pu, v0 = d->v0 - pu, u1 = d->u1 + pu, v1 = d->v1 + pu;
    if (u0 < 0.0f) u0 = 0.0f;
    if (v0 < 0.0f) v0 = 0.0f;
    if (u1 > 1.0f) u1 = 1.0f;
    if (v1 > 1.0f) v1 = 1.0f;
    int x0 = (int)floorf(u0 * (float)map);
    int y0 = (int)floorf(v0 * (float)map);
    int x1 = (int)ceilf(u1 * (float)map);
    int y1 = (int)ceilf(v1 * (float)map);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > map) x1 = map;
    if (y1 > map) y1 = map;
    *sx = x0;
    *sy = y0;
    *sw = x1 - x0;
    *sh = y1 - y0;
}

static void shadow_run_cascade(Renderer* r, const Scene* scene, unsigned int fbo,
                               const Mat4* view, const Mat4* proj, const Mat4* ls,
                               float range, float near_z, float far_z,
                               const ShadowDirtyUV* dirty) {
    int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
    int sx = 0, sy = 0, sw = map, sh = map;
    bool scissor = false;
    if (dirty && dirty->any) {
        shadow_uv_to_scissor(dirty, map, 8, &sx, &sy, &sw, &sh);
        if (sw <= 0 || sh <= 0) return;
        if ((float)sw * (float)sh <= (float)map * (float)map * 0.28f)
            scissor = true;
        else
            dirty = NULL;
    }
    shadow_fill_begin(r, fbo, view, proj, scissor, sx, sy, sw, sh);
    shadow_fill_world(r, scene, view, ls, range, near_z, far_z, scissor ? dirty : NULL);
    glDisable(GL_SCISSOR_TEST);
}

static bool shadow_cascade_update(Renderer* r, const Scene* scene, Vec3 focus_pos, int map,
                                  unsigned int fbo, Mat4* view, Mat4* proj, Mat4* ls, float* zspan,
                                  Vec3* cache_focus, float* cache_r, float* cache_nz, float* cache_fz,
                                  bool* cache_ok, float range, bool stuck) {
    if (!stuck) {
        float near_z = 0.0f, far_z = 0.0f, fit_xy = range;
        shadow_build_cascade(r, scene, focus_pos, range, map, view, proj, ls, zspan,
                             &near_z, &far_z, &fit_xy);
        *cache_focus = focus_pos;
        *cache_r = range;
        *cache_nz = near_z;
        *cache_fz = far_z;
        *cache_ok = true;
        shadow_run_cascade(r, scene, fbo, view, proj, ls, fit_xy, near_z, far_z, NULL);
        return true;
    }
    ShadowDirtyUV dirty;
    float fit_xy = range;
    if (proj && fabsf(proj->m[0]) > 1e-8f)
        fit_xy = 1.0f / fabsf(proj->m[0]);
    if (!shadow_collect_dirty(r, scene, ls, fit_xy, &dirty)) {
        shadow_run_cascade(r, scene, fbo, view, proj, ls, fit_xy,
                           *cache_nz, *cache_fz, NULL);
        return true;
    }
    if (!dirty.any) return false;
    if (shadow_dirty_too_big(&dirty)) {
        shadow_run_cascade(r, scene, fbo, view, proj, ls, fit_xy,
                           *cache_nz, *cache_fz, NULL);
        return true;
    }
    shadow_run_cascade(r, scene, fbo, view, proj, ls, fit_xy,
                       *cache_nz, *cache_fz, &dirty);
    return true;
}

void renderer_shadow_pass(Renderer* r, const Scene* scene, Vec3 focus_pos) {
    if (!r || !scene || !r->shadows_enabled || !r->shadow_fbo || !r->shadow_id_tex ||
        !r->shadow_shader.program) {
        return;
    }

    float range = r->shadow_range > 1.0f ? r->shadow_range : 50.0f;
    int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
    Vec3 ld = vec3_normalize(r->light_dir);
    float ldx = ld.x - r->shadow_cache_ld.x;
    float ldy = ld.y - r->shadow_cache_ld.y;
    float ldz = ld.z - r->shadow_cache_ld.z;
    bool light_moved = (ldx * ldx + ldy * ldy + ldz * ldz) > 1.0e-5f;
    if (light_moved || map != r->shadow_cache_map ||
        fabsf(range - r->shadow_cache_range) > 0.5f) {
        r->shadow_cache_far_ok = false;
        r->shadow_cache_near_ok = false;
    }

    float near_r = r->shadow_near_range;
    bool want_near = near_r > 4.0f && r->shadow_near_fbo && r->shadow_near_id_tex &&
                     r->shadow_near_depth_tex;
    if (want_near && near_r > range * 0.55f) near_r = range * 0.55f;
    if (want_near && r->shadow_cache_near_ok &&
        fabsf(near_r - r->shadow_cache_near_r) > 0.5f)
        r->shadow_cache_near_ok = false;

    bool far_stuck = r->shadow_cache_far_ok &&
                     shadow_focus_stuck(focus_pos, r->shadow_cache_focus_far, range);
    bool filled = shadow_cascade_update(r, scene, focus_pos, map, r->shadow_fbo,
                          &r->shadow_view, &r->shadow_proj, &r->light_space_matrix,
                          &r->shadow_z_span, &r->shadow_cache_focus_far, &r->shadow_cache_range,
                          &r->shadow_cache_far_nz, &r->shadow_cache_far_fz,
                          &r->shadow_cache_far_ok, range, far_stuck);
    r->shadow_cache_ld = ld;
    r->shadow_cache_map = map;

    if (want_near) {
        bool near_stuck = r->shadow_cache_near_ok &&
                          shadow_focus_stuck(focus_pos, r->shadow_cache_focus_near, near_r);
        filled = shadow_cascade_update(r, scene, focus_pos, map, r->shadow_near_fbo,
                              &r->shadow_view_near, &r->shadow_proj_near, &r->light_space_near,
                              &r->shadow_z_span_near, &r->shadow_cache_focus_near,
                              &r->shadow_cache_near_r, &r->shadow_cache_near_nz,
                              &r->shadow_cache_near_fz, &r->shadow_cache_near_ok,
                              near_r, near_stuck) || filled;
    } else {
        r->light_space_near = r->light_space_matrix;
        r->shadow_cache_near_ok = false;
    }

    if (filled)
        shadow_snapshot_casters(r, scene);

    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r->clear_r, r->clear_g, r->clear_b, 1.0f);
    glCullFace(GL_BACK);
    renderer_bind_draw_target(r);
}

static Vec3 glow_extra_key(const Renderer* r) {
    Vec3 k = { 0.0f, 0.0f, 0.0f };
    int n = r->extra_caster_count;
    if (n <= 0) return k;
    const Mat4* a = &r->extra_casters[0].model;
    k.x = a->m[12];
    k.y = a->m[13];
    k.z = a->m[14];
    if (n > 1) {
        const Mat4* b = &r->extra_casters[n - 1].model;
        k.x += b->m[12];
        k.y += b->m[13];
        k.z += b->m[14];
    }
    return k;
}

static bool glow_in_range(Vec3 pos, float radius, Vec3 lamp, float range) {
    float dx = pos.x - lamp.x, dy = pos.y - lamp.y, dz = pos.z - lamp.z;
    float lim = range + radius;
    return dx * dx + dy * dy + dz * dz <= lim * lim;
}

static bool glow_lit_by_active(Vec3 pos, float radius, const float* glow_pos,
                               const float* glow_range, int glow_count) {
    for (int i = 0; i < glow_count; i++) {
        Vec3 lamp = { glow_pos[i * 3 + 0], glow_pos[i * 3 + 1], glow_pos[i * 3 + 2] };
        if (glow_in_range(pos, radius, lamp, glow_range[i]))
            return true;
    }
    return false;
}

static void glow_draw_casters(Renderer* r, const Scene* scene, EntityID skip,
                              Vec3 lamp, float range, int skip_extras) {
    shadow_set_caster_group(r, 0.0f);
    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh || !e->mesh->vao) continue;
        if (e->material.alpha < 0.99f) continue;
        if (e->id == skip) continue;
        if (renderer_shadow_skip_has(r, e->id)) continue;
        Mat4 model = scene_get_world_matrix(scene, e->id);
        Vec3 pos = { model.m[12], model.m[13], model.m[14] };
        float radius = shadow_model_radius(e->mesh, &model);
        if (!glow_in_range(pos, radius, lamp, range)) continue;
        r->current_shadow_id = renderer_shadow_id_entity(e->id);
        shadow_draw_mesh(r, renderer_entity_tess_mesh(r, e), &model);
    }
    if (skip_extras) return;
    shadow_set_caster_group(r, 1.0f);
    for (int i = 0; i < r->extra_caster_count; i++) {
        const Mat4* m = &r->extra_casters[i].model;
        Vec3 pos = { m->m[12], m->m[13], m->m[14] };
        float radius = shadow_model_radius(r->extra_casters[i].mesh, m);
        if (!glow_in_range(pos, radius, lamp, range)) continue;
        shadow_draw_mesh(r, r->extra_casters[i].mesh, m);
    }
}

static void renderer_glow_evsm_pass(Renderer* r, const Scene* scene,
                                    const EntityID* ids, const Vec3* pos,
                                    const float* range, const unsigned char* acc_flag,
                                    int light_n) {
    int n = light_n;
    if (n > RENDERER_MAX_GLOW_SHADOW_LIGHTS) n = RENDERER_MAX_GLOW_SHADOW_LIGHTS;
    if (n < 0) n = 0;

    if (!r || !scene || !r->glow_shadow_fbo || !r->shadow_shader.program ||
        r->glow_shadow_face < 16 || n <= 0) {
        if (r) {
            r->glow_shadow_count = 0;
            r->glow_shadow_cache_ok = false;
            r->extra_caster_count = 0;
        }
        return;
    }

    static const Vec3 k_fwd[6] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, {  0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, {  0.0f,  0.0f, -1.0f }
    };
    static const Vec3 k_up[6] = {
        { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 0.0f,  0.0f, 1.0f }, { 0.0f,  0.0f, -1.0f },
        { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }
    };

    Vec3 extra0 = glow_extra_key(r);
    bool same = r->glow_shadow_cache_ok && r->glow_shadow_count == n &&
                r->glow_cache_extras == r->extra_caster_count;
    if (same) {
        float dx = extra0.x - r->glow_cache_extra0.x;
        float dy = extra0.y - r->glow_cache_extra0.y;
        float dz = extra0.z - r->glow_cache_extra0.z;
        if (dx * dx + dy * dy + dz * dz > 0.0004f)
            same = false;
    }
    if (same) {
        for (int i = 0; i < n; i++) {
            if (r->glow_cache_id[i] != ids[i]) { same = false; break; }
            float dx = r->glow_cache_pos[i].x - pos[i].x;
            float dy = r->glow_cache_pos[i].y - pos[i].y;
            float dz = r->glow_cache_pos[i].z - pos[i].z;
            if (dx * dx + dy * dy + dz * dz > 0.0025f) { same = false; break; }
            if (fabsf(r->glow_cache_range[i] - range[i]) > 0.5f) { same = false; break; }
        }
    }
    if (same) {
        r->extra_caster_count = 0;
        return;
    }

    GLint prev_fbo = 0;
    GLint prev_vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    int face_px = r->glow_shadow_face;
    glBindFramebuffer(GL_FRAMEBUFFER, r->glow_shadow_fbo);
#if !PW_USE_GLES
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
#else
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }
#endif
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);

    shader_use(&r->shadow_shader);
    shadow_set_evsm_c(r);

    for (int li = 0; li < n; li++) {
        if (!r->glow_shadow_tex[li]) continue;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               r->glow_shadow_tex[li], 0);
        Vec3 lamp = pos[li];
        float far_z = range[li];
        if (far_z < 2.0f) far_z = 2.0f;
        float near_z = 0.08f;
        if (near_z > far_z * 0.04f) near_z = far_z * 0.04f;
        if (near_z < 0.04f) near_z = 0.04f;
        Mat4 proj = mat4_perspective(90.0f, 1.0f, near_z, far_z);
        glUniformMatrix4fv(r->shadow_shader.u_projection, 1, GL_FALSE, proj.m);

        for (int face = 0; face < 6; face++) {
            Vec3 target = vec3_add(lamp, k_fwd[face]);
            Mat4 view = mat4_look_at(lamp, target, k_up[face]);
            r->glow_ls[li * 6 + face] = mat4_multiply(proj, view);

            int col = face % 3;
            int row = face / 3;
            int sx = col * face_px;
            int sy = row * face_px;
            glViewport(sx, sy, face_px, face_px);
            glScissor(sx, sy, face_px, face_px);
            shadow_clear_moments(r);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(r->shadow_shader.u_view, 1, GL_FALSE, view.m);
            glow_draw_casters(r, scene, ids[li], lamp, far_z,
                              acc_flag && acc_flag[li]);
        }

        r->glow_cache_id[li] = ids[li];
        r->glow_cache_pos[li] = lamp;
        r->glow_cache_range[li] = range[li];
    }

    r->glow_shadow_count = n;
    r->glow_cache_extras = r->extra_caster_count;
    r->glow_cache_extra0 = extra0;
    r->glow_shadow_cache_ok = true;
    r->extra_caster_count = 0;

    glBindVertexArray(0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r->clear_r, r->clear_g, r->clear_b, 1.0f);
    glCullFace(GL_BACK);
    renderer_bind_draw_target(r);
    {
        GLint now_fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &now_fbo);
        if (now_fbo != prev_fbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
            glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        }
    }
}

static int voxel_clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float voxel_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void voxel_local_cell_half(const Mat4* inv, float hs, float* hx, float* hy, float* hz) {
    *hx = hs * (fabsf(inv->m[0]) + fabsf(inv->m[4]) + fabsf(inv->m[8]));
    *hy = hs * (fabsf(inv->m[1]) + fabsf(inv->m[5]) + fabsf(inv->m[9]));
    *hz = hs * (fabsf(inv->m[2]) + fabsf(inv->m[6]) + fabsf(inv->m[10]));
}

static int voxel_mesh_shape(const GPUMesh* mesh) {
    if (!mesh) return 0;
    if (mesh->prim_kind == 1) return 1;
    if (mesh->prim_kind == 2) return 2;
    unsigned ic = mesh->index_count;
    if (ic >= 2000) return 1;
    if (ic >= 100) return 2;
    return 0;
}

static bool voxel_cell_hits_part(int shape, Vec4 lp, float hx, float hy, float hz,
                                 float xmin, float xmax, float ymin, float ymax,
                                 float zmin, float zmax) {
    float mx = 0.5f * (xmin + xmax);
    float my = 0.5f * (ymin + ymax);
    float mz = 0.5f * (zmin + zmax);
    if (shape == 1) {
        float rx = fmaxf(xmax - mx, mx - xmin);
        float ry = fmaxf(ymax - my, my - ymin);
        float rz = fmaxf(zmax - mz, mz - zmin);
        float qx = voxel_clampf(mx, lp.x - hx, lp.x + hx) - mx;
        float qy = voxel_clampf(my, lp.y - hy, lp.y + hy) - my;
        float qz = voxel_clampf(mz, lp.z - hz, lp.z + hz) - mz;
        float invrx = (rx > 1e-6f) ? 1.0f / rx : 0.0f;
        float invry = (ry > 1e-6f) ? 1.0f / ry : 0.0f;
        float invrz = (rz > 1e-6f) ? 1.0f / rz : 0.0f;
        float ux = qx * invrx, uy = qy * invry, uz = qz * invrz;
        return (ux * ux + uy * uy + uz * uz) <= 1.0f;
    }
    if (shape == 2) {
        float R = 0.5f * fmaxf(xmax - xmin, zmax - zmin);
        float H = 0.5f * (ymax - ymin);
        float y0 = lp.y - hy, y1 = lp.y + hy;
        if (y1 < my - H || y0 > my + H) return false;
        float qx = voxel_clampf(mx, lp.x - hx, lp.x + hx) - mx;
        float qz = voxel_clampf(mz, lp.z - hz, lp.z + hz) - mz;
        return (qx * qx + qz * qz) <= R * R;
    }
    if (lp.x + hx < xmin || lp.x - hx > xmax) return false;
    if (lp.y + hy < ymin || lp.y - hy > ymax) return false;
    if (lp.z + hz < zmin || lp.z - hz > zmax) return false;
    return true;
}

void renderer_set_voxel_range(Renderer* r, float range) {
    if (!r || r->is_studio) return;
    if (range < 24.0f) range = 24.0f;
    if (range > 160.0f) range = 160.0f;
    float size = r->voxel_size > 0.1f ? r->voxel_size : RENDERER_VOXEL_SIZE;
    int dim = (int)ceilf(2.0f * range / size);
    if (dim < RENDERER_VOXEL_DIM_MIN) dim = RENDERER_VOXEL_DIM_MIN;
    if (dim > RENDERER_VOXEL_DIM_MAX) dim = RENDERER_VOXEL_DIM_MAX;
    r->voxel_range = range;
    r->voxel_size = size;
    if (dim == r->voxel_dim && r->voxel_occ && r->voxel_vis) return;
    r->voxel_origin_ok = false;

    size_t n = (size_t)dim * (size_t)dim * (size_t)dim;
    unsigned char* occ = (unsigned char*)malloc(n);
    unsigned char* vis = (unsigned char*)malloc(n);
    if (!occ || !vis) {
        free(occ);
        free(vis);
        return;
    }
    free(r->voxel_occ);
    free(r->voxel_vis);
    r->voxel_occ = occ;
    r->voxel_vis = vis;
    r->voxel_dim = dim;
}

void renderer_voxel_update(Renderer* r, const Scene* scene, Vec3 focus_pos) {
    if (!r || !scene || !r->voxel_enabled || !r->voxel_tex || !r->voxel_occ || !r->voxel_vis)
        return;

    const int dim = r->voxel_dim > 1 ? r->voxel_dim : RENDERER_VOXEL_DIM;
    const float size = r->voxel_size > 0.1f ? r->voxel_size : RENDERER_VOXEL_SIZE;
    const int cells = dim * dim * dim;
    const float extent = size * (float)dim;

    Vec3 origin;
    origin.x = floorf(focus_pos.x / size) * size - extent * 0.5f;
    origin.y = floorf(focus_pos.y / size) * size - extent * 0.5f;
    origin.z = floorf(focus_pos.z / size) * size - extent * 0.5f;
    if (r->voxel_origin_ok &&
        origin.x == r->voxel_origin.x &&
        origin.y == r->voxel_origin.y &&
        origin.z == r->voxel_origin.z)
        return;
    r->voxel_origin = origin;
    r->voxel_origin_ok = true;
    r->voxel_dim = dim;
    r->voxel_size = size;

    memset(r->voxel_occ, 0, (size_t)cells);

    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh) continue;
        if (e->material.alpha < 0.99f) continue;
        if (renderer_shadow_skip_has(r, e->id)) continue;

        Mat4 world = scene_get_world_matrix(scene, e->id);
        Mat4 inv_world = mat4_inverse(world);
        float minx = 1e30f, miny = 1e30f, minz = 1e30f;
        float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
        float xs[2] = { e->mesh->aabb_min[0], e->mesh->aabb_max[0] };
        float ys[2] = { e->mesh->aabb_min[1], e->mesh->aabb_max[1] };
        float zs[2] = { e->mesh->aabb_min[2], e->mesh->aabb_max[2] };
        for (int ix = 0; ix < 2; ix++) {
            for (int iy = 0; iy < 2; iy++) {
                for (int iz = 0; iz < 2; iz++) {
                    Vec4 lp = { xs[ix], ys[iy], zs[iz], 1.0f };
                    Vec4 wp = mat4_mul_vec4(world, lp);
                    if (wp.x < minx) minx = wp.x;
                    if (wp.y < miny) miny = wp.y;
                    if (wp.z < minz) minz = wp.z;
                    if (wp.x > maxx) maxx = wp.x;
                    if (wp.y > maxy) maxy = wp.y;
                    if (wp.z > maxz) maxz = wp.z;
                }
            }
        }

        int x0 = voxel_clampi((int)floorf((minx - origin.x) / size) - 1, 0, dim - 1);
        int y0 = voxel_clampi((int)floorf((miny - origin.y) / size) - 1, 0, dim - 1);
        int z0 = voxel_clampi((int)floorf((minz - origin.z) / size) - 1, 0, dim - 1);
        int x1 = voxel_clampi((int)floorf((maxx - origin.x) / size) + 1, 0, dim - 1);
        int y1 = voxel_clampi((int)floorf((maxy - origin.y) / size) + 1, 0, dim - 1);
        int z1 = voxel_clampi((int)floorf((maxz - origin.z) / size) + 1, 0, dim - 1);
        if (maxx < origin.x || maxy < origin.y || maxz < origin.z) continue;
        if (minx > origin.x + extent || miny > origin.y + extent || minz > origin.z + extent)
            continue;

        int shape = voxel_mesh_shape(e->mesh);
        float chx, chy, chz;
        voxel_local_cell_half(&inv_world, size * 0.5f, &chx, &chy, &chz);
        for (int z = z0; z <= z1; z++) {
            float cz = origin.z + ((float)z + 0.5f) * size;
            for (int y = y0; y <= y1; y++) {
                float cy = origin.y + ((float)y + 0.5f) * size;
                int row = dim * (y + dim * z);
                for (int x = x0; x <= x1; x++) {
                    float cx = origin.x + ((float)x + 0.5f) * size;
                    Vec4 lp = mat4_mul_vec4(inv_world, (Vec4){ cx, cy, cz, 1.0f });
                    if (!voxel_cell_hits_part(shape, lp, chx, chy, chz,
                                              xs[0], xs[1], ys[0], ys[1], zs[0], zs[1]))
                        continue;
                    r->voxel_occ[x + row] = 1;
                }
            }
        }
    }

    Vec3 ld = vec3_normalize(r->light_dir);
    int xs = (ld.x >= 0.0f) ? 1 : -1;
    int ys = (ld.y >= 0.0f) ? 1 : -1;
    int zs = (ld.z >= 0.0f) ? 1 : -1;
    int xb = (xs > 0) ? 0 : dim - 1;
    int xe = (xs > 0) ? dim : -1;
    int yb = (ys > 0) ? 0 : dim - 1;
    int ye = (ys > 0) ? dim : -1;
    int zb = (zs > 0) ? 0 : dim - 1;
    int ze = (zs > 0) ? dim : -1;
    float wx = fabsf(ld.x), wy = fabsf(ld.y), wz = fabsf(ld.z);

    for (int z = zb; z != ze; z += zs) {
        for (int y = yb; y != ye; y += ys) {
            for (int x = xb; x != xe; x += xs) {
                int i = x + dim * (y + dim * z);
                int me_occ = r->voxel_occ[i];
                float vsum = 0.0f, wsum = 0.0f;
                int px = x - xs;
                float tx;
                if (px < 0 || px >= dim) tx = 255.0f;
                else if (r->voxel_occ[px + dim * (y + dim * z)] && !me_occ) tx = 0.0f;
                else tx = (float)r->voxel_vis[px + dim * (y + dim * z)];
                vsum += tx * wx; wsum += wx;
                int py = y - ys;
                float ty;
                if (py < 0 || py >= dim) ty = 255.0f;
                else if (r->voxel_occ[x + dim * (py + dim * z)] && !me_occ) ty = 0.0f;
                else ty = (float)r->voxel_vis[x + dim * (py + dim * z)];
                vsum += ty * wy; wsum += wy;
                int pz = z - zs;
                float tz;
                if (pz < 0 || pz >= dim) tz = 255.0f;
                else if (r->voxel_occ[x + dim * (y + dim * pz)] && !me_occ) tz = 0.0f;
                else tz = (float)r->voxel_vis[x + dim * (y + dim * pz)];
                vsum += tz * wz; wsum += wz;
                if (wsum < 1e-6f)
                    r->voxel_vis[i] = 255;
                else
                    r->voxel_vis[i] = (unsigned char)(vsum / wsum + 0.5f);
            }
        }
    }

    glBindTexture(GL_TEXTURE_3D, r->voxel_tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, dim, dim, dim, 0, GL_RED, GL_UNSIGNED_BYTE, r->voxel_vis);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void renderer_shadow_cast_begin(Renderer* r) {
    if (!r || !r->shadows_enabled || !r->shadow_fbo || !r->shadow_shader.program) return;
    int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
    glViewport(0, 0, map, map);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    shader_use(&r->shadow_shader);
    glUniformMatrix4fv(r->shadow_shader.u_view, 1, GL_FALSE, r->shadow_view.m);
    glUniformMatrix4fv(r->shadow_shader.u_projection, 1, GL_FALSE, r->shadow_proj.m);
    shadow_set_evsm_c(r);
    shadow_set_caster_group(r, 1.0f);
    if (r->shadow_shader.u_shadow_id >= 0)
        glUniform1ui(r->shadow_shader.u_shadow_id, r->current_shadow_id);
}

void renderer_shadow_cast_begin_near(Renderer* r) {
    if (!r || !r->shadows_enabled || !r->shadow_near_fbo || !r->shadow_shader.program) return;
    int map = r->shadow_map_size > 0 ? r->shadow_map_size : 2048;
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_near_fbo);
    glViewport(0, 0, map, map);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    shader_use(&r->shadow_shader);
    glUniformMatrix4fv(r->shadow_shader.u_view, 1, GL_FALSE, r->shadow_view_near.m);
    glUniformMatrix4fv(r->shadow_shader.u_projection, 1, GL_FALSE, r->shadow_proj_near.m);
    shadow_set_evsm_c(r);
    shadow_set_caster_group(r, 1.0f);
    if (r->shadow_shader.u_shadow_id >= 0)
        glUniform1ui(r->shadow_shader.u_shadow_id, r->current_shadow_id);
}

void renderer_shadow_cast_mesh(Renderer* r, const GPUMesh* mesh, const Mat4* model) {
    if (!r || !r->shadows_enabled || !mesh || !model || !mesh->vao) return;
    if (r->extra_caster_count >= RENDERER_MAX_EXTRA_CASTERS) return;
    r->extra_casters[r->extra_caster_count].mesh = mesh;
    r->extra_casters[r->extra_caster_count].model = *model;
    r->extra_caster_count++;
}

void renderer_shadow_cast_end(Renderer* r) {
    if (!r || !r->shadows_enabled) return;
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glCullFace(GL_BACK);
    renderer_bind_draw_target(r);
}

static void renderer_bind_shader_samplers(Renderer* r) {
    if (!r) return;
    if (r->shader.u_texture >= 0)
        glUniform1i(r->shader.u_texture, 0);
    if (r->shader.u_shadow_map >= 0)
        glUniform1i(r->shader.u_shadow_map, 1);
    if (r->shader.u_shadow_map_near >= 0)
        glUniform1i(r->shader.u_shadow_map_near, 7);
    if (r->shader.u_normal_map >= 0)
        glUniform1i(r->shader.u_normal_map, 2);
    if (r->shader.u_inlet_map >= 0)
        glUniform1i(r->shader.u_inlet_map, 3);
    if (r->shader.u_mat_albedo >= 0)
        glUniform1i(r->shader.u_mat_albedo, 8);
    if (r->shader.u_mat_normal >= 0)
        glUniform1i(r->shader.u_mat_normal, 9);
    if (r->shader.u_mat_specular >= 0)
        glUniform1i(r->shader.u_mat_specular, 10);
    if (r->shader.u_voxel_map >= 0)
        glUniform1i(r->shader.u_voxel_map, 4);
    if (r->shader.u_glow_shadow_map0 >= 0)
        glUniform1i(r->shader.u_glow_shadow_map0, 5);
    if (r->shader.u_glow_shadow_map1 >= 0)
        glUniform1i(r->shader.u_glow_shadow_map1, 6);

    TextureID stub2d = texture_get_for_surface(&r->textures, SURFACE_STUD);
    if (stub2d == TEXTURE_INVALID)
        stub2d = texture_get_for_surface(&r->textures, SURFACE_INLET);

    unsigned int mom = r->shadow_id_tex ? r->shadow_id_tex :
                       (r->shadow_depth_stub ? r->shadow_depth_stub :
                        (stub2d != TEXTURE_INVALID ? stub2d : 0));
    unsigned int mom_n = r->shadow_near_id_tex ? r->shadow_near_id_tex : mom;

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mom);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, mom_n);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, stub2d != TEXTURE_INVALID ? stub2d : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, stub2d != TEXTURE_INVALID ? stub2d : 0);
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, stub2d != TEXTURE_INVALID ? stub2d : 0);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, stub2d != TEXTURE_INVALID ? stub2d : 0);
    {
        TextureID blk = r->textures.tex_black ? r->textures.tex_black : stub2d;
        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D, blk != TEXTURE_INVALID ? blk : 0);
    }
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, r->voxel_tex ? r->voxel_tex : 0);
    {
        unsigned int g0 = r->glow_shadow_tex[0] ? r->glow_shadow_tex[0] : mom;
        unsigned int g1 = r->glow_shadow_tex[1] ? r->glow_shadow_tex[1] : g0;
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, g0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, g1);
    }
    glActiveTexture(GL_TEXTURE0);
}

static void renderer_bind_part_material(Renderer* r, uint8_t mat) {
    if (!r) return;
    if (mat >= PART_MATERIAL_COUNT) mat = PART_MATERIAL_PLASTIC;
    if (r->shader.u_part_material >= 0)
        glUniform1i(r->shader.u_part_material, (int)mat);
    TextureID alb = texture_get_mat_albedo(&r->textures, (int)mat);
    TextureID nrm = texture_get_mat_normal(&r->textures, (int)mat);
    TextureID spec = texture_get_mat_specular(&r->textures, (int)mat);
    TextureID stub = texture_get_for_surface(&r->textures, SURFACE_STUD);
    if (stub == TEXTURE_INVALID)
        stub = texture_get_for_surface(&r->textures, SURFACE_INLET);
    TextureID black = r->textures.tex_black ? r->textures.tex_black : stub;
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, alb != TEXTURE_INVALID ? alb : stub);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, nrm != TEXTURE_INVALID ? nrm : stub);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, spec != TEXTURE_INVALID ? spec : black);
    glActiveTexture(GL_TEXTURE0);
}

static void renderer_bind_vsm_receive(Renderer* r) {
    if (!r) return;
    bool on = r->shadows_enabled && r->shadow_fbo && r->shadow_id_tex && r->shadow_shader.program;
    if (r->shader.u_shadow_enabled >= 0)
        glUniform1i(r->shader.u_shadow_enabled, on ? 1 : 0);
    if (r->shader.u_shadow_soft >= 0)
        glUniform1i(r->shader.u_shadow_soft, on ? r->shadow_soft : 0);
    if (r->shader.u_light_space >= 0)
        glUniformMatrix4fv(r->shader.u_light_space, 1, GL_FALSE, r->light_space_matrix.m);
    if (r->shader.u_shadow_exp >= 0)
        glUniform1f(r->shader.u_shadow_exp, r->shadow_esm_c);
    if (r->shader.u_shadow_depth_bias >= 0) {
        float near_r = (r->shadow_cache_near_ok && r->shadow_cache_near_r > 1.0f)
            ? r->shadow_cache_near_r
            : (r->shadow_near_range > 1.0f ? r->shadow_near_range : 32.0f);
        glUniform1f(r->shader.u_shadow_depth_bias, near_r);
    }
    if (r->shader.u_shadow_range >= 0) {
        float fade = r->shadow_range > 1.0f ? r->shadow_range : 50.0f;

        glUniform1f(r->shader.u_shadow_range, fade);
    }
    bool have_near = on && r->shadow_near_fbo && r->shadow_near_id_tex &&
                     r->shadow_near_range > 4.0f;
    if (r->shader.u_shadow_cascades >= 0)
        glUniform1i(r->shader.u_shadow_cascades, have_near ? 1 : 0);
    if (r->shader.u_light_space_near >= 0)
        glUniformMatrix4fv(r->shader.u_light_space_near, 1, GL_FALSE, r->light_space_near.m);
    unsigned int mom = r->shadow_id_tex ? r->shadow_id_tex : r->shadow_depth_stub;
    unsigned int mom_n = have_near ? r->shadow_near_id_tex : mom;
    if (r->shader.u_shadow_map >= 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mom);
        glUniform1i(r->shader.u_shadow_map, 1);
    }
    if (r->shader.u_shadow_map_near >= 0) {
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, mom_n);
        glUniform1i(r->shader.u_shadow_map_near, 7);
    }
    glActiveTexture(GL_TEXTURE0);
    if (r->shader.u_voxel_enabled >= 0)
        glUniform1i(r->shader.u_voxel_enabled, (r->voxel_enabled && !on) ? 1 : 0);
    if (r->shader.u_voxel_origin >= 0)
        glUniform3f(r->shader.u_voxel_origin, r->voxel_origin.x, r->voxel_origin.y, r->voxel_origin.z);
    if (r->shader.u_voxel_size >= 0)
        glUniform1f(r->shader.u_voxel_size, r->voxel_size > 0.1f ? r->voxel_size : RENDERER_VOXEL_SIZE);
    if (r->shader.u_voxel_dim >= 0)
        glUniform1i(r->shader.u_voxel_dim, r->voxel_dim > 1 ? r->voxel_dim : RENDERER_VOXEL_DIM);
    if (r->shader.u_voxel_range >= 0) {
        float half = (r->voxel_size > 0.1f ? r->voxel_size : RENDERER_VOXEL_SIZE) *
                     (float)(r->voxel_dim > 1 ? r->voxel_dim : RENDERER_VOXEL_DIM) * 0.5f;
        float fade_end = r->voxel_range > 1.0f ? r->voxel_range : half;
        if (fade_end > half * 0.98f) fade_end = half * 0.98f;
        glUniform1f(r->shader.u_voxel_range, fade_end);
    }
    if (r->shader.u_voxel_map >= 0) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_3D, r->voxel_tex ? r->voxel_tex : 0);
        glUniform1i(r->shader.u_voxel_map, 4);
        glActiveTexture(GL_TEXTURE0);
    }
    if (r->shader.u_shadow_face_ids >= 0)
        glUniform1i(r->shader.u_shadow_face_ids, 1);
}

void renderer_render_scene(Renderer* r, const Scene* scene, const Mat4* view, const Mat4* projection) {
    renderer_render_scene_ex(r, scene, view, projection, NULL, NULL);
}

void renderer_render_scene_ex(Renderer* r, const Scene* scene, const Mat4* view, const Mat4* projection,
                              RendererMidDrawFn mid, void* mid_user) {
    shader_use(&r->shader);

    glUniformMatrix4fv(r->shader.u_view, 1, GL_FALSE, view->m);
    glUniformMatrix4fv(r->shader.u_projection, 1, GL_FALSE, projection->m);

    glUniform3f(r->shader.u_light_dir, r->light_dir.x, r->light_dir.y, r->light_dir.z);
    glUniform3f(r->shader.u_light_color, r->light_color.x, r->light_color.y, r->light_color.z);

    {
        Mat4 inv_view = mat4_inverse(*view);
        glUniform3f(r->shader.u_camera_pos, inv_view.m[12], inv_view.m[13], inv_view.m[14]);
    }

    glUniform3f(r->shader.u_fog_color, r->clear_r, r->clear_g, r->clear_b);
    glUniform1f(r->shader.u_fog_start, PW_CAMERA_FAR * PW_FOG_START_FRAC);
    if (r->shader.u_face_mode >= 0)
        glUniform1i(r->shader.u_face_mode, 0);
    if (r->shader.u_part_shape >= 0)
        glUniform1i(r->shader.u_part_shape, 0);
    if (r->shader.u_shadow_face_ids >= 0)
        glUniform1i(r->shader.u_shadow_face_ids, 1);
    if (r->shader.u_part_size >= 0)
        glUniform3f(r->shader.u_part_size, 1.0f, 1.0f, 1.0f);
    renderer_bind_part_material(r, PART_MATERIAL_PLASTIC);
    glUniform1f(r->shader.u_fog_end, PW_CAMERA_FAR * PW_FOG_END_FRAC);
    glUniform1i(r->shader.u_fog_enabled, 0);

    renderer_bind_shader_samplers(r);
    renderer_bind_vsm_receive(r);

    float glow_pos[RENDERER_MAX_GLOW_LIGHTS * 3];
    float glow_col[RENDERER_MAX_GLOW_LIGHTS * 3];
    float glow_range[RENDERER_MAX_GLOW_LIGHTS];
    EntityID glow_ids[RENDERER_MAX_GLOW_LIGHTS];
    unsigned char glow_acc[RENDERER_MAX_GLOW_LIGHTS];
    int glow_count = 0;
    memset(glow_acc, 0, sizeof(glow_acc));
    {
        Vec3 focus = {0};
        Mat4 inv_view = mat4_inverse(*view);
        focus.x = inv_view.m[12]; focus.y = inv_view.m[13]; focus.z = inv_view.m[14];

        if (r->shadow_cache_far_ok)
            focus = r->shadow_cache_focus_far;

        typedef struct {
            EntityID id;
            Vec3 pos;
            Vec3 color;
            float range;
            float score;
        } GlowCand;
        GlowCand desired[RENDERER_MAX_GLOW_LIGHTS];
        float desired_score[RENDERER_MAX_GLOW_LIGHTS];
        int desired_n = 0;
        int glow_max = r->glow_light_max;
        if (glow_max < 1 || glow_max > RENDERER_MAX_GLOW_LIGHTS)
            glow_max = RENDERER_MAX_GLOW_LIGHTS;

        for (uint32_t i = 0; i < scene->count; i++) {
            const Entity* e = &scene->entities[i];
            if (!e->active || e->material.glow < 0.05f) continue;
            Mat4 model = scene_get_world_matrix(scene, e->id);
            Vec3 p = { model.m[12], model.m[13], model.m[14] };
            float sx = e->transform.scale.x, sy = e->transform.scale.y, sz = e->transform.scale.z;
            float size = sx;
            if (sy > size) size = sy;
            if (sz > size) size = sz;
            if (e->mesh) {
                float br = e->mesh->bounding_radius * size;
                if (br > size) size = br;
            }
            float g = e->material.glow;
            float range = size * (2.0f + g * 4.0f);
            if (range < 4.0f) range = 4.0f;
            if (r->glow_leak_mode >= 1) {
                float cap = RENDERER_GLOW_RANGE_CAP;
                if (glow_max <= 8) cap *= 0.5f;
                if (range > cap) range = cap;
            } else {
                range = size * (3.0f + g * 10.0f);
                if (range < 4.0f) range = 4.0f;
            }

            {
                Vec3 ax = { model.m[0], model.m[1], model.m[2] };
                Vec3 ay = { model.m[4], model.m[5], model.m[6] };
                Vec3 az = { model.m[8], model.m[9], model.m[10] };
                float hl[3] = {
                    0.5f * vec3_length(ax),
                    0.5f * vec3_length(ay),
                    0.5f * vec3_length(az)
                };
                int thin = 0;
                if (hl[1] < hl[thin]) thin = 1;
                if (hl[2] < hl[thin]) thin = 2;
                if (hl[thin] < 0.4f) {
                    Vec3 dir = (thin == 0) ? ax : (thin == 1) ? ay : az;
                    float dlen = vec3_length(dir);
                    if (dlen > 1e-5f) {
                        dir = vec3_scale(dir, 1.0f / dlen);
                        if (dir.y < 0.0f) dir = vec3_scale(dir, -1.0f);
                        p = vec3_add(p, vec3_scale(dir, hl[thin] + 0.12f));
                    }
                }
            }

            float dx = p.x - focus.x, dy = p.y - focus.y, dz = p.z - focus.z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            float score = g * range * range / (1.0f + dist);

            Vec3 c = e->material.color;
            float luma = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
            float sat = 2.35f;
            c.x = luma + (c.x - luma) * sat;
            c.y = luma + (c.y - luma) * sat;
            c.z = luma + (c.z - luma) * sat;
            if (c.x < 0.0f) c.x = 0.0f; if (c.x > 1.0f) c.x = 1.0f;
            if (c.y < 0.0f) c.y = 0.0f; if (c.y > 1.0f) c.y = 1.0f;
            if (c.z < 0.0f) c.z = 0.0f; if (c.z > 1.0f) c.z = 1.0f;
            float strength = g * RENDERER_GLOW_LIGHT_STRENGTH;
            c.x *= strength; c.y *= strength; c.z *= strength;

            int slot = desired_n;
            if (desired_n < glow_max) {
                desired_n++;
            } else {
                slot = 0;
                for (int j = 1; j < glow_max; j++) {
                    if (desired_score[j] < desired_score[slot]) slot = j;
                }
                if (score <= desired_score[slot]) continue;
            }
            desired_score[slot] = score;
            desired[slot].id = e->id;
            desired[slot].pos = p;
            desired[slot].color = c;
            desired[slot].range = range;
            desired[slot].score = score;
        }

        for (int a = 0; a < desired_n; a++) {
            int best = a;
            for (int b = a + 1; b < desired_n; b++) {
                if (desired[b].score > desired[best].score) best = b;
            }
            if (best != a) {
                GlowCand tmp = desired[a];
                desired[a] = desired[best];
                desired[best] = tmp;
                float ts = desired_score[a];
                desired_score[a] = desired_score[best];
                desired_score[best] = ts;
            }
        }

        if (r->glow_shadow_cache_ok) {
            int pin_n = r->glow_shadow_count;
            if (pin_n > RENDERER_MAX_GLOW_SHADOW_LIGHTS)
                pin_n = RENDERER_MAX_GLOW_SHADOW_LIGHTS;
            if (pin_n > desired_n) pin_n = desired_n;
            for (int s = 0; s < pin_n; s++) {
                EntityID keep = r->glow_cache_id[s];
                if (keep == ENTITY_INVALID) continue;
                int found = -1;
                for (int d = 0; d < desired_n; d++) {
                    if (desired[d].id == keep) { found = d; break; }
                }
                if (found < 0 || found == s) continue;
                GlowCand tmp = desired[s];
                desired[s] = desired[found];
                desired[found] = tmp;
                float ts = desired_score[s];
                desired_score[s] = desired_score[found];
                desired_score[found] = ts;
            }
        }

        for (int s = 0; s < RENDERER_MAX_GLOW_LIGHTS; s++) {
            r->glow_lights[s].id = ENTITY_INVALID;
            r->glow_lights[s].intensity = 0.0f;
            r->glow_lights[s].score = 0.0f;
        }
        for (int d = 0; d < desired_n; d++) {
            RendererGlowLight* gl = &r->glow_lights[d];
            gl->id = desired[d].id;
            gl->pos = desired[d].pos;
            gl->color = desired[d].color;
            gl->range = desired[d].range;
            gl->score = desired[d].score;
            gl->intensity = 1.0f;
            int o = glow_count++;
            glow_pos[o * 3 + 0] = gl->pos.x;
            glow_pos[o * 3 + 1] = gl->pos.y;
            glow_pos[o * 3 + 2] = gl->pos.z;
            glow_col[o * 3 + 0] = gl->color.x;
            glow_col[o * 3 + 1] = gl->color.y;
            glow_col[o * 3 + 2] = gl->color.z;
            glow_range[o] = gl->range;
            glow_ids[o] = gl->id;
        }
    }
    int brick_glow_n = glow_count;

    for (int ai = 0; ai < r->acc_glow_count; ai++) {
        const RendererFxLight* fx = &r->acc_glow_lights[ai];
        if (fx->intensity <= 0.001f) continue;
        int slot;
        if (glow_count < RENDERER_MAX_GLOW_LIGHTS) {
            slot = glow_count++;
        } else {
            slot = glow_count - 1;
        }
        glow_pos[slot * 3 + 0] = fx->pos.x;
        glow_pos[slot * 3 + 1] = fx->pos.y;
        glow_pos[slot * 3 + 2] = fx->pos.z;
        glow_col[slot * 3 + 0] = fx->color.x * fx->intensity;
        glow_col[slot * 3 + 1] = fx->color.y * fx->intensity;
        glow_col[slot * 3 + 2] = fx->color.z * fx->intensity;
        glow_range[slot] = fx->range;
        glow_ids[slot] = ENTITY_INVALID;
        glow_acc[slot] = 1;
    }

    for (int fi = 0; fi < r->fx_light_count; fi++) {
        const RendererFxLight* fx = &r->fx_lights[fi];
        if (fx->intensity <= 0.001f) continue;
        int slot;
        if (glow_count < RENDERER_MAX_GLOW_LIGHTS) {
            slot = glow_count++;
        } else {
            slot = glow_count - 1;
        }
        glow_pos[slot * 3 + 0] = fx->pos.x;
        glow_pos[slot * 3 + 1] = fx->pos.y;
        glow_pos[slot * 3 + 2] = fx->pos.z;
        glow_col[slot * 3 + 0] = fx->color.x * fx->intensity;
        glow_col[slot * 3 + 1] = fx->color.y * fx->intensity;
        glow_col[slot * 3 + 2] = fx->color.z * fx->intensity;
        glow_range[slot] = fx->range;
        glow_ids[slot] = ENTITY_INVALID;
        glow_acc[slot] = 0;
    }

    {
        int sn = brick_glow_n;
        if (sn > RENDERER_MAX_GLOW_SHADOW_LIGHTS) sn = RENDERER_MAX_GLOW_SHADOW_LIGHTS;
        Vec3 spos[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
        float srange[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
        EntityID sids[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
        for (int i = 0; i < sn; i++) {
            sids[i] = glow_ids[i];
            spos[i].x = glow_pos[i * 3 + 0];
            spos[i].y = glow_pos[i * 3 + 1];
            spos[i].z = glow_pos[i * 3 + 2];
            srange[i] = glow_range[i];
        }
        renderer_glow_evsm_pass(r, scene, sids, spos, srange, glow_acc, sn);
        shader_use(&r->shader);
    }

    if (r->shader.u_glow_light_count >= 0)
        glUniform1i(r->shader.u_glow_light_count, glow_count);

    for (int i = 0; i < RENDERER_MAX_GLOW_LIGHTS; i++) {
        int live = (i < glow_count);
        float px = live ? glow_pos[i * 3 + 0] : 0.0f;
        float py = live ? glow_pos[i * 3 + 1] : 0.0f;
        float pz = live ? glow_pos[i * 3 + 2] : 0.0f;
        float cx = live ? glow_col[i * 3 + 0] : 0.0f;
        float cy = live ? glow_col[i * 3 + 1] : 0.0f;
        float cz = live ? glow_col[i * 3 + 2] : 0.0f;
        float rng = live ? glow_range[i] : 0.0f;
        unsigned eid = live ? renderer_shadow_id_entity(glow_ids[i]) : 0u;
        if (live && glow_acc[i]) eid |= RENDERER_GLOW_ACC_FLAG;
        if (r->shader.u_glow_light_pos >= 0)
            glUniform3f(r->shader.u_glow_light_pos + i, px, py, pz);
        if (r->shader.u_glow_light_color >= 0)
            glUniform3f(r->shader.u_glow_light_color + i, cx, cy, cz);
        if (r->shader.u_glow_light_range >= 0)
            glUniform1f(r->shader.u_glow_light_range + i, rng);
        if (r->shader.u_glow_light_entity >= 0)
            glUniform1ui(r->shader.u_glow_light_entity + i, eid);
    }
    if (r->shader.u_glow_shadow_count >= 0)
        glUniform1i(r->shader.u_glow_shadow_count, r->glow_shadow_count);
    if (r->shader.u_glow_ls >= 0) {
        int mats = r->glow_shadow_count * 6;
        if (mats > 12) mats = 12;
        for (int i = 0; i < mats; i++)
            glUniformMatrix4fv(r->shader.u_glow_ls + i, 1, GL_FALSE, r->glow_ls[i].m);
    }
    renderer_bind_shader_samplers(r);
    renderer_bind_vsm_receive(r);
    glActiveTexture(GL_TEXTURE0);

    float frustum[6][4];
    if (r->frustum_cull_frozen && r->frustum_has_frozen) {
        memcpy(frustum, r->frozen_frustum, sizeof(frustum));
    } else {
        Mat4 vp;
        for (int i = 0; i < 16; i++) vp.m[i] = 0;

        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
                for (int k = 0; k < 4; k++)
                    vp.m[col*4+row] += projection->m[k*4+row] * view->m[col*4+k];

        frustum[0][0] = vp.m[3] + vp.m[0]; frustum[0][1] = vp.m[7] + vp.m[4];
        frustum[0][2] = vp.m[11] + vp.m[8]; frustum[0][3] = vp.m[15] + vp.m[12];

        frustum[1][0] = vp.m[3] - vp.m[0]; frustum[1][1] = vp.m[7] - vp.m[4];
        frustum[1][2] = vp.m[11] - vp.m[8]; frustum[1][3] = vp.m[15] - vp.m[12];

        frustum[2][0] = vp.m[3] + vp.m[1]; frustum[2][1] = vp.m[7] + vp.m[5];
        frustum[2][2] = vp.m[11] + vp.m[9]; frustum[2][3] = vp.m[15] + vp.m[13];

        frustum[3][0] = vp.m[3] - vp.m[1]; frustum[3][1] = vp.m[7] - vp.m[5];
        frustum[3][2] = vp.m[11] - vp.m[9]; frustum[3][3] = vp.m[15] - vp.m[13];

        frustum[4][0] = vp.m[3] + vp.m[2]; frustum[4][1] = vp.m[7] + vp.m[6];
        frustum[4][2] = vp.m[11] + vp.m[10]; frustum[4][3] = vp.m[15] + vp.m[14];

        frustum[5][0] = vp.m[3] - vp.m[2]; frustum[5][1] = vp.m[7] - vp.m[6];
        frustum[5][2] = vp.m[11] - vp.m[10]; frustum[5][3] = vp.m[15] - vp.m[14];

        for (int p = 0; p < 6; p++) {
            float len = sqrtf(frustum[p][0]*frustum[p][0] + frustum[p][1]*frustum[p][1] + frustum[p][2]*frustum[p][2]);
            if (len > 0.0001f) { frustum[p][0]/=len; frustum[p][1]/=len; frustum[p][2]/=len; frustum[p][3]/=len; }
        }
        if (r->frustum_cull_frozen) {
            memcpy(r->frozen_frustum, frustum, sizeof(frustum));
            r->frustum_has_frozen = true;
        }
    }

    bool use_batches = brick_batch_active() && !r->shadows_enabled;
    bool building_batches = brick_batch_is_building();

    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            renderer_apply_corner_ao(r, projection);
            if (mid) {
                mid(mid_user);

                shader_use(&r->shader);
                glUniformMatrix4fv(r->shader.u_view, 1, GL_FALSE, view->m);
                glUniformMatrix4fv(r->shader.u_projection, 1, GL_FALSE, projection->m);
                glUniform3f(r->shader.u_light_dir, r->light_dir.x, r->light_dir.y, r->light_dir.z);
                glUniform3f(r->shader.u_light_color, r->light_color.x, r->light_color.y, r->light_color.z);
                renderer_bind_shader_samplers(r);
                renderer_bind_vsm_receive(r);
                if (r->shader.u_shadow_face_ids >= 0)
                    glUniform1i(r->shader.u_shadow_face_ids, 1);
                if (r->shader.u_glow_light_count >= 0)
                    glUniform1i(r->shader.u_glow_light_count, glow_count);
                if (r->shader.u_glow_shadow_count >= 0)
                    glUniform1i(r->shader.u_glow_shadow_count, r->glow_shadow_count);
            }
            glEnable(GL_BLEND);

            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            glEnable(GL_CULL_FACE);

            if (r->fog_world_pass && r->scene_fog_depth_tex) {
#if !PW_USE_GLES
                glDisablei(GL_BLEND, 1);
#endif
            }
        }

        if (pass == 0 && use_batches && !building_batches) {
            Mat4 ident = mat4_identity();

            if (r->shader.u_part_size >= 0)
                glUniform3f(r->shader.u_part_size, 1.0f, 1.0f, 1.0f);
            renderer_bind_part_material(r, PART_MATERIAL_PLASTIC);
            brick_batch_draw(&r->textures,
                            r->shader.u_model, r->shader.u_color, r->shader.u_glow,
                            r->shader.u_alpha, r->shader.u_has_texture,
                            r->shader.u_shadow_id, r->shader.u_normal_map,
                            NULL, ident.m);
        }

    for (uint32_t i = 0; i < scene->count; i++) {
        const Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh) continue;
        if (renderer_shadow_skip_has(r, e->id)) continue;

        if (use_batches && e->render_batched) continue;
        (void)building_batches;

        float alpha = e->material.alpha;
        if (alpha <= 0.01f) continue;
        bool is_trans = (alpha < 0.99f);
        if (pass == 0 && is_trans) continue;
        if (pass == 1 && !is_trans) continue;

        Mat4 model = scene_get_world_matrix(scene, e->id);
        Vec3 pos = { model.m[12], model.m[13], model.m[14] };
        float sx = e->transform.scale.x, sy = e->transform.scale.y, sz = e->transform.scale.z;
        if (sx < 0.0f) sx = -sx;
        if (sy < 0.0f) sy = -sy;
        if (sz < 0.0f) sz = -sz;
        float rx = fmaxf(fabsf(e->mesh->aabb_min[0]), fabsf(e->mesh->aabb_max[0])) * sx;
        float ry = fmaxf(fabsf(e->mesh->aabb_min[1]), fabsf(e->mesh->aabb_max[1])) * sy;
        float rz = fmaxf(fabsf(e->mesh->aabb_min[2]), fabsf(e->mesh->aabb_max[2])) * sz;
        float radius = sqrtf(rx * rx + ry * ry + rz * rz);
        if (radius < 0.001f) {
            float scale_max = sx;
            if (sy > scale_max) scale_max = sy;
            if (sz > scale_max) scale_max = sz;
            radius = e->mesh->bounding_radius * scale_max;
        }

        bool force_visible = (e->material.glow >= 0.05f) ||
                             (e->mesh->bounding_radius >= 12.0f) ||
                             (sx >= 16.0f || sy >= 16.0f || sz >= 16.0f) ||
                             glow_lit_by_active(pos, radius, glow_pos, glow_range, glow_count);
        bool visible = true;
        if (!force_visible) {
            for (int p = 0; p < 6; p++) {
                float dist = frustum[p][0]*pos.x + frustum[p][1]*pos.y + frustum[p][2]*pos.z + frustum[p][3];
                if (dist < -radius) { visible = false; break; }
            }
        }
        if (!visible) continue;
        glUniformMatrix4fv(r->shader.u_model, 1, GL_FALSE, model.m);
        glUniform3f(r->shader.u_color, e->material.color.x, e->material.color.y, e->material.color.z);

        if (r->shader.u_part_size >= 0) {
            float psx = fabsf(e->transform.scale.x);
            float psy = fabsf(e->transform.scale.y);
            float psz = fabsf(e->transform.scale.z);
            if (psx < 1e-4f) psx = 1.0f;
            if (psy < 1e-4f) psy = 1.0f;
            if (psz < 1e-4f) psz = 1.0f;
            glUniform3f(r->shader.u_part_size, psx, psy, psz);
        }
        if (r->shader.u_part_shape >= 0) {
            GPUMesh* kindm = renderer_entity_tess_mesh(r, e);
            int shape = (kindm && (kindm->prim_kind == 1 || kindm->prim_kind == 2))
                ? (int)kindm->prim_kind : 0;
            glUniform1i(r->shader.u_part_shape, shape);
        }
        renderer_bind_part_material(r, e->material.part_material);
        if (r->shader.u_glow >= 0)
            glUniform1f(r->shader.u_glow, e->material.glow);
        if (r->shader.u_alpha >= 0)
            glUniform1f(r->shader.u_alpha, alpha);
        if (r->shader.u_contact_shade >= 0)
            glUniform1f(r->shader.u_contact_shade, 0.0f);

        GPUMesh* draw_mesh = renderer_entity_tess_mesh(r, e);
        if (!draw_mesh) continue;
        glBindVertexArray(draw_mesh->vao);

        if (!draw_mesh->has_colors) {
            glDisableVertexAttribArray(3);
            glVertexAttrib3f(3, 1.0f, 1.0f, 1.0f);
        }

        const bool is_box = (draw_mesh->index_count == 36);

        const int recv_face_ids = 1;

        if (e->material.texture_id && draw_mesh->has_texcoords) {
            if (r->shader.u_shadow_id >= 0)
                glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
            if (r->shader.u_shadow_face_ids >= 0)
                glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, e->material.texture_id);
            glUniform1i(r->shader.u_has_texture, e->material.texture_mode);
            glDrawElements(GL_TRIANGLES, (GLsizei)draw_mesh->index_count, GL_UNSIGNED_INT, 0);
        }

        else if (draw_mesh->has_texcoords) {
            static const int face_to_surface[] = { 2, 3, 0, 1, 5, 4 };
            const GLsizei draw_count = (GLsizei)draw_mesh->index_count;

            bool all_smooth = true;
            if (e->material.part_material == PART_MATERIAL_PLASTIC) {
                for (int s = 0; s < 6; s++) {
                    if (e->material.surfaces[s] != SURFACE_SMOOTH) { all_smooth = false; break; }
                }
            }

            bool use_face_mode = !all_smooth &&
                                 r->shader.u_face_mode >= 0 && r->shader.u_face_surf >= 0;
            if (use_face_mode) {
                int face_surf[6];
                for (int face = 0; face < 6; face++) {
                    SurfaceType st = e->material.surfaces[face_to_surface[face]];
                    face_surf[face] = (st == SURFACE_STUD) ? 1 : (st == SURFACE_INLET) ? 2 : 0;
                }
                if (r->shader.u_shadow_id >= 0)
                    glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                if (r->shader.u_shadow_face_ids >= 0)
                    glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
                glUniform1i(r->shader.u_face_mode, 1);
                glUniform1iv(r->shader.u_face_surf, 6, face_surf);
                glUniform1i(r->shader.u_has_texture, 0);
                TextureID stud = texture_get_for_surface(&r->textures, SURFACE_STUD);
                TextureID inlet = texture_get_for_surface(&r->textures, SURFACE_INLET);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, stud != TEXTURE_INVALID ? stud : 0);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, inlet != TEXTURE_INVALID ? inlet : 0);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);
                glDrawElements(GL_TRIANGLES, draw_count, GL_UNSIGNED_INT, 0);
                glUniform1i(r->shader.u_face_mode, 0);
            } else if (!is_box) {
                if (r->shader.u_shadow_id >= 0)
                    glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                if (r->shader.u_shadow_face_ids >= 0)
                    glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
                glUniform1i(r->shader.u_has_texture, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDrawElements(GL_TRIANGLES, draw_count, GL_UNSIGNED_INT, 0);
            } else if (!r->shadows_enabled) {
                if (r->shader.u_shadow_id >= 0)
                    glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                glUniform1i(r->shader.u_has_texture, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
            } else if (all_smooth) {
                if (r->shader.u_shadow_id >= 0)
                    glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                if (r->shader.u_shadow_face_ids >= 0)
                    glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
                glUniform1i(r->shader.u_has_texture, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
            } else {
                bool all_same = true;
                SurfaceType first_surf = e->material.surfaces[0];
                for (int s = 1; s < 6; s++) {
                    if (e->material.surfaces[s] != first_surf) { all_same = false; break; }
                }
                if (all_same) {
                    if (r->shader.u_shadow_id >= 0)
                        glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                    if (r->shader.u_shadow_face_ids >= 0)
                        glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
                    TextureID tex = texture_get_for_surface(&r->textures, first_surf);
                    TextureID norm_tex = texture_get_normal_for_surface(&r->textures, first_surf);
                    if (tex != TEXTURE_INVALID) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glUniform1i(r->shader.u_has_texture, 1);
                    } else {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        glUniform1i(r->shader.u_has_texture, 0);
                    }
                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, norm_tex != TEXTURE_INVALID ? norm_tex : 0);
                    glUniform1i(r->shader.u_normal_map, 2);
                    glActiveTexture(GL_TEXTURE1);
                    glActiveTexture(GL_TEXTURE0);
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
                } else {

                    for (int face = 0; face < 6; face++) {
                        if (r->shader.u_shadow_id >= 0)
                            glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
                        if (r->shader.u_shadow_face_ids >= 0)
                            glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);

                        int surf_idx = face_to_surface[face];
                        SurfaceType st = e->material.surfaces[surf_idx];
                        TextureID tex = texture_get_for_surface(&r->textures, st);
                        TextureID norm_tex = texture_get_normal_for_surface(&r->textures, st);
                        if (tex != TEXTURE_INVALID) {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, tex);
                            glUniform1i(r->shader.u_has_texture, 1);
                        } else {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, 0);
                            glUniform1i(r->shader.u_has_texture, 0);
                        }
                        glActiveTexture(GL_TEXTURE2);
                        glBindTexture(GL_TEXTURE_2D, norm_tex != TEXTURE_INVALID ? norm_tex : 0);
                        glUniform1i(r->shader.u_normal_map, 2);
                        glActiveTexture(GL_TEXTURE1);
                        glActiveTexture(GL_TEXTURE0);
                        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                                       (void*)(size_t)(face * 6 * sizeof(uint32_t)));
                    }
                }
            }
        } else if (draw_mesh->index_count == 36) {
            if (r->shader.u_shadow_id >= 0)
                glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
            if (r->shader.u_shadow_face_ids >= 0)
                glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
            glUniform1i(r->shader.u_has_texture, 0);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        } else {

            if (r->shader.u_shadow_id >= 0)
                glUniform1ui(r->shader.u_shadow_id, renderer_shadow_id_entity(e->id));
            if (r->shader.u_shadow_face_ids >= 0)
                glUniform1i(r->shader.u_shadow_face_ids, recv_face_ids);
            glUniform1i(r->shader.u_has_texture, 0);
            glDrawElements(GL_TRIANGLES, (GLsizei)draw_mesh->index_count, GL_UNSIGNED_INT, 0);
        }
    }

        if (pass == 1) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            if (r->fog_world_pass && r->scene_fog_depth_tex) {
#if !PW_USE_GLES
                glEnablei(GL_BLEND, 1);
#endif
            }
            glEnable(GL_CULL_FACE);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void renderer_apply_corner_ao(Renderer* r, const Mat4* projection) {
    if (!r || r->is_studio || !projection) return;
    if (!r->fog_world_pass || !r->scene_fbo || !r->scene_depth_tex) return;
    if (!r->ssao_enabled) return;
    if (!r->ssao_shader.program || !r->ssao_noise_tex || !r->fog_quad_vao) return;
    if (!r->ssao_fbo || !r->ssao_tex || !r->ssao_blur_shader.program) return;

    Mat4 inv_proj = mat4_inverse(*projection);
    int vw = r->scene_w > 0 ? r->scene_w : r->canvas_width;
    int vh = r->scene_h > 0 ? r->scene_h : r->canvas_height;
    int aw = vw > 1 ? (vw + 1) / 2 : 1;
    int ah = vh > 1 ? (vh + 1) / 2 : 1;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, r->ssao_fbo);
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }
    glViewport(0, 0, aw, ah);
    shader_use(&r->ssao_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->scene_depth_tex);
    if (r->ssao_u_depth >= 0) glUniform1i(r->ssao_u_depth, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->ssao_noise_tex);
    if (r->ssao_u_noise >= 0) glUniform1i(r->ssao_u_noise, 1);
    if (r->ssao_u_projection >= 0)
        glUniformMatrix4fv(r->ssao_u_projection, 1, GL_FALSE, projection->m);
    if (r->ssao_u_inv_projection >= 0)
        glUniformMatrix4fv(r->ssao_u_inv_projection, 1, GL_FALSE, inv_proj.m);
    if (r->ssao_u_noise_scale >= 0)
        glUniform2f(r->ssao_u_noise_scale, (float)aw / 4.0f, (float)ah / 4.0f);
    if (r->ssao_u_radius >= 0) glUniform1f(r->ssao_u_radius, 1.25f);
    {
        float bias = 0.06f + 12.0f / (float)(ah > 80 ? ah : 80);
        if (r->ssao_u_bias >= 0) glUniform1f(r->ssao_u_bias, bias);
    }
    for (int i = 0; i < 24; i++) {
        char name[32];
        snprintf(name, sizeof(name), "u_samples[%d]", i);
        int loc = glGetUniformLocation(r->ssao_shader.program, name);
        if (loc < 0 && r->ssao_u_samples >= 0) loc = r->ssao_u_samples + i;
        if (loc >= 0)
            glUniform3f(loc, r->ssao_kernel[i * 3], r->ssao_kernel[i * 3 + 1],
                        r->ssao_kernel[i * 3 + 2]);
    }
    glBindVertexArray(r->fog_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    {
        GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }
    glViewport(0, 0, vw, vh);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    shader_use(&r->ssao_blur_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->ssao_tex);
    if (r->ssao_blur_u_ssao >= 0) glUniform1i(r->ssao_blur_u_ssao, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->scene_depth_tex);
    if (r->ssao_blur_u_depth >= 0) glUniform1i(r->ssao_blur_u_depth, 1);
    if (r->ssao_blur_u_inv_projection >= 0)
        glUniformMatrix4fv(r->ssao_blur_u_inv_projection, 1, GL_FALSE, inv_proj.m);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    renderer_bind_draw_target(r);
    shader_use(&r->shader);
    renderer_bind_shader_samplers(r);
    renderer_bind_vsm_receive(r);
}

void renderer_apply_fog(Renderer* r, float near_plane, float far_plane) {
    if (!r || r->is_studio || !r->fog_world_pass || !r->scene_fbo || !r->fog_shader.program)
        return;

    r->fog_world_pass = false;
    if (r->scale_active && r->scale_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->scale_fbo);
        glViewport(0, 0, r->scene_w, r->scene_h);
    } else {
        renderer_bind_window_fbo(r);
        glViewport(0, 0, r->canvas_width, r->canvas_height);
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_use(&r->fog_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->scene_color_tex);
    glUniform1i(r->fog_u_color, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->scene_depth_tex);
    glUniform1i(r->fog_u_depth, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r->scene_fog_depth_tex ? r->scene_fog_depth_tex : r->scene_depth_tex);
    if (r->fog_u_fog_depth >= 0)
        glUniform1i(r->fog_u_fog_depth, 2);
    if (r->fog_u_clear_color >= 0)
        glUniform3f(r->fog_u_clear_color, r->clear_r, r->clear_g, r->clear_b);
    glUniform1f(r->fog_u_near, near_plane);
    glUniform1f(r->fog_u_far, far_plane);

    if (r->fog_enabled) {
        glUniform1f(r->fog_u_start, far_plane * PW_FOG_START_FRAC);
        glUniform1f(r->fog_u_end, far_plane * PW_FOG_END_FRAC);
    } else {
        glUniform1f(r->fog_u_start, far_plane * 8.0f);
        glUniform1f(r->fog_u_end, far_plane * 9.0f);
    }
    glBindVertexArray(r->fog_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader_use(&r->shader);
}

void renderer_end_frame(Renderer* r) {
    if (r->is_studio && !r->host_present) {

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

}

void renderer_set_frustum_cull_frozen(Renderer* r, bool frozen) {
    if (!r) return;
    r->frustum_cull_frozen = frozen;
    if (!frozen) r->frustum_has_frozen = false;

}

bool renderer_get_frustum_cull_frozen(const Renderer* r) {
    return r && r->frustum_cull_frozen;
}

void renderer_set_shadow_map_size(Renderer* r, int size) {
    if (!r || r->is_studio) return;
    if (size < 256) size = 256;
#if PW_MOBILE
    if (size > 2048) size = 2048;
#else
    if (size > 4096) size = 4096;
#endif

    if (r->shadow_fbo && r->shadow_id_tex && size != r->shadow_map_size) {
        int old = r->shadow_map_size;
        if (!renderer_setup_sun_shadow_map(r, size)) {
            PW_ERR(ERR_GENERIC, "Sun shadow resize to %d failed, keeping %d\n", size, old);
            if (old > 0 && !renderer_setup_sun_shadow_map(r, old))
                r->shadows_enabled = false;
        }
    } else if (!r->shadow_fbo && r->shadow_shader.program) {
        if (renderer_setup_sun_shadow_map(r, size))
            r->shadows_enabled = true;
    } else {
        r->shadow_map_size = size;
    }
}

bool mesh_upload(const MeshData* data, GPUMesh* out) {
    if (!data || !out || data->vertex_count == 0 || data->index_count == 0) {
        return false;
    }

    memset(out, 0, sizeof(GPUMesh));
    out->index_count = data->index_count;
    out->has_texcoords = (data->texcoords != NULL);
    out->has_colors = (data->colors != NULL);

    glGenVertexArrays(1, &out->vao);
    glBindVertexArray(out->vao);

    glGenBuffers(1, &out->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, out->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(data->vertex_count * 3 * sizeof(float)),
                 data->positions, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glGenBuffers(1, &out->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, out->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(data->vertex_count * 3 * sizeof(float)),
                 data->normals, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    if (data->texcoords) {
        glGenBuffers(1, &out->vbo_texcoords);
        glBindBuffer(GL_ARRAY_BUFFER, out->vbo_texcoords);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(data->vertex_count * 2 * sizeof(float)),
                     data->texcoords, GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    }

    {
        glGenBuffers(1, &out->vbo_colors);
        glBindBuffer(GL_ARRAY_BUFFER, out->vbo_colors);
        if (data->colors) {
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(data->vertex_count * 3 * sizeof(float)),
                         data->colors, GL_STATIC_DRAW);
            out->has_colors = true;
        } else {
            size_t n = data->vertex_count * 3;
            float* white = (float*)malloc(n * sizeof(float));
            if (white) {
                for (size_t i = 0; i < n; i++) white[i] = 1.0f;
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(float)), white, GL_STATIC_DRAW);
                free(white);
            } else {
                float one[3] = {1.0f, 1.0f, 1.0f};
                glBufferData(GL_ARRAY_BUFFER, sizeof(one), one, GL_STATIC_DRAW);
            }
            out->has_colors = true;
        }
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    }

    glGenBuffers(1, &out->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(data->index_count * sizeof(uint32_t)),
                 data->indices, GL_STATIC_DRAW);

    glBindVertexArray(0);

    float max_dist_sq = 0.0f;
    out->aabb_min[0] = out->aabb_min[1] = out->aabb_min[2] = 1e30f;
    out->aabb_max[0] = out->aabb_max[1] = out->aabb_max[2] = -1e30f;
    for (size_t v = 0; v < data->vertex_count; v++) {
        float x = data->positions[v*3], y = data->positions[v*3+1], z = data->positions[v*3+2];
        float d = x*x + y*y + z*z;
        if (d > max_dist_sq) max_dist_sq = d;
        if (x < out->aabb_min[0]) out->aabb_min[0] = x;
        if (y < out->aabb_min[1]) out->aabb_min[1] = y;
        if (z < out->aabb_min[2]) out->aabb_min[2] = z;
        if (x > out->aabb_max[0]) out->aabb_max[0] = x;
        if (y > out->aabb_max[1]) out->aabb_max[1] = y;
        if (z > out->aabb_max[2]) out->aabb_max[2] = z;
    }
    out->bounding_radius = sqrtf(max_dist_sq);

#if PW_MOBILE
    out->cpu_vertex_count = 0;
    out->cpu_positions = NULL;
    out->cpu_indices = NULL;
#else
    out->cpu_vertex_count = data->vertex_count;
    out->cpu_positions = (float*)malloc(data->vertex_count * 3 * sizeof(float));
    out->cpu_indices = (uint32_t*)malloc(data->index_count * sizeof(uint32_t));
    if (!out->cpu_positions || !out->cpu_indices) {
        free(out->cpu_positions);
        free(out->cpu_indices);
        out->cpu_positions = NULL;
        out->cpu_indices = NULL;
        out->cpu_vertex_count = 0;
    } else {
        memcpy(out->cpu_positions, data->positions, data->vertex_count * 3 * sizeof(float));
        memcpy(out->cpu_indices, data->indices, data->index_count * sizeof(uint32_t));
    }
#endif

    return true;
}

void mesh_gpu_free(GPUMesh* mesh) {
    if (!mesh) return;
    if (mesh->vao) glDeleteVertexArrays(1, &mesh->vao);
    if (mesh->vbo_positions) glDeleteBuffers(1, &mesh->vbo_positions);
    if (mesh->vbo_normals) glDeleteBuffers(1, &mesh->vbo_normals);
    if (mesh->vbo_texcoords) glDeleteBuffers(1, &mesh->vbo_texcoords);
    if (mesh->vbo_colors) glDeleteBuffers(1, &mesh->vbo_colors);
    if (mesh->ebo) glDeleteBuffers(1, &mesh->ebo);
    free(mesh->cpu_positions);
    free(mesh->cpu_indices);
    memset(mesh, 0, sizeof(GPUMesh));
}

static Shader debug_shader;
static GLuint debug_program = 0;
static GLint debug_u_mvp = -1;
static GLint debug_u_color = -1;
static GLuint debug_box_vao = 0;
static GLuint debug_box_vbo = 0;

static void debug_ensure_init(void) {
    if (debug_program != 0) return;

    if (!shader_compile_asset(&debug_shader, "debug_line"))
        return;
    debug_program = debug_shader.program;
    debug_u_mvp = glGetUniformLocation(debug_program, "u_mvp");
    debug_u_color = glGetUniformLocation(debug_program, "u_debug_color");

    float box_lines[] = {

        -1,-1,-1,  1,-1,-1,   1,-1,-1,  1,-1, 1,
         1,-1, 1, -1,-1, 1,  -1,-1, 1, -1,-1,-1,

        -1, 1,-1,  1, 1,-1,   1, 1,-1,  1, 1, 1,
         1, 1, 1, -1, 1, 1,  -1, 1, 1, -1, 1,-1,

        -1,-1,-1, -1, 1,-1,   1,-1,-1,  1, 1,-1,
         1,-1, 1,  1, 1, 1,  -1,-1, 1, -1, 1, 1,
    };

    glGenVertexArrays(1, &debug_box_vao);
    glGenBuffers(1, &debug_box_vbo);
    glBindVertexArray(debug_box_vao);
    glBindBuffer(GL_ARRAY_BUFFER, debug_box_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(box_lines), box_lines, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void renderer_debug_box(Renderer* r, Vec3 pos, Vec3 half_extents, Vec3 rotation, Vec3 color,
                        const Mat4* view, const Mat4* projection) {
    (void)r;
    debug_ensure_init();

    Mat4 scale = mat4_scale(half_extents);
    Mat4 rx = mat4_rotate_x(rotation.x);
    Mat4 ry = mat4_rotate_y(rotation.y);
    Mat4 rz = mat4_rotate_z(rotation.z);
    Mat4 trans = mat4_translate(pos);

    Mat4 model = mat4_multiply(rx, scale);
    model = mat4_multiply(ry, model);
    model = mat4_multiply(rz, model);
    model = mat4_multiply(trans, model);

    Mat4 vp = mat4_multiply(*projection, *view);
    Mat4 mvp = mat4_multiply(vp, model);

    glUseProgram(debug_program);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glUniform3f(debug_u_color, color.x, color.y, color.z);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(debug_box_vao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void renderer_debug_line(Renderer* r, Vec3 start, Vec3 end, Vec3 color,
                         const Mat4* view, const Mat4* projection) {
    (void)r;
    debug_ensure_init();

    float vertices[6] = {
        start.x, start.y, start.z,
        end.x,   end.y,   end.z
    };

    Mat4 mvp = mat4_multiply(*projection, *view);

    glUseProgram(debug_program);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glUniform3f(debug_u_color, color.x, color.y, color.z);

    static GLuint line_vbo = 0, line_vao = 0;
    if (line_vbo == 0) {
        glGenVertexArrays(1, &line_vao);
        glGenBuffers(1, &line_vbo);
        glBindVertexArray(line_vao);
        glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void renderer_debug_box_matrix(Renderer* r, const float* model_matrix, Vec3 color,
                               const Mat4* view, const Mat4* projection) {
    (void)r;
    debug_ensure_init();

    Mat4 model;
    memcpy(model.m, model_matrix, 16 * sizeof(float));

    Mat4 vp = mat4_multiply(*projection, *view);
    Mat4 mvp = mat4_multiply(vp, model);

    glUseProgram(debug_program);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glUniform3f(debug_u_color, color.x, color.y, color.z);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(debug_box_vao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void renderer_debug_sphere(Renderer* r, Vec3 pos, float radius, Vec3 color,
                           const Mat4* view, const Mat4* projection) {

    (void)r;
    debug_ensure_init();

    #define CIRCLE_SEGMENTS 24
    static GLuint circle_vao = 0;
    static GLuint circle_vbo = 0;

    if (circle_vao == 0) {
        float circle_verts[CIRCLE_SEGMENTS * 2 * 3];
        for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
            float a0 = (float)i / CIRCLE_SEGMENTS * 6.28318530718f;
            float a1 = (float)(i + 1) / CIRCLE_SEGMENTS * 6.28318530718f;
            int idx = i * 6;
            circle_verts[idx+0] = cosf(a0);
            circle_verts[idx+1] = sinf(a0);
            circle_verts[idx+2] = 0.0f;
            circle_verts[idx+3] = cosf(a1);
            circle_verts[idx+4] = sinf(a1);
            circle_verts[idx+5] = 0.0f;
        }
        glGenVertexArrays(1, &circle_vao);
        glGenBuffers(1, &circle_vbo);
        glBindVertexArray(circle_vao);
        glBindBuffer(GL_ARRAY_BUFFER, circle_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(circle_verts), circle_verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    Mat4 vp = mat4_multiply(*projection, *view);
    Mat4 trans = mat4_translate(pos);
    Vec3 s = {radius, radius, radius};
    Mat4 sc = mat4_scale(s);

    glUseProgram(debug_program);
    glUniform3f(debug_u_color, color.x, color.y, color.z);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(circle_vao);

    Mat4 model = mat4_multiply(trans, sc);
    Mat4 mvp = mat4_multiply(vp, model);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glDrawArrays(GL_LINES, 0, CIRCLE_SEGMENTS * 2);

    Mat4 rot_x = mat4_rotate_x(90.0f);
    model = mat4_multiply(trans, mat4_multiply(rot_x, sc));
    mvp = mat4_multiply(vp, model);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glDrawArrays(GL_LINES, 0, CIRCLE_SEGMENTS * 2);

    Mat4 rot_y = mat4_rotate_y(90.0f);
    model = mat4_multiply(trans, mat4_multiply(rot_y, sc));
    mvp = mat4_multiply(vp, model);
    glUniformMatrix4fv(debug_u_mvp, 1, GL_FALSE, mvp.m);
    glDrawArrays(GL_LINES, 0, CIRCLE_SEGMENTS * 2);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    #undef CIRCLE_SEGMENTS
}

void renderer_draw_mesh_alpha(Renderer* r, const GPUMesh* mesh, const Mat4* model,
                              Vec3 color, uint32_t texture_id, int texture_mode,
                              const Mat4* view, const Mat4* projection, float alpha) {
    if (!mesh || mesh->vao == 0 || alpha <= 0.001f) return;

    bool additive = r->mesh_fx_additive != 0;
    bool faded = additive || alpha < 0.999f;
    bool texel_alpha = (texture_mode == 4 || texture_mode == 5);
    bool used_blend = faded || texel_alpha;
    bool fog_mrt = r->fog_world_pass && r->scene_fog_depth_tex;
    if (used_blend) {
        glEnable(GL_BLEND);
        if (additive)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (faded)
            glDepthMask(GL_FALSE);

        if (fog_mrt && faded) {
#if !PW_USE_GLES
            glDisablei(GL_BLEND, 1);
#endif
        }
    }
    if (r->mesh_fx_no_cull == 1)
        glDisable(GL_CULL_FACE);
    else {
        glEnable(GL_CULL_FACE);
        glCullFace(r->mesh_fx_no_cull == 2 ? GL_FRONT : GL_BACK);
    }

    shader_use(&r->shader);
    glUniformMatrix4fv(r->shader.u_view, 1, GL_FALSE, view->m);
    glUniformMatrix4fv(r->shader.u_projection, 1, GL_FALSE, projection->m);
    glUniform3f(r->shader.u_light_dir, r->light_dir.x, r->light_dir.y, r->light_dir.z);
    glUniform3f(r->shader.u_light_color, r->light_color.x, r->light_color.y, r->light_color.z);
    glUniform1i(r->shader.u_texture, 0);

    if (r->shader.u_shadow_id >= 0)
        glUniform1ui(r->shader.u_shadow_id, r->current_shadow_id);
    if (r->shader.u_contact_shade >= 0)
        glUniform1f(r->shader.u_contact_shade, 0.0f);

    {
        Mat4 inv_view = mat4_inverse(*view);
        glUniform3f(r->shader.u_camera_pos, inv_view.m[12], inv_view.m[13], inv_view.m[14]);
    }

    glUniform3f(r->shader.u_fog_color, r->clear_r, r->clear_g, r->clear_b);
    glUniform1f(r->shader.u_fog_start, PW_CAMERA_FAR * PW_FOG_START_FRAC);
    glUniform1f(r->shader.u_fog_end, PW_CAMERA_FAR * PW_FOG_END_FRAC);
    glUniform1i(r->shader.u_fog_enabled, 0);

    renderer_bind_shader_samplers(r);
    renderer_bind_vsm_receive(r);

    if (r->shader.u_shadow_face_ids >= 0)
        glUniform1i(r->shader.u_shadow_face_ids, 0);

    if (r->shader.u_face_mode >= 0)
        glUniform1i(r->shader.u_face_mode, 0);
    if (r->shader.u_part_shape >= 0)
        glUniform1i(r->shader.u_part_shape, 0);
    if (r->shader.u_part_size >= 0)
        glUniform3f(r->shader.u_part_size, 1.0f, 1.0f, 1.0f);
    renderer_bind_part_material(r, PART_MATERIAL_PLASTIC);

    glUniformMatrix4fv(r->shader.u_model, 1, GL_FALSE, model->m);
    glUniform3f(r->shader.u_color, color.x, color.y, color.z);
    if (r->shader.u_uv_rect >= 0) {
        float su = r->mesh_fx_uv[2];
        float sv = r->mesh_fx_uv[3];
        if (su <= 0.0f) su = 1.0f;
        if (sv <= 0.0f) sv = 1.0f;
        glUniform4f(r->shader.u_uv_rect, r->mesh_fx_uv[0], r->mesh_fx_uv[1], su, sv);
    }
    if (r->shader.u_glow >= 0)
        glUniform1f(r->shader.u_glow, r->mesh_fx_glow);
    if (r->shader.u_alpha >= 0)
        glUniform1f(r->shader.u_alpha, alpha);

    glBindVertexArray(mesh->vao);

    if (texture_id && mesh->has_texcoords) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glUniform1i(r->shader.u_has_texture, texture_mode);
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count, GL_UNSIGNED_INT, 0);
    } else {
        glUniform1i(r->shader.u_has_texture, 0);
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count, GL_UNSIGNED_INT, 0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    if ((faded || texel_alpha) && !r->mesh_fx_hold) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (fog_mrt) {
#if !PW_USE_GLES
            glEnablei(GL_BLEND, 1);
#endif
        }
    }
    if (r->mesh_fx_no_cull && !r->mesh_fx_hold) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    r->mesh_fx_glow = 0.0f;
    r->mesh_fx_additive = 0;
    r->mesh_fx_no_cull = 0;
    r->mesh_fx_uv[0] = 0.0f;
    r->mesh_fx_uv[1] = 0.0f;
    r->mesh_fx_uv[2] = 1.0f;
    r->mesh_fx_uv[3] = 1.0f;
}

void renderer_draw_mesh(Renderer* r, const GPUMesh* mesh, const Mat4* model,
                        Vec3 color, uint32_t texture_id, int texture_mode,
                        const Mat4* view, const Mat4* projection) {
    renderer_draw_mesh_alpha(r, mesh, model, color, texture_id, texture_mode, view, projection, 1.0f);
}

void renderer_draw_comfort_vignette(Renderer* r, float strength) {
    if (!r || strength < 0.01f) return;
    static Shader vig;
    static int u_strength = -1;
    static unsigned int vao, vbo;
    static int ready;
    if (ready < 0) return;
    if (!ready) {
        memset(&vig, 0, sizeof(vig));
        if (!shader_compile_asset(&vig, "vignette")) {
            ready = -1;
            return;
        }
        u_strength = glGetUniformLocation(vig.program, "u_strength");
        if (r->fog_quad_vao) {
            vao = r->fog_quad_vao;
        } else {
            float quad[] = { -1.f, -1.f,  1.f, -1.f,  -1.f, 1.f,  1.f, 1.f };
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glBindVertexArray(0);
        }
        ready = 1;
    }
    if (strength > 1.0f) strength = 1.0f;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_use(&vig);
    if (u_strength >= 0)
        glUniform1f(u_strength, strength);
    glBindVertexArray(vao ? vao : r->fog_quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
