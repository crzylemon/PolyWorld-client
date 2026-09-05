/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: mesh_loader.c                                                                       |
|   Purpose: OBJ parse + upload                                                               |
\*-------------------------------------------------------------------------------------------*/

#include "mesh_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "log.h"

typedef struct {
    float* data;
    size_t count;
    size_t capacity;
} FloatArray;

typedef struct {
    uint32_t* data;
    size_t count;
    size_t capacity;
} UintArray;

typedef struct {
    int pos_idx;
    int norm_idx;
    int tex_idx;
} VertexKey;

typedef struct {
    VertexKey* keys;
    size_t count;
    size_t capacity;
} VertexKeyArray;

static void float_array_init(FloatArray* a) {
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

static bool float_array_push(FloatArray* a, float val) {
    if (a->count >= a->capacity) {
        size_t new_cap = a->capacity == 0 ? 64 : a->capacity * 2;
        float* new_data = realloc(a->data, new_cap * sizeof(float));
        if (!new_data) return false;
        a->data = new_data;
        a->capacity = new_cap;
    }
    a->data[a->count++] = val;
    return true;
}

static void float_array_free(FloatArray* a) {
    free(a->data);
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

static void uint_array_init(UintArray* a) {
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

static bool uint_array_push(UintArray* a, uint32_t val) {
    if (a->count >= a->capacity) {
        size_t new_cap = a->capacity == 0 ? 64 : a->capacity * 2;
        uint32_t* new_data = realloc(a->data, new_cap * sizeof(uint32_t));
        if (!new_data) return false;
        a->data = new_data;
        a->capacity = new_cap;
    }
    a->data[a->count++] = val;
    return true;
}

static void uint_array_free(UintArray* a) {
    free(a->data);
    a->data = NULL;
    a->count = 0;
    a->capacity = 0;
}

static void vertex_key_array_init(VertexKeyArray* a) {
    a->keys = NULL;
    a->count = 0;
    a->capacity = 0;
}

static bool vertex_key_array_push(VertexKeyArray* a, VertexKey key) {
    if (a->count >= a->capacity) {
        size_t new_cap = a->capacity == 0 ? 64 : a->capacity * 2;
        VertexKey* new_keys = realloc(a->keys, new_cap * sizeof(VertexKey));
        if (!new_keys) return false;
        a->keys = new_keys;
        a->capacity = new_cap;
    }
    a->keys[a->count++] = key;
    return true;
}

static void vertex_key_array_free(VertexKeyArray* a) {
    free(a->keys);
    a->keys = NULL;
    a->count = 0;
    a->capacity = 0;
}

static uint32_t find_or_add_vertex(VertexKeyArray* unique_verts,
                                   FloatArray* out_positions,
                                   FloatArray* out_normals,
                                   FloatArray* out_texcoords,
                                   const FloatArray* raw_positions,
                                   const FloatArray* raw_normals,
                                   const FloatArray* raw_texcoords,
                                   int pos_idx, int norm_idx, int tex_idx) {

    for (size_t i = 0; i < unique_verts->count; i++) {
        if (unique_verts->keys[i].pos_idx == pos_idx &&
            unique_verts->keys[i].norm_idx == norm_idx &&
            unique_verts->keys[i].tex_idx == tex_idx) {
            return (uint32_t)i;
        }
    }

    VertexKey key = { pos_idx, norm_idx, tex_idx };
    vertex_key_array_push(unique_verts, key);

    size_t p_base = (size_t)pos_idx * 3;
    float_array_push(out_positions, raw_positions->data[p_base]);
    float_array_push(out_positions, raw_positions->data[p_base + 1]);
    float_array_push(out_positions, raw_positions->data[p_base + 2]);

    size_t n_base = (size_t)norm_idx * 3;
    float_array_push(out_normals, raw_normals->data[n_base]);
    float_array_push(out_normals, raw_normals->data[n_base + 1]);
    float_array_push(out_normals, raw_normals->data[n_base + 2]);

    if (tex_idx >= 0 && raw_texcoords->count > 0) {
        size_t t_base = (size_t)tex_idx * 2;
        float_array_push(out_texcoords, raw_texcoords->data[t_base]);
        float_array_push(out_texcoords, raw_texcoords->data[t_base + 1]);
    } else {
        float_array_push(out_texcoords, 0.0f);
        float_array_push(out_texcoords, 0.0f);
    }

    return (uint32_t)(unique_verts->count - 1);
}

static const char* skip_spaces(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static bool parse_float(const char** pp, float* out) {
    const char* p = skip_spaces(*pp);
    char* end = NULL;
    double val = strtod(p, &end);
    if (end == p) return false;
    *out = (float)val;
    *pp = end;
    return true;
}

static bool parse_face_vertex(const char** pp, int* pos_idx, int* norm_idx, int* tex_idx,
                              int num_positions, int num_normals, int num_texcoords) {
    const char* p = skip_spaces(*pp);
    char* end = NULL;

    long pi = strtol(p, &end, 10);
    if (end == p) return false;
    p = end;

    if (pi > 0) *pos_idx = (int)pi - 1;
    else if (pi < 0) *pos_idx = num_positions + (int)pi;
    else return false;

    *norm_idx = -1;
    *tex_idx = -1;

    if (*p == '/') {
        p++;

        if (*p != '/') {
            long ti = strtol(p, &end, 10);
            if (end != p) {
                if (ti > 0) *tex_idx = (int)ti - 1;
                else if (ti < 0) *tex_idx = num_texcoords + (int)ti;
            }
            p = end;
        }
        if (*p == '/') {
            p++;
            long ni = strtol(p, &end, 10);
            if (end == p) return false;
            p = end;
            if (ni > 0) *norm_idx = (int)ni - 1;
            else if (ni < 0) *norm_idx = num_normals + (int)ni;
            else return false;
        }
    }

    *pp = p;
    return true;
}

bool mesh_parse_obj(const char* obj_text, size_t len, MeshData* out) {
    if (!obj_text || len == 0 || !out) {
        PW_ERR(ERR_GENERIC, "Broken obj\n");
        return false;
    }
    memset(out, 0, sizeof(*out));

    FloatArray raw_positions, raw_normals, raw_texcoords;
    FloatArray out_positions, out_normals, out_texcoords;
    UintArray out_indices;
    VertexKeyArray unique_verts;

    float_array_init(&raw_positions);
    float_array_init(&raw_normals);
    float_array_init(&raw_texcoords);
    float_array_init(&out_positions);
    float_array_init(&out_normals);
    float_array_init(&out_texcoords);
    uint_array_init(&out_indices);
    vertex_key_array_init(&unique_verts);

    const char* p = obj_text;
    const char* end = obj_text + len;
    int line_num = 0;
    bool has_faces = false;

    while (p < end) {
        line_num++;
        const char* line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;

        const char* lp = skip_spaces(p);

        if (lp >= line_end || *lp == '#') {
            p = line_end;
            if (p < end && *p == '\r') p++;
            if (p < end && *p == '\n') p++;
            continue;
        }

        if (*lp == 'v' && *(lp + 1) == ' ') {

            lp += 2;
            float x, y, z;
            if (!parse_float(&lp, &x) || !parse_float(&lp, &y) || !parse_float(&lp, &z)) {
                PW_ERR(ERR_GENERIC, "Malformed vertex\n");
                goto fail;
            }
            float_array_push(&raw_positions, x);
            float_array_push(&raw_positions, y);
            float_array_push(&raw_positions, z);
        } else if (*lp == 'v' && *(lp + 1) == 'n' && *(lp + 2) == ' ') {

            lp += 3;
            float x, y, z;
            if (!parse_float(&lp, &x) || !parse_float(&lp, &y) || !parse_float(&lp, &z)) {
                PW_ERR(ERR_GENERIC, "Malformed normal\n");
                goto fail;
            }
            float_array_push(&raw_normals, x);
            float_array_push(&raw_normals, y);
            float_array_push(&raw_normals, z);
        } else if (*lp == 'v' && *(lp + 1) == 't' && *(lp + 2) == ' ') {

            lp += 3;
            float u, v;
            if (!parse_float(&lp, &u) || !parse_float(&lp, &v)) {
                PW_ERR(ERR_GENERIC, "Malformed uvs\n");
                goto fail;
            }
            float_array_push(&raw_texcoords, u);
            float_array_push(&raw_texcoords, v);
        } else if (*lp == 'f' && (*(lp + 1) == ' ' || *(lp + 1) == '\t')) {

            lp += 2;
            int pos_indices[64], norm_indices[64], tex_indices[64];
            int num_pos = (int)(raw_positions.count / 3);
            int num_norm = (int)(raw_normals.count / 3);
            int num_tex = (int)(raw_texcoords.count / 2);
            int face_verts = 0;

            while (face_verts < 64) {
                lp = skip_spaces(lp);
                if (lp >= line_end || *lp == '\n' || *lp == '\r' || *lp == '#') break;
                if (!parse_face_vertex(&lp, &pos_indices[face_verts], &norm_indices[face_verts],
                                       &tex_indices[face_verts], num_pos, num_norm, num_tex)) {

                    if (face_verts >= 3) break;
                    PW_ERR(ERR_GENERIC, "Malformed face\n");
                    goto fail;
                }

                if (pos_indices[face_verts] < 0 || pos_indices[face_verts] >= num_pos) {
                    PW_ERR(ERR_GENERIC, "Malformed vertex (oor)\n");
                    goto fail;
                }
                if (norm_indices[face_verts] != -1 &&
                    (norm_indices[face_verts] < 0 || norm_indices[face_verts] >= num_norm)) {
                        PW_ERR(ERR_GENERIC, "Malformed normal (oor)\n");
                    goto fail;
                }
                face_verts++;
            }

            if (face_verts < 3) {
                PW_ERR(ERR_GENERIC, "Face with fewer than 3 verts\n");
                goto fail;
            }

            for (int i = 0; i < face_verts; i++) {
                if (norm_indices[i] == -1) {

                    size_t b0 = (size_t)pos_indices[0] * 3;
                    size_t b1 = (size_t)pos_indices[1] * 3;
                    size_t b2 = (size_t)pos_indices[2] * 3;
                    float ax = raw_positions.data[b1] - raw_positions.data[b0];
                    float ay = raw_positions.data[b1 + 1] - raw_positions.data[b0 + 1];
                    float az = raw_positions.data[b1 + 2] - raw_positions.data[b0 + 2];
                    float bx = raw_positions.data[b2] - raw_positions.data[b0];
                    float by = raw_positions.data[b2 + 1] - raw_positions.data[b0 + 1];
                    float bz = raw_positions.data[b2 + 2] - raw_positions.data[b0 + 2];
                    float nx = ay * bz - az * by;
                    float ny = az * bx - ax * bz;
                    float nz = ax * by - ay * bx;
                    float len_n = sqrtf(nx * nx + ny * ny + nz * nz);
                    if (len_n > 1e-8f) { nx /= len_n; ny /= len_n; nz /= len_n; }

                    int gen_norm_idx = (int)(raw_normals.count / 3);
                    float_array_push(&raw_normals, nx);
                    float_array_push(&raw_normals, ny);
                    float_array_push(&raw_normals, nz);
                    norm_indices[i] = gen_norm_idx;
                }
            }

            for (int i = 1; i < face_verts - 1; i++) {
                uint32_t idx0 = find_or_add_vertex(&unique_verts, &out_positions, &out_normals, &out_texcoords,
                                                   &raw_positions, &raw_normals, &raw_texcoords,
                                                   pos_indices[0], norm_indices[0], tex_indices[0]);
                uint32_t idx1 = find_or_add_vertex(&unique_verts, &out_positions, &out_normals, &out_texcoords,
                                                   &raw_positions, &raw_normals, &raw_texcoords,
                                                   pos_indices[i], norm_indices[i], tex_indices[i]);
                uint32_t idx2 = find_or_add_vertex(&unique_verts, &out_positions, &out_normals, &out_texcoords,
                                                   &raw_positions, &raw_normals, &raw_texcoords,
                                                   pos_indices[i + 1], norm_indices[i + 1], tex_indices[i + 1]);
                uint_array_push(&out_indices, idx0);
                uint_array_push(&out_indices, idx1);
                uint_array_push(&out_indices, idx2);
            }
            has_faces = true;
        }

        p = line_end;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    if (!has_faces || out_indices.count == 0) {
        PW_ERR(ERR_GENERIC, "obj empty?!?!\n");
        goto fail;
    }

    out->positions = out_positions.data;
    out->normals = out_normals.data;
    out->texcoords = (out_texcoords.count > 0) ? out_texcoords.data : NULL;
    out->colors = NULL;
    out->indices = out_indices.data;
    out->vertex_count = out_positions.count / 3;
    out->index_count = out_indices.count;

    float_array_free(&raw_positions);
    float_array_free(&raw_normals);
    float_array_free(&raw_texcoords);
    vertex_key_array_free(&unique_verts);
    return true;

fail:
    float_array_free(&raw_positions);
    float_array_free(&raw_normals);
    float_array_free(&raw_texcoords);
    float_array_free(&out_positions);
    float_array_free(&out_normals);
    float_array_free(&out_texcoords);
    uint_array_free(&out_indices);
    vertex_key_array_free(&unique_verts);
    return false;
}

void mesh_data_free(MeshData* data) {
    if (!data) return;
    free(data->positions);
    free(data->normals);
    free(data->texcoords);
    free(data->colors);
    free(data->indices);
    data->positions = NULL;
    data->normals = NULL;
    data->texcoords = NULL;
    data->colors = NULL;
    data->indices = NULL;
    data->vertex_count = 0;
    data->index_count = 0;
}

char* mesh_serialize_obj(const MeshData* data, size_t* out_len) {
    if (!data || data->vertex_count == 0 || data->index_count == 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t est_size = data->vertex_count * 80 + (data->index_count / 3) * 40 + 256;
    char* buf = malloc(est_size);
    if (!buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }

    size_t offset = 0;

    for (size_t i = 0; i < data->vertex_count; i++) {
        size_t base = i * 3;
        int written = snprintf(buf + offset, est_size - offset, "v %g %g %g\n",
                               (double)data->positions[base],
                               (double)data->positions[base + 1],
                               (double)data->positions[base + 2]);
        if (written < 0) goto serialize_fail;
        offset += (size_t)written;
        if (offset >= est_size) goto grow;
    }

    for (size_t i = 0; i < data->vertex_count; i++) {
        size_t base = i * 3;
        int written = snprintf(buf + offset, est_size - offset, "vn %g %g %g\n",
                               (double)data->normals[base],
                               (double)data->normals[base + 1],
                               (double)data->normals[base + 2]);
        if (written < 0) goto serialize_fail;
        offset += (size_t)written;
        if (offset >= est_size) goto grow;
    }

    for (size_t i = 0; i + 2 < data->index_count; i += 3) {
        uint32_t i0 = data->indices[i] + 1;
        uint32_t i1 = data->indices[i + 1] + 1;
        uint32_t i2 = data->indices[i + 2] + 1;
        int written = snprintf(buf + offset, est_size - offset,
                               "f %u//%u %u//%u %u//%u\n",
                               i0, i0, i1, i1, i2, i2);
        if (written < 0) goto serialize_fail;
        offset += (size_t)written;
        if (offset >= est_size) goto grow;
    }

    if (out_len) *out_len = offset;
    return buf;

grow:

    free(buf);
    est_size *= 2;
    buf = malloc(est_size);
    if (!buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    offset = 0;

    for (size_t i = 0; i < data->vertex_count; i++) {
        size_t base = i * 3;
        offset += (size_t)snprintf(buf + offset, est_size - offset, "v %g %g %g\n",
                                   (double)data->positions[base],
                                   (double)data->positions[base + 1],
                                   (double)data->positions[base + 2]);
    }
    for (size_t i = 0; i < data->vertex_count; i++) {
        size_t base = i * 3;
        offset += (size_t)snprintf(buf + offset, est_size - offset, "vn %g %g %g\n",
                                   (double)data->normals[base],
                                   (double)data->normals[base + 1],
                                   (double)data->normals[base + 2]);
    }
    for (size_t i = 0; i + 2 < data->index_count; i += 3) {
        uint32_t i0 = data->indices[i] + 1;
        uint32_t i1 = data->indices[i + 1] + 1;
        uint32_t i2 = data->indices[i + 2] + 1;
        offset += (size_t)snprintf(buf + offset, est_size - offset,
                                   "f %u//%u %u//%u %u//%u\n",
                                   i0, i0, i1, i1, i2, i2);
    }

    if (out_len) *out_len = offset;
    return buf;

serialize_fail:
    free(buf);
    if (out_len) *out_len = 0;
    return NULL;
}
