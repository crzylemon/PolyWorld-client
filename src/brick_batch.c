/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: brick_batch.c                                                                       |
|   Purpose: static box batching. Off on Android.                                             |
\*-------------------------------------------------------------------------------------------*/

#include "brick_batch.h"
#include "math_types.h"
#include "renderer.h"
#include "log.h"
#include "platform.h"
#include "pw_gles.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#if defined(__EMSCRIPTEN__)
#define BRICK_BATCH_ENABLED 1
#else
#define BRICK_BATCH_ENABLED 0
#endif

#if !BRICK_BATCH_ENABLED

void brick_batch_init(void) {}
void brick_batch_shutdown(void) {}
void brick_batch_rebuild(Scene* scene) { (void)scene; }
void brick_batch_clear(Scene* scene) {
    if (!scene) return;
    for (uint32_t i = 0; i < scene->count; i++) {
        if (scene->entities[i].active)
            scene->entities[i].render_batched = false;
    }
}
void brick_batch_mark_dirty(void) {}
void brick_batch_update(Scene* scene) { (void)scene; }
bool brick_batch_active(void) { return false; }
bool brick_batch_is_building(void) { return false; }
void brick_batch_draw(TextureManager* textures, int u_model, int u_color, int u_glow,
                      int u_alpha, int u_has_texture, int u_shadow_id, int u_normal_map,
                      const float* view_proj_frustum_optional,
                      const float identity_model[16]) {
    (void)textures; (void)u_model; (void)u_color; (void)u_glow; (void)u_alpha;
    (void)u_has_texture; (void)u_shadow_id; (void)u_normal_map;
    (void)view_proj_frustum_optional; (void)identity_model;
}

#else

#define BRICK_CHUNK_SIZE     32.0f
#define BRICK_MESH_MAX       2048
#define BRICK_SURFACES       3
#if defined(__ANDROID__)
#define BRICK_SLICE_SEC      0.004
#define BRICKS_PER_FRAME     8
#define FLUSHES_PER_FRAME    1
#define BRICK_MAX_VERTS      1536
#define BRICK_BUILD_TIMEOUT  20.0
#else
#define BRICK_SLICE_SEC      0.012
#define BRICKS_PER_FRAME     64
#define FLUSHES_PER_FRAME    2
#define BRICK_MAX_VERTS      8192
#define BRICK_BUILD_TIMEOUT  60.0
#endif

typedef struct {
    float* positions;
    float* normals;
    float* texcoords;
    float* colors;
    uint32_t* indices;
    size_t vert_count;
    size_t vert_cap;
    size_t idx_count;
    size_t idx_cap;
} SurfBuilder;

typedef struct {
    GPUMesh mesh;
    int surface;
    bool used;
} BrickMesh;

typedef struct {
    int cx, cy, cz;
    uint32_t ent;
} BrickItem;

static BrickMesh g_meshes[BRICK_MESH_MAX];
static int g_mesh_n = 0;
static bool g_active = false;

static bool g_dirty = false;
static bool g_building = false;
static bool g_restart_when_done = false;

static BrickItem* g_items = NULL;
static int g_n_items = 0;
static int g_item_i = 0;
static SurfBuilder g_builders[BRICK_SURFACES];
static int g_cur_cx, g_cur_cy, g_cur_cz;
static bool g_have_cur = false;
static Scene* g_build_scene = NULL;
static double g_build_start_time = 0.0;

static const int k_face_to_surface[6] = { 2, 3, 0, 1, 5, 4 };

static void builder_free(SurfBuilder* b) {
    free(b->positions);
    free(b->normals);
    free(b->texcoords);
    free(b->colors);
    free(b->indices);
    memset(b, 0, sizeof(*b));
}

static bool builder_reserve(SurfBuilder* b, size_t add_v, size_t add_i) {
    if (b->vert_count + add_v > b->vert_cap) {
        size_t nc = b->vert_cap ? b->vert_cap * 2 : 512;
        while (nc < b->vert_count + add_v) nc *= 2;
        float* p = (float*)realloc(b->positions, nc * 3 * sizeof(float));
        if (!p) return false;
        b->positions = p;
        float* n = (float*)realloc(b->normals, nc * 3 * sizeof(float));
        if (!n) return false;
        b->normals = n;
        float* t = (float*)realloc(b->texcoords, nc * 2 * sizeof(float));
        if (!t) return false;
        b->texcoords = t;
        float* c = (float*)realloc(b->colors, nc * 3 * sizeof(float));
        if (!c) return false;
        b->colors = c;
        b->vert_cap = nc;
    }
    if (b->idx_count + add_i > b->idx_cap) {
        size_t nc = b->idx_cap ? b->idx_cap * 2 : 768;
        while (nc < b->idx_count + add_i) nc *= 2;
        uint32_t* ix = (uint32_t*)realloc(b->indices, nc * sizeof(uint32_t));
        if (!ix) return false;
        b->indices = ix;
        b->idx_cap = nc;
    }
    return true;
}

static void meshes_clear(void) {
    for (int i = 0; i < g_mesh_n; i++) {
        if (g_meshes[i].used)
            mesh_gpu_free(&g_meshes[i].mesh);
        memset(&g_meshes[i], 0, sizeof(g_meshes[i]));
    }
    g_mesh_n = 0;
}

static void abort_build(Scene* scene) {
    for (int s = 0; s < BRICK_SURFACES; s++)
        builder_free(&g_builders[s]);
    free(g_items);
    g_items = NULL;
    g_n_items = 0;
    g_item_i = 0;
    g_building = false;
    g_have_cur = false;
    g_build_scene = NULL;
    meshes_clear();
    g_active = false;
    if (scene) {
        for (uint32_t i = 0; i < scene->count; i++) {
            if (scene->entities[i].active)
                scene->entities[i].render_batched = false;
        }
    }
}

void brick_batch_init(void) {
    brick_batch_clear(NULL);
}

void brick_batch_shutdown(void) {
    brick_batch_clear(NULL);
}

bool brick_batch_active(void) {
    return g_active && g_mesh_n > 0;
}

bool brick_batch_is_building(void) {
    return g_building;
}

void brick_batch_mark_dirty(void) {
    if (g_building)
        g_restart_when_done = true;
    else
        g_dirty = true;
}

void brick_batch_clear(Scene* scene) {
    abort_build(scene);
    g_dirty = false;
    g_restart_when_done = false;
}

static bool flush_builder(SurfBuilder* b, int surface) {
    if (b->vert_count == 0 || b->idx_count == 0) {
        builder_free(b);
        return true;
    }
    if (g_mesh_n >= BRICK_MESH_MAX) {
        builder_free(b);
        return false;
    }

    MeshData md;
    memset(&md, 0, sizeof(md));
    md.positions = b->positions;
    md.normals = b->normals;
    md.texcoords = b->texcoords;
    md.colors = b->colors;
    md.indices = b->indices;
    md.vertex_count = b->vert_count;
    md.index_count = b->idx_count;

    BrickMesh* out = &g_meshes[g_mesh_n];
    memset(out, 0, sizeof(*out));
    if (!mesh_upload(&md, &out->mesh)) {
        builder_free(b);
        return false;
    }
    free(out->mesh.cpu_positions);
    free(out->mesh.cpu_indices);
    out->mesh.cpu_positions = NULL;
    out->mesh.cpu_indices = NULL;
    out->mesh.cpu_vertex_count = 0;

    out->surface = surface;
    out->used = true;
    g_mesh_n++;
    builder_free(b);
    return true;
}

static int append_box_entity(SurfBuilder builders[BRICK_SURFACES], const Entity* e, const Mat4* world) {
    const GPUMesh* src = e->mesh;
    if (!src || !src->cpu_positions || src->cpu_vertex_count < 24 || src->index_count != 36)
        return -1;

    for (int face = 0; face < 6; face++) {
        int surf = e->material.surfaces[k_face_to_surface[face]];
        if (surf < 0) surf = 0;
        if (surf > 2) surf = 2;
        if (builders[surf].vert_count + 4 > BRICK_MAX_VERTS)
            return 0;
    }

    float cr = e->material.color.x;
    float cg = e->material.color.y;
    float cb = e->material.color.z;
    float sx = (src->aabb_max[0] - src->aabb_min[0]) * fabsf(e->transform.scale.x);
    float sy = (src->aabb_max[1] - src->aabb_min[1]) * fabsf(e->transform.scale.y);
    float sz = (src->aabb_max[2] - src->aabb_min[2]) * fabsf(e->transform.scale.z);
    if (sx < 1e-4f) sx = 1.0f;
    if (sy < 1e-4f) sy = 1.0f;
    if (sz < 1e-4f) sz = 1.0f;

    for (int face = 0; face < 6; face++) {
        int surf = e->material.surfaces[k_face_to_surface[face]];
        if (surf < 0) surf = 0;
        if (surf > 2) surf = 2;
        SurfBuilder* b = &builders[surf];
        if (!builder_reserve(b, 4, 6)) return -1;

        float face_u, face_v;
        if (face <= 1) { face_u = sx; face_v = sy; }
        else if (face <= 3) { face_u = sx; face_v = sz; }
        else { face_u = sz; face_v = sy; }
        float uvs[8] = { 0, 0, face_u, 0, face_u, face_v, 0, face_v };

        float fn[3] = {0};
        if (face == 0) fn[2] = 1;
        else if (face == 1) fn[2] = -1;
        else if (face == 2) fn[1] = 1;
        else if (face == 3) fn[1] = -1;
        else if (face == 4) fn[0] = 1;
        else fn[0] = -1;
        Vec4 wn = mat4_mul_vec4(*world, (Vec4){ fn[0], fn[1], fn[2], 0.0f });
        float nlen = sqrtf(wn.x * wn.x + wn.y * wn.y + wn.z * wn.z);
        if (nlen > 1e-6f) { wn.x /= nlen; wn.y /= nlen; wn.z /= nlen; }

        uint32_t base = (uint32_t)b->vert_count;
        for (int vi = 0; vi < 4; vi++) {
            int src_i = face * 4 + vi;
            Vec4 wp = mat4_mul_vec4(*world, (Vec4){
                src->cpu_positions[src_i * 3 + 0],
                src->cpu_positions[src_i * 3 + 1],
                src->cpu_positions[src_i * 3 + 2],
                1.0f
            });
            size_t vo = b->vert_count;
            b->positions[vo * 3 + 0] = wp.x;
            b->positions[vo * 3 + 1] = wp.y;
            b->positions[vo * 3 + 2] = wp.z;
            b->normals[vo * 3 + 0] = wn.x;
            b->normals[vo * 3 + 1] = wn.y;
            b->normals[vo * 3 + 2] = wn.z;
            b->texcoords[vo * 2 + 0] = uvs[vi * 2 + 0];
            b->texcoords[vo * 2 + 1] = uvs[vi * 2 + 1];
            b->colors[vo * 3 + 0] = cr;
            b->colors[vo * 3 + 1] = cg;
            b->colors[vo * 3 + 2] = cb;
            b->vert_count++;
        }
        b->indices[b->idx_count++] = base + 0;
        b->indices[b->idx_count++] = base + 1;
        b->indices[b->idx_count++] = base + 2;
        b->indices[b->idx_count++] = base + 0;
        b->indices[b->idx_count++] = base + 2;
        b->indices[b->idx_count++] = base + 3;
    }
    return 1;
}

static int brick_item_cmp(const void* a, const void* b) {
    const BrickItem* ia = (const BrickItem*)a;
    const BrickItem* ib = (const BrickItem*)b;
    if (ia->cx != ib->cx) return (ia->cx < ib->cx) ? -1 : 1;
    if (ia->cy != ib->cy) return (ia->cy < ib->cy) ? -1 : 1;
    if (ia->cz != ib->cz) return (ia->cz < ib->cz) ? -1 : 1;
    return 0;
}

static void clear_batched_flags(Scene* scene) {
    if (!scene) return;
    for (uint32_t i = 0; i < scene->count; i++) {
        if (scene->entities[i].active)
            scene->entities[i].render_batched = false;
    }
}

static bool begin_build(Scene* scene) {
    abort_build(NULL);
    clear_batched_flags(scene);
    g_dirty = false;

    if (!scene || scene->count == 0) return false;

    g_items = (BrickItem*)malloc(sizeof(BrickItem) * (size_t)scene->count);
    if (!g_items) {
        PW_ERR(ERR_GENERIC, "[brick_batch] alloc failed\n");
        return false;
    }

    g_n_items = 0;
    for (uint32_t i = 0; i < scene->count; i++) {
        Entity* e = &scene->entities[i];
        if (!e->active || !e->mesh || !e->static_batch) continue;
        if (e->material.alpha < 0.99f || e->material.glow >= 0.05f) continue;
        if (e->material.texture_id != 0) continue;
        if (e->material.part_material != 0) continue;
        if (e->mesh->index_count != 36 || !e->mesh->has_texcoords) continue;
        if (!e->mesh->cpu_positions || e->mesh->cpu_vertex_count < 24) continue;

        g_items[g_n_items].cx = (int)floorf(e->transform.position.x / BRICK_CHUNK_SIZE);
        g_items[g_n_items].cy = (int)floorf(e->transform.position.y / BRICK_CHUNK_SIZE);
        g_items[g_n_items].cz = (int)floorf(e->transform.position.z / BRICK_CHUNK_SIZE);
        g_items[g_n_items].ent = i;
        g_n_items++;
    }

    if (g_n_items == 0) {
        free(g_items);
        g_items = NULL;
        return false;
    }

    qsort(g_items, (size_t)g_n_items, sizeof(BrickItem), brick_item_cmp);
    memset(g_builders, 0, sizeof(g_builders));
    g_item_i = 0;
    g_have_cur = false;
    g_building = true;
    g_build_scene = scene;
    g_active = false;
    g_build_start_time = platform_get_time();
    PW_LOG("[brick_batch] start incremental build (%d bricks)\n", g_n_items);
    return true;
}

static void finish_build_ok(void) {

    if (g_build_scene && g_items) {
        for (int i = 0; i < g_n_items; i++) {
            uint32_t ei = g_items[i].ent;
            if (ei >= g_build_scene->count) continue;
            Entity* e = &g_build_scene->entities[ei];
            if (e->active && e->static_batch)
                e->render_batched = true;
        }
    }

    free(g_items);
    g_items = NULL;
    g_n_items = 0;
    g_item_i = 0;
    g_building = false;
    g_have_cur = false;
    g_build_scene = NULL;
    g_active = (g_mesh_n > 0);
    PW_LOG("[brick_batch] done: %d meshes\n", g_mesh_n);

    if (g_restart_when_done) {
        g_restart_when_done = false;
        g_dirty = true;
    }
}

static void step_build(Scene* scene) {
    if (!g_building || !g_items || !scene) return;

    if (platform_get_time() - g_build_start_time > BRICK_BUILD_TIMEOUT) {
        PW_ERR(ERR_GENERIC, "[brick_batch] build timed out. falling back to per-brick draws\n");
        abort_build(scene);
        return;
    }

    int bricks_left = BRICKS_PER_FRAME;
    int flushes_left = FLUSHES_PER_FRAME;

    while (bricks_left > 0 && g_item_i < g_n_items) {
        BrickItem* it = &g_items[g_item_i];

        if (g_have_cur && (it->cx != g_cur_cx || it->cy != g_cur_cy || it->cz != g_cur_cz)) {
            if (flushes_left <= 0) break;
            for (int s = 0; s < BRICK_SURFACES; s++) {
                if (!flush_builder(&g_builders[s], s)) {
                    PW_ERR(ERR_GENERIC, "[brick_batch] flush failed. abort\n");
                    abort_build(scene);
                    return;
                }
            }
            flushes_left--;
            g_have_cur = false;
            g_active = (g_mesh_n > 0);
            break;
        }

        if (!g_have_cur) {
            g_cur_cx = it->cx;
            g_cur_cy = it->cy;
            g_cur_cz = it->cz;
            g_have_cur = true;
        }

        Entity* e = &scene->entities[it->ent];
        if (!e->active || !e->mesh || !e->static_batch) {
            g_item_i++;
            bricks_left--;
            continue;
        }

        Mat4 world = scene_get_world_matrix(scene, e->id);
        int ar = append_box_entity(g_builders, e, &world);
        if (ar == 0) {

            if (flushes_left <= 0) break;
            for (int s = 0; s < BRICK_SURFACES; s++) {
                if (g_builders[s].vert_count + 4 > BRICK_MAX_VERTS ||
                    g_builders[s].vert_count > BRICK_MAX_VERTS / 2) {
                    if (!flush_builder(&g_builders[s], s)) {
                        abort_build(scene);
                        return;
                    }
                }
            }
            flushes_left--;
            g_active = (g_mesh_n > 0);
            break;
        }
        if (ar < 0) {
            PW_ERR(ERR_GENERIC, "[brick_batch] append failed. abort\n");
            abort_build(scene);
            return;
        }

        g_item_i++;
        bricks_left--;
        g_active = (g_mesh_n > 0);
    }

    if (g_item_i >= g_n_items) {
        for (int s = 0; s < BRICK_SURFACES; s++) {
            if (g_builders[s].vert_count == 0) continue;
            if (!flush_builder(&g_builders[s], s)) {
                PW_ERR(ERR_GENERIC, "[brick_batch] final flush failed. abort\n");
                abort_build(scene);
                return;
            }
            g_active = (g_mesh_n > 0);
            return;
        }
        finish_build_ok();
    }
}

void brick_batch_rebuild(Scene* scene) {
    (void)scene;
    brick_batch_mark_dirty();
}

void brick_batch_update(Scene* scene) {
    if (!scene) return;

    if (g_building) {
        if (scene != g_build_scene) {
            abort_build(scene);
            return;
        }
        step_build(scene);
        return;
    }

    if (g_dirty) {
        if (!begin_build(scene))
            g_dirty = false;
        else
            step_build(scene);
    }
}

void brick_batch_draw(TextureManager* textures, int u_model, int u_color, int u_glow,
                      int u_alpha, int u_has_texture, int u_shadow_id, int u_normal_map,
                      const float* view_proj_frustum_optional,
                      const float identity_model[16]) {
    (void)view_proj_frustum_optional;
    if (!g_active || !textures) return;

    Mat4 ident = mat4_identity();
    const float* model = identity_model ? identity_model : ident.m;

    for (int i = 0; i < g_mesh_n; i++) {
        BrickMesh* bm = &g_meshes[i];
        if (!bm->used || !bm->mesh.vao || bm->mesh.index_count == 0) continue;

        if (u_model >= 0) glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
        if (u_color >= 0) glUniform3f(u_color, 1.0f, 1.0f, 1.0f);
        if (u_glow >= 0) glUniform1f(u_glow, 0.0f);
        if (u_alpha >= 0) glUniform1f(u_alpha, 1.0f);
        if (u_shadow_id >= 0) glUniform1ui(u_shadow_id, 0);

        TextureID tex = texture_get_for_surface(textures, bm->surface);
        if (tex != TEXTURE_INVALID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            if (u_has_texture >= 0) glUniform1i(u_has_texture, 1);
        } else {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (u_has_texture >= 0) glUniform1i(u_has_texture, 0);
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (u_normal_map >= 0) glUniform1i(u_normal_map, 2);
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(bm->mesh.vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)bm->mesh.index_count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

#endif
