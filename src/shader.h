/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: shader.h                                                                            |
|   Purpose: shaders + uniform cache                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef SHADER_H
#define SHADER_H

#include <stdbool.h>

typedef struct {
    unsigned int program;

    int u_model;
    int u_view;
    int u_projection;
    int u_color;
    int u_light_dir;
    int u_light_color;
    int u_texture;
    int u_has_texture;
    int u_uv_rect;
    int u_face_mode;
    int u_face_surf;
    int u_part_shape;
    int u_part_size;
    int u_inlet_map;
    int u_mat_albedo;
    int u_mat_normal;
    int u_mat_specular;
    int u_part_material;
    int u_camera_pos;
    int u_fog_color;
    int u_fog_start;
    int u_fog_end;
    int u_shadow_map;
    int u_shadow_map_near;
    int u_shadow_group;
    int u_shadow_group_near;
    int u_light_space;
    int u_light_space_near;
    int u_shadow_enabled;
    int u_shadow_soft;
    int u_shadow_cascades;
    int u_shadow_id;
    int u_shadow_face_ids;
    int u_shadow_id_packed;
    int u_shadow_depth_bias;
    int u_shadow_exp;
    int u_shadow_range;
    int u_fog_enabled;
    int u_normal_map;
    int u_glow;
    int u_alpha;
    int u_contact_shade;
    int u_glow_light_count;
    int u_glow_light_pos;
    int u_glow_light_color;
    int u_glow_light_range;
    int u_glow_light_entity;
    int u_glow_shadow_count;
    int u_glow_shadow_map0;
    int u_glow_shadow_map1;
    int u_glow_ls;
    int u_voxel_map;
    int u_voxel_enabled;
    int u_voxel_origin;
    int u_voxel_size;
    int u_voxel_dim;
    int u_voxel_range;
} Shader;

bool shader_compile(Shader* s, const char* vert_src, const char* frag_src);

bool shader_compile_asset(Shader* s, const char* name);

unsigned int shader_load_program(const char* name);

bool shader_warmup_all(void);

void shader_use(const Shader* s);

void shader_destroy(Shader* s);

#endif
