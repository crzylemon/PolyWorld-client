/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: renderer.h                                                                          |
|   Purpose: GL. desktop, web, gles. it's a lot                                               |
\*-------------------------------------------------------------------------------------------*/

#ifndef RENDERER_H
#define RENDERER_H

#include "scene.h"
#include "shader.h"
#include "mesh_loader.h"
#include "math_types.h"
#include "texture.h"
#include "pw_gles.h"
#include <stdbool.h>
#include <stdint.h>

#define RENDERER_MAX_GLOW_LIGHTS 32
#define RENDERER_MAX_GLOW_SHADOW_LIGHTS 2
#define RENDERER_GLOW_SHADOW_FACE 256
#define RENDERER_MAX_FX_LIGHTS 8
#define RENDERER_MAX_ACC_GLOW_LIGHTS 24

#define RENDERER_GLOW_ACC_FLAG 0x80000000u

#define RENDERER_GLOW_LIGHT_STRENGTH 3.5f
#if PW_MOBILE

#define RENDERER_GLOW_RANGE_CAP 18.0f
#else
#define RENDERER_GLOW_RANGE_CAP 36.0f
#endif
#define RENDERER_MAX_EXTRA_CASTERS 512
#define RENDERER_SHADOW_SKIP_MAX 128
#define RENDERER_VOXEL_DIM  48
#define RENDERER_VOXEL_SIZE 4.0f
#define RENDERER_VOXEL_DIM_MIN 16
#define RENDERER_VOXEL_DIM_MAX 64

#define PW_CAMERA_NEAR     0.1f
#define PW_CAMERA_FAR      800.0f
#define PW_FOG_START_FRAC  0.28f
#define PW_FOG_END_FRAC    0.90f

typedef struct {
    EntityID id;
    Vec3 pos;
    Vec3 color;
    float range;
    float intensity;
    float score;
} RendererGlowLight;

typedef struct {
    Vec3 pos;
    Vec3 color;
    float range;
    float intensity;
} RendererFxLight;

typedef struct {
    Shader shader;
    int canvas_width;
    int canvas_height;
    Vec3 light_dir;
    Vec3 light_color;
    float clear_r, clear_g, clear_b, clear_a;
    TextureManager textures;

    bool is_studio;
    unsigned int studio_fbo;
    unsigned int studio_texture;
    unsigned int studio_rbo;

    bool host_present;

    unsigned int shadow_fbo;
    unsigned int shadow_id_tex;
    unsigned int shadow_depth_rb;
    unsigned int shadow_depth_tex;
    unsigned int shadow_depth_stub;
    Shader shadow_shader;
    int shadow_shader_u_id;
    int shadow_shader_u_face_expand;
    int shadow_shader_u_id_packed;
    int shadow_shader_u_light_dir;
    int shadow_shader_u_z_span;
    uint32_t current_shadow_id;
    int shadow_map_size;

    bool shadow_id_packed;
    float shadow_depth_bias;
    float shadow_esm_c;
    unsigned int shadow_color_internal;
    unsigned int shadow_color_format;
    unsigned int shadow_color_type;
    unsigned int shadow_depth_internal;
    Mat4 light_space_matrix;
    Mat4 shadow_view;
    Mat4 shadow_proj;

    unsigned int shadow_near_fbo;
    unsigned int shadow_near_id_tex;
    unsigned int shadow_near_depth_rb;
    unsigned int shadow_near_depth_tex;
    Mat4 light_space_near;
    Mat4 shadow_view_near;
    Mat4 shadow_proj_near;
    float shadow_near_range;
    float shadow_z_span;
    float shadow_z_span_near;
    bool shadows_enabled;
    bool voxel_enabled;
    unsigned int voxel_tex;
    unsigned char* voxel_occ;
    unsigned char* voxel_vis;
    int voxel_dim;
    float voxel_size;
    float voxel_range;
    int curve_tess_quality;
    Vec3 voxel_origin;
    bool voxel_origin_ok;
    bool fog_enabled;
    float shadow_range;
    int shadow_soft;
    struct {
        const GPUMesh* mesh;
        Mat4 model;
    } extra_casters[RENDERER_MAX_EXTRA_CASTERS];
    int extra_caster_count;

    struct {
        Vec3 pos;
        float radius;
        const GPUMesh* mesh;
    } shadow_extra_prev[RENDERER_MAX_EXTRA_CASTERS];
    int shadow_extra_prev_count;
    bool shadow_cache_far_ok;
    bool shadow_cache_near_ok;
    Vec3 shadow_cache_focus_far;
    Vec3 shadow_cache_focus_near;
    Vec3 shadow_cache_ld;
    float shadow_cache_range;
    float shadow_cache_near_r;
    int shadow_cache_map;
    float shadow_cache_far_nz, shadow_cache_far_fz;
    float shadow_cache_near_nz, shadow_cache_near_fz;
    Vec3* shadow_pose_pos;
    Vec3* shadow_pose_hint;
    float* shadow_pose_rad;
    unsigned char* shadow_pose_on;
    const GPUMesh** shadow_pose_mesh;
    uint32_t shadow_pose_cap;
    uint32_t shadow_pose_n;
    uint64_t shadow_world_stamp;
    EntityID shadow_skip[RENDERER_SHADOW_SKIP_MAX];
    int shadow_skip_count;

    int glow_leak_mode;
    int glow_light_max;
    int ssao_enabled;

    RendererGlowLight glow_lights[RENDERER_MAX_GLOW_LIGHTS];
    double glow_light_last_time;
    RendererFxLight fx_lights[RENDERER_MAX_FX_LIGHTS];
    int fx_light_count;
    RendererFxLight acc_glow_lights[RENDERER_MAX_ACC_GLOW_LIGHTS];
    int acc_glow_count;

    unsigned int glow_shadow_fbo;
    unsigned int glow_shadow_tex[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
    unsigned int glow_shadow_depth_tex;
    int glow_shadow_face;
    int glow_shadow_count;
    Mat4 glow_ls[RENDERER_MAX_GLOW_SHADOW_LIGHTS * 6];
    EntityID glow_cache_id[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
    Vec3 glow_cache_pos[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
    float glow_cache_range[RENDERER_MAX_GLOW_SHADOW_LIGHTS];
    int glow_cache_extras;
    Vec3 glow_cache_extra0;
    bool glow_shadow_cache_ok;
    float mesh_fx_glow;
    int mesh_fx_additive;
    int mesh_fx_no_cull;
    int mesh_fx_hold;
    float mesh_fx_uv[4];

    bool fog_world_pass;
    unsigned int scene_fbo;
    unsigned int scene_color_tex;
    unsigned int scene_depth_tex;
    unsigned int scene_fog_depth_tex;
    Shader fog_shader;
    int fog_u_color;
    int fog_u_depth;
    int fog_u_fog_depth;
    int fog_u_clear_color;
    int fog_u_near;
    int fog_u_far;
    int fog_u_start;
    int fog_u_end;
    unsigned int fog_quad_vao;
    unsigned int fog_quad_vbo;

    Shader ssao_shader;
    int ssao_u_depth;
    int ssao_u_noise;
    int ssao_u_projection;
    int ssao_u_inv_projection;
    int ssao_u_noise_scale;
    int ssao_u_samples;
    int ssao_u_radius;
    int ssao_u_bias;
    unsigned int ssao_noise_tex;
    float ssao_kernel[72];
    unsigned int ssao_fbo;
    unsigned int ssao_tex;
    unsigned int ssao_internal;
    unsigned int ssao_format;
    unsigned int ssao_type;
    Shader ssao_blur_shader;
    int ssao_blur_u_ssao;
    int ssao_blur_u_depth;
    int ssao_blur_u_inv_projection;

    bool frustum_cull_frozen;
    bool frustum_has_frozen;
    float frozen_frustum[6][4];

    float render_scale;
    int scene_w, scene_h;
    bool scale_active;
    unsigned int scale_fbo;
    unsigned int scale_color_tex;
    unsigned int scale_depth_rb;
} Renderer;

bool renderer_init(Renderer* r, int canvas_width, int canvas_height, bool editor);
void renderer_shutdown(Renderer* r);

bool renderer_recreate_gl(Renderer* r);
void renderer_resize(Renderer* r, int width, int height);
void renderer_begin_frame(Renderer* r);

void renderer_set_shadow_id(Renderer* r, uint32_t shadow_id);
void renderer_clear_fx_lights(Renderer* r);

void renderer_reset_lights(Renderer* r);
void renderer_add_fx_light(Renderer* r, Vec3 pos, Vec3 color, float range, float intensity);
void renderer_add_acc_glow_light(Renderer* r, Vec3 pos, Vec3 color, float range, float intensity);

void renderer_set_mesh_fx(Renderer* r, float glow, int additive, int no_cull);
void renderer_set_mesh_uv_rect(Renderer* r, float u, float v, float su, float sv);

void renderer_shadow_skip_reset(Renderer* r);
void renderer_shadow_skip_add(Renderer* r, EntityID id);

void renderer_shadow_pass(Renderer* r, const Scene* scene, Vec3 focus_pos);

void renderer_voxel_update(Renderer* r, const Scene* scene, Vec3 focus_pos);
void renderer_set_voxel_range(Renderer* r, float range);
void renderer_shadow_cast_begin(Renderer* r);
void renderer_shadow_cast_begin_near(Renderer* r);

void renderer_shadow_cast_mesh(Renderer* r, const GPUMesh* mesh, const Mat4* model);
void renderer_shadow_cast_end(Renderer* r);

static inline uint32_t renderer_shadow_id_entity(EntityID eid) {
    return (eid == ENTITY_INVALID) ? 0u : (uint32_t)eid + 1u;
}
static inline uint32_t renderer_shadow_id_entity_face(EntityID eid, int face) {
    uint32_t base = renderer_shadow_id_entity(eid);
    if (base == 0u) return 0u;
    return base * 8u + (uint32_t)(face & 7);
}

static inline uint32_t renderer_shadow_id_avatar(int player_slot, int part) {
    (void)part;

    return 16000000u + (uint32_t)(player_slot + 1);
}
static inline uint32_t renderer_shadow_id_accessory(int player_slot, int part) {
    (void)part;
    return 16000000u + (uint32_t)(player_slot + 1);
}

void renderer_render_scene(Renderer* r, const Scene* scene, const Mat4* view, const Mat4* projection);

typedef void (*RendererMidDrawFn)(void* user);
void renderer_render_scene_ex(Renderer* r, const Scene* scene, const Mat4* view, const Mat4* projection,
                              RendererMidDrawFn mid, void* mid_user);

void renderer_set_frustum_cull_frozen(Renderer* r, bool frozen);
bool renderer_get_frustum_cull_frozen(const Renderer* r);

void renderer_set_shadow_map_size(Renderer* r, int size);

void renderer_end_frame(Renderer* r);

void renderer_set_host_present(Renderer* r, bool on);
unsigned int renderer_host_fbo(const Renderer* r);
unsigned int renderer_host_color_tex(const Renderer* r);

void renderer_apply_corner_ao(Renderer* r, const Mat4* projection);

void renderer_begin_world_pass(Renderer* r);
void renderer_apply_fog(Renderer* r, float near_plane, float far_plane);

void renderer_set_render_scale(Renderer* r, float scale);
void renderer_present_scaled_3d(Renderer* r);

void renderer_draw_comfort_vignette(Renderer* r, float strength);

void renderer_debug_box(Renderer* r, Vec3 pos, Vec3 half_extents, Vec3 rotation, Vec3 color,
                        const Mat4* view, const Mat4* projection);
void renderer_debug_line(Renderer* r, Vec3 start, Vec3 end, Vec3 color,
                         const Mat4* view, const Mat4* projection);

void renderer_debug_box_matrix(Renderer* r, const float* model_matrix, Vec3 color,
                               const Mat4* view, const Mat4* projection);

void renderer_debug_sphere(Renderer* r, Vec3 pos, float radius, Vec3 color,
                           const Mat4* view, const Mat4* projection);

void renderer_draw_mesh(Renderer* r, const GPUMesh* mesh, const Mat4* model,
                        Vec3 color, uint32_t texture_id, int texture_mode,
                        const Mat4* view, const Mat4* projection);

void renderer_draw_mesh_alpha(Renderer* r, const GPUMesh* mesh, const Mat4* model,
                              Vec3 color, uint32_t texture_id, int texture_mode,
                              const Mat4* view, const Mat4* projection, float alpha);

bool mesh_upload(const MeshData* data, GPUMesh* out);

void mesh_gpu_free(GPUMesh* mesh);

GPUMesh* renderer_unit_curve_mesh(int prim_kind, int lod);
void renderer_invalidate_curve_meshes(void);

#endif
