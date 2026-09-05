/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_hub.c                                                                            |
|   Purpose: VR lobby. Local copy of game 159, UI on the wall.                                |
\*-------------------------------------------------------------------------------------------*/

#if defined(VR) && !defined(__EMSCRIPTEN__)

#include "vr_hub.h"
#include "login_screen.h"
#include "log.h"
#include "mesh_loader.h"
#include "mesh_primitives.h"
#include "platform.h"
#include "renderer.h"
#include "texture.h"
#include "world_loader.h"
#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VR_HUB_LASER_MAX 48.0f
#define VR_HUB_CURSOR 0.08f

static bool g_active;
static EntityID g_display = ENTITY_INVALID;
static unsigned g_fbo, g_color, g_depth, g_dot, g_logo;
static int g_logo_w, g_logo_h;
static bool g_logo_tried;
static GPUMesh g_quad;
static bool g_quad_ready;
static bool g_dot_ready;
static bool g_hit;
static float g_u, g_v;
static Vec3 g_from, g_to;

static Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }

static Vec3 quat_rotate(const PwVrTracker* t, Vec3 v) {
    Vec3 u = {t->qx, t->qy, t->qz};
    Vec3 uv = vec3_cross(u, v);
    Vec3 uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * t->qw);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

static void destroy_fbo(bool gl_alive) {
    if (gl_alive) {
        if (g_color) glDeleteTextures(1, &g_color);
        if (g_dot) glDeleteTextures(1, &g_dot);
        if (g_logo) glDeleteTextures(1, &g_logo);
        if (g_depth) glDeleteRenderbuffers(1, &g_depth);
        if (g_fbo) glDeleteFramebuffers(1, &g_fbo);
        if (g_quad_ready) mesh_gpu_free(&g_quad);
    }
    g_fbo = g_color = g_depth = g_dot = g_logo = 0;
    g_logo_w = g_logo_h = 0;
    g_logo_tried = false;
    memset(&g_quad, 0, sizeof(g_quad));
    g_quad_ready = false;
    g_dot_ready = false;
}

static bool ensure_fbo(void) {
    if (g_fbo) return true;
    glGenTextures(1, &g_color);
    glBindTexture(GL_TEXTURE_2D, g_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VR_HUB_FB_W, VR_HUB_FB_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &g_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, VR_HUB_FB_W, VR_HUB_FB_H);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth);
    GLenum ok = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    if (ok != GL_FRAMEBUFFER_COMPLETE) {
        PW_WARN("[VR hub] framebuffer incomplete (%u)\n", (unsigned)ok);
        destroy_fbo(true);
        return false;
    }
    return true;
}

static bool ensure_quad(void) {
    if (g_quad_ready) return true;
    MeshData md;
    memset(&md, 0, sizeof(md));
    md.vertex_count = 4;
    md.index_count = 6;
    md.positions = (float*)malloc(4 * 3 * sizeof(float));
    md.normals = (float*)malloc(4 * 3 * sizeof(float));
    md.texcoords = (float*)malloc(4 * 2 * sizeof(float));
    md.indices = (uint32_t*)malloc(6 * sizeof(uint32_t));
    if (!md.positions || !md.normals || !md.texcoords || !md.indices) {
        mesh_data_free(&md);
        return false;
    }

    float verts[] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f,
    };
    float norms[] = { 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
    float uvs[] = { 0,0,  1,0,  1,1,  0,1 };
    uint32_t idxs[] = { 0, 1, 2, 0, 2, 3 };
    memcpy(md.positions, verts, sizeof(verts));
    memcpy(md.normals, norms, sizeof(norms));
    memcpy(md.texcoords, uvs, sizeof(uvs));
    memcpy(md.indices, idxs, sizeof(idxs));
    memset(&g_quad, 0, sizeof(g_quad));
    if (!mesh_upload(&md, &g_quad)) {
        mesh_data_free(&md);
        return false;
    }
    mesh_data_free(&md);
    g_quad_ready = true;
    return true;
}

static void on_hub_logo(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    (void)user;
    if (!data || len == 0) return;
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    extern void stbi_set_flip_vertically_on_load(int);
    int w = 0, h = 0, c = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!px) return;
    g_logo = texture_load_from_memory(px, w, h, 4);
    g_logo_w = w;
    g_logo_h = h;
    stbi_image_free(px);
}

static bool ensure_logo(void) {
    if (g_logo) return true;
    if (g_logo_tried) return false;
    g_logo_tried = true;
    platform_load_file("assets/polyworld_logo.png", on_hub_logo, NULL);
    return g_logo != 0;
}

static bool ensure_dot(void) {
    if (g_dot_ready && g_dot) return true;
    const int n = 64;
    unsigned char* px = (unsigned char*)malloc((size_t)n * (size_t)n * 4);
    if (!px) return false;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float u = ((float)x + 0.5f) / (float)n * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / (float)n * 2.0f - 1.0f;
            float d = sqrtf(u * u + v * v);
            float a = 1.0f - d;
            if (a < 0.0f) a = 0.0f;
            a = a * a * (3.0f - 2.0f * a);
            int i = (y * n + x) * 4;
            px[i + 0] = 255;
            px[i + 1] = 255;
            px[i + 2] = 255;
            px[i + 3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
    glGenTextures(1, &g_dot);
    glBindTexture(GL_TEXTURE_2D, g_dot);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(px);
    g_dot_ready = true;
    return true;
}

void vr_hub_shutdown(void) {
    destroy_fbo(true);
    g_active = false;
    g_display = ENTITY_INVALID;
    g_hit = false;
}

void vr_hub_invalidate_gl(bool context_alive) {
    destroy_fbo(context_alive);
}

bool vr_hub_active(void) { return g_active; }

void vr_hub_set_active(bool on) {
    g_active = on;
    if (!on) {
        g_display = ENTITY_INVALID;
        g_hit = false;
    }
}

void vr_hub_on_world_loaded(void) {
    g_display = world_loader_display_ui_id();
    if (g_display == ENTITY_INVALID)
        PW_WARN("[VR hub] DISPLAY_UI part not found in hub XML\n");
}

bool vr_hub_laser(const PwVrPose* pose, const Scene* scene,
                  float* out_u, float* out_v,
                  Vec3* out_from, Vec3* out_to) {
    g_hit = false;
    g_u = g_v = 0.5f;
    if (!pose || !scene || !(pose->flags & PW_VR_FLAG_RHAND)) return false;

    Vec3 origin = v3(pose->rhand.x, pose->rhand.y, pose->rhand.z);
    Vec3 dir = vec3_normalize(quat_rotate(&pose->rhand, v3(0.0f, 0.0f, -1.0f)));
    g_from = origin;
    g_to = vec3_add(origin, vec3_scale(dir, VR_HUB_LASER_MAX));

    Entity* ent = scene_get_entity((Scene*)scene, g_display);
    if (!ent || !ent->active) {
        if (out_from) *out_from = g_from;
        if (out_to) *out_to = g_to;
        return false;
    }

    Mat4 M = scene_get_world_matrix(scene, g_display);
    Mat4 inv = mat4_inverse(M);

    Vec4 n4 = mat4_mul_vec4(M, (Vec4){0.0f, 0.0f, 1.0f, 0.0f});
    Vec3 N = vec3_normalize(v3(n4.x, n4.y, n4.z));
    Vec4 p4 = mat4_mul_vec4(M, (Vec4){0.0f, 0.0f, 0.52f, 1.0f});
    Vec3 P = v3(p4.x, p4.y, p4.z);

    float denom = vec3_dot(dir, N);
    if (denom > -0.004f) {
        if (out_from) *out_from = g_from;
        if (out_to) *out_to = g_to;
        return false;
    }
    float t = vec3_dot(vec3_sub(P, origin), N) / denom;
    if (t < 0.02f || t > VR_HUB_LASER_MAX) {
        if (out_from) *out_from = g_from;
        if (out_to) *out_to = g_to;
        return false;
    }
    Vec3 hit = vec3_add(origin, vec3_scale(dir, t));
    Vec4 lh = mat4_mul_vec4(inv, (Vec4){hit.x, hit.y, hit.z, 1.0f});
    const float pad = 0.18f;
    if (fabsf(lh.z - 0.52f) > 0.20f || fabsf(lh.x) > 0.5f + pad || fabsf(lh.y) > 0.5f + pad) {
        if (out_from) *out_from = g_from;
        if (out_to) *out_to = g_to;
        return false;
    }

    g_hit = true;
    g_u = lh.x + 0.5f;
    g_v = lh.y + 0.5f;
    if (g_u < 0.0f) g_u = 0.0f;
    if (g_u > 1.0f) g_u = 1.0f;
    if (g_v < 0.0f) g_v = 0.0f;
    if (g_v > 1.0f) g_v = 1.0f;
    g_to = hit;
    if (out_u) *out_u = g_u;
    if (out_v) *out_v = g_v;
    if (out_from) *out_from = g_from;
    if (out_to) *out_to = g_to;
    return true;
}

void vr_hub_render_ui(LoginScreen* ls) {
    if (!ls || !g_active) return;
    if (!ensure_fbo()) return;
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    login_screen_render_to(ls, VR_HUB_FB_W, VR_HUB_FB_H, g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
}

void vr_hub_draw(Renderer* r, const Scene* scene,
                 const Mat4* view, const Mat4* projection,
                 Vec3 laser_from, Vec3 laser_to, bool hit, float u, float v) {
    if (!r || !scene || !view || !projection || !g_active) return;
    Entity* ent = scene_get_entity((Scene*)scene, g_display);
    if (ent && ent->active && ensure_quad() && ensure_logo()) {

        float aspect = (g_logo_h > 0) ? ((float)g_logo_w / (float)g_logo_h) : 3.86f;
        float logo_h = 5.2f;
        float logo_w = logo_h * aspect;
        Vec3 pos = ent->transform.position;
        pos.x += 0.85f;
        pos.y += 6.55f;
        Mat4 logo_m = mat4_multiply(mat4_translate(pos),
                      mat4_multiply(mat4_rotate_y(ent->transform.rotation.y),
                                    mat4_scale(v3(logo_w, logo_h, 1.0f))));
        float prev_glow = r->mesh_fx_glow;
        int prev_cull = r->mesh_fx_no_cull;
        r->mesh_fx_glow = 0.28f;
        r->mesh_fx_no_cull = 1;
        r->mesh_fx_uv[0] = 0.0f;
        r->mesh_fx_uv[1] = 0.0f;
        r->mesh_fx_uv[2] = 1.0f;
        r->mesh_fx_uv[3] = 1.0f;
        renderer_draw_mesh_alpha(r, &g_quad, &logo_m, v3(1, 1, 1),
                                 g_logo, 4, view, projection, 1.0f);
        r->mesh_fx_glow = prev_glow;
        r->mesh_fx_no_cull = prev_cull;
    }
    if (ent && ent->active && g_color && ensure_quad()) {
        Mat4 M = scene_get_world_matrix(scene, g_display);
        Mat4 off = mat4_translate(v3(0.0f, 0.0f, 0.52f));
        Mat4 model = mat4_multiply(M, off);
        float prev_glow = r->mesh_fx_glow;
        int prev_cull = r->mesh_fx_no_cull;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        r->mesh_fx_glow = 0.15f;
        r->mesh_fx_no_cull = 1;
        r->mesh_fx_uv[0] = 0.0f;
        r->mesh_fx_uv[1] = 0.0f;
        r->mesh_fx_uv[2] = 1.0f;
        r->mesh_fx_uv[3] = 1.0f;

        renderer_draw_mesh(r, &g_quad, &model, v3(1, 1, 1), g_color, 4, view, projection);
        if (hit && ensure_dot()) {
            Mat4 cur_t = mat4_translate(v3(u - 0.5f, v - 0.5f, 0.54f));
            Mat4 cur_s = mat4_scale(v3(0.07f, 0.07f * (16.0f / 9.0f), 1.0f));
            Mat4 cur = mat4_multiply(M, mat4_multiply(cur_t, cur_s));
            r->mesh_fx_glow = 0.35f;
            r->mesh_fx_additive = 1;
            r->mesh_fx_no_cull = 1;
            renderer_draw_mesh_alpha(r, &g_quad, &cur, v3(0.45f, 1.0f, 0.20f),
                                     g_dot, 4, view, projection, 1.0f);
        }
        r->mesh_fx_glow = prev_glow;
        r->mesh_fx_no_cull = prev_cull;
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
    Vec3 lime = hit ? v3(0.55f, 1.0f, 0.15f) : v3(1.0f, 0.35f, 0.15f);
    renderer_debug_line(r, laser_from, laser_to, lime, view, projection);
    (void)VR_HUB_CURSOR;
}

#else
typedef int pw_vr_hub_tu;
#endif
