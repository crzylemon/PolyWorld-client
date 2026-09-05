/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: mesh_loader.h                                                                       |
|   Purpose: OBJ parse + upload                                                               |
\*-------------------------------------------------------------------------------------------*/

#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float* positions;
    float* normals;
    float* texcoords;
    float* colors;
    uint32_t* indices;
    size_t vertex_count;
    size_t index_count;
} MeshData;

typedef struct GPUMesh {
    unsigned int vao;
    unsigned int vbo_positions;
    unsigned int vbo_normals;
    unsigned int vbo_texcoords;
    unsigned int vbo_colors;
    unsigned int ebo;
    size_t index_count;
    bool has_texcoords;
    bool has_colors;
    float bounding_radius;
    float aabb_min[3];
    float aabb_max[3];
    uint8_t prim_kind;

    float* cpu_positions;
    uint32_t* cpu_indices;
    size_t cpu_vertex_count;
} GPUMesh;

bool mesh_parse_obj(const char* obj_text, size_t len, MeshData* out);

void mesh_data_free(MeshData* data);

char* mesh_serialize_obj(const MeshData* data, size_t* out_len);

#endif
