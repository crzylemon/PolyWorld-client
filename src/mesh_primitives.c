/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: mesh_primitives.c                                                                   |
|   Purpose: boxes, spheres, cylinders, wedges, quads                                         |
\*-------------------------------------------------------------------------------------------*/

#include "mesh_primitives.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool create_box_mesh(MeshData* out, float hx, float hy, float hz) {
    memset(out, 0, sizeof(MeshData));
    out->vertex_count = 24;
    out->index_count = 36;
    out->positions = (float*)malloc(24 * 3 * sizeof(float));
    out->normals = (float*)malloc(24 * 3 * sizeof(float));
    out->texcoords = (float*)malloc(24 * 2 * sizeof(float));
    out->indices = (uint32_t*)malloc(36 * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->texcoords || !out->indices) { mesh_data_free(out); return false; }

    float face_u[] = { hx*2, hx*2, hx*2, hx*2, hz*2, hz*2 };
    float face_v[] = { hy*2, hy*2, hz*2, hz*2, hy*2, hy*2 };
    float verts[] = {
        -hx,-hy, hz,  hx,-hy, hz,  hx, hy, hz, -hx, hy, hz,
         hx,-hy,-hz, -hx,-hy,-hz, -hx, hy,-hz,  hx, hy,-hz,
        -hx, hy, hz,  hx, hy, hz,  hx, hy,-hz, -hx, hy,-hz,
        -hx,-hy,-hz,  hx,-hy,-hz,  hx,-hy, hz, -hx,-hy, hz,
         hx,-hy, hz,  hx,-hy,-hz,  hx, hy,-hz,  hx, hy, hz,
        -hx,-hy,-hz, -hx,-hy, hz, -hx, hy, hz, -hx, hy,-hz,
    };
    float norms[] = {
        0,0,1, 0,0,1, 0,0,1, 0,0,1,
        0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1,
        0,1,0, 0,1,0, 0,1,0, 0,1,0,
        0,-1,0, 0,-1,0, 0,-1,0, 0,-1,0,
        1,0,0, 1,0,0, 1,0,0, 1,0,0,
        -1,0,0, -1,0,0, -1,0,0, -1,0,0,
    };
    float uvs[48];
    for (int f = 0; f < 6; f++) {
        int i = f*8;
        uvs[i]=0; uvs[i+1]=0; uvs[i+2]=face_u[f]; uvs[i+3]=0;
        uvs[i+4]=face_u[f]; uvs[i+5]=face_v[f]; uvs[i+6]=0; uvs[i+7]=face_v[f];
    }
    uint32_t idxs[] = { 0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11, 12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23 };
    memcpy(out->positions, verts, sizeof(verts));
    memcpy(out->normals, norms, sizeof(norms));
    memcpy(out->texcoords, uvs, sizeof(uvs));
    memcpy(out->indices, idxs, sizeof(idxs));
    return true;
}

int mesh_curve_lod_sphere_segments(int lod) {
    static const int segs[MESH_CURVE_LOD_COUNT] = { 8, 16, 24, 32, 48, 64, 96 };
    if (lod < 0) lod = 0;
    if (lod >= MESH_CURVE_LOD_COUNT) lod = MESH_CURVE_LOD_COUNT - 1;
    return segs[lod];
}

int mesh_curve_lod_cylinder_segments(int lod) {
    return mesh_curve_lod_sphere_segments(lod);
}

int mesh_curve_lod_index(float radius_studs, int quality) {
    static const float err[6] = { 0.28f, 0.12f, 0.055f, 0.038f, 0.026f, 0.018f };
    static const int cap[6] = { 0, 2, 3, 4, 5, 6 };
    if (quality < 0) quality = 0;
    if (quality > 5) quality = 5;
    if (radius_studs < 0.25f) radius_studs = 0.25f;
    float e = err[quality];
    float ratio = e / radius_studs;
    if (ratio > 0.5f) ratio = 0.5f;
    if (ratio < 1e-4f) ratio = 1e-4f;
    float n = (float)M_PI / acosf(1.0f - ratio);
    int lod = 0;
    while (lod < MESH_CURVE_LOD_COUNT - 1 &&
           (float)mesh_curve_lod_sphere_segments(lod) + 0.5f < n)
        lod++;
    if (lod > cap[quality]) lod = cap[quality];
    return lod;
}

bool create_sphere_mesh(MeshData* out, float radius, int segments, int rings) {
    if (segments < 8) segments = 8;
    if (radius < 0.01f) radius = 0.01f;
    int div = segments / 4;
    if (rings > 0) {
        int d2 = rings / 2;
        if (d2 > div) div = d2;
    }
    if (div < 3) div = 3;
    if (div > 32) div = 32;

    int n = div + 1;
    int vert_count = 6 * n * n;
    int idx_count = 6 * div * div * 6;

    memset(out, 0, sizeof(MeshData));
    out->vertex_count = vert_count;
    out->index_count = idx_count;
    out->positions = (float*)malloc((size_t)vert_count * 3 * sizeof(float));
    out->normals = (float*)malloc((size_t)vert_count * 3 * sizeof(float));
    out->texcoords = (float*)malloc((size_t)vert_count * 2 * sizeof(float));
    out->indices = (uint32_t*)malloc((size_t)idx_count * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->texcoords || !out->indices) {
        mesh_data_free(out);
        return false;
    }

    int vi = 0, ii = 0;
    for (int face = 0; face < 6; face++) {
        int base = vi;
        for (int j = 0; j < n; j++) {
            float v = -1.0f + 2.0f * (float)j / (float)div;
            for (int i = 0; i < n; i++) {
                float u = -1.0f + 2.0f * (float)i / (float)div;
                float x, y, z;
                switch (face) {
                case 0: x =  u; y =  v; z =  1.0f; break;
                case 1: x = -u; y =  v; z = -1.0f; break;
                case 2: x =  u; y =  1.0f; z = -v; break;
                case 3: x =  u; y = -1.0f; z =  v; break;
                case 4: x =  1.0f; y =  v; z = -u; break;
                default: x = -1.0f; y =  v; z =  u; break;
                }
                float len = sqrtf(x * x + y * y + z * z);
                if (len < 1e-8f) len = 1e-8f;
                x /= len; y /= len; z /= len;
                out->positions[vi * 3 + 0] = x * radius;
                out->positions[vi * 3 + 1] = y * radius;
                out->positions[vi * 3 + 2] = z * radius;
                out->normals[vi * 3 + 0] = x;
                out->normals[vi * 3 + 1] = y;
                out->normals[vi * 3 + 2] = z;

                out->texcoords[vi * 2 + 0] = (u * 0.5f + 0.5f);
                out->texcoords[vi * 2 + 1] = (v * 0.5f + 0.5f);
                vi++;
            }
        }
        for (int j = 0; j < div; j++) {
            for (int i = 0; i < div; i++) {
                uint32_t a = (uint32_t)(base + j * n + i);
                uint32_t b = a + 1;
                uint32_t c = a + (uint32_t)n;
                uint32_t d = c + 1;
                out->indices[ii++] = a; out->indices[ii++] = b; out->indices[ii++] = d;
                out->indices[ii++] = a; out->indices[ii++] = d; out->indices[ii++] = c;
            }
        }
    }
    out->vertex_count = vi;
    out->index_count = ii;
    return true;
}

bool create_cylinder_mesh(MeshData* out, float radius, float height, int segments) {
    if (segments < 6) segments = 6;
    if (radius < 0.01f) radius = 0.01f;
    if (height < 0.01f) height = 0.01f;

    int vert_count = (segments + 1) * 2 + (segments + 1) * 2 + 2;
    int idx_count = segments * 6 + segments * 3 * 2;

    memset(out, 0, sizeof(MeshData));
    out->vertex_count = vert_count;
    out->index_count = idx_count;
    out->positions = (float*)malloc(vert_count * 3 * sizeof(float));
    out->normals = (float*)malloc(vert_count * 3 * sizeof(float));
    out->texcoords = (float*)malloc(vert_count * 2 * sizeof(float));
    out->indices = (uint32_t*)malloc(idx_count * sizeof(uint32_t));

    if (!out->positions || !out->normals || !out->texcoords || !out->indices) {
        mesh_data_free(out);
        return false;
    }

    float half_h = height * 0.5f;
    int vi = 0, ii = 0;

    int side_start = vi;
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        float nx = cosf(theta), nz = sinf(theta);
        float u = theta * radius;

        out->positions[vi*3+0] = x; out->positions[vi*3+1] = -half_h; out->positions[vi*3+2] = z;
        out->normals[vi*3+0] = nx; out->normals[vi*3+1] = 0; out->normals[vi*3+2] = nz;
        out->texcoords[vi*2+0] = u; out->texcoords[vi*2+1] = 0;
        vi++;
        out->positions[vi*3+0] = x; out->positions[vi*3+1] = half_h; out->positions[vi*3+2] = z;
        out->normals[vi*3+0] = nx; out->normals[vi*3+1] = 0; out->normals[vi*3+2] = nz;
        out->texcoords[vi*2+0] = u; out->texcoords[vi*2+1] = height;
        vi++;
    }

    for (int i = 0; i < segments; i++) {
        int b0 = side_start + i * 2;
        int t0 = b0 + 1;
        int b1 = b0 + 2;
        int t1 = b0 + 3;
        out->indices[ii++] = b0; out->indices[ii++] = t0; out->indices[ii++] = b1;
        out->indices[ii++] = b1; out->indices[ii++] = t0; out->indices[ii++] = t1;
    }

    int top_center = vi;
    out->positions[vi*3+0] = 0; out->positions[vi*3+1] = half_h; out->positions[vi*3+2] = 0;
    out->normals[vi*3+0] = 0; out->normals[vi*3+1] = 1; out->normals[vi*3+2] = 0;
    out->texcoords[vi*2+0] = 0; out->texcoords[vi*2+1] = 0;
    vi++;
    int top_ring_start = vi;
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        out->positions[vi*3+0] = x; out->positions[vi*3+1] = half_h; out->positions[vi*3+2] = z;
        out->normals[vi*3+0] = 0; out->normals[vi*3+1] = 1; out->normals[vi*3+2] = 0;
        out->texcoords[vi*2+0] = x; out->texcoords[vi*2+1] = -z;
        vi++;
    }
    for (int i = 0; i < segments; i++) {
        out->indices[ii++] = top_center;
        out->indices[ii++] = top_ring_start + i + 1;
        out->indices[ii++] = top_ring_start + i;
    }

    int bot_center = vi;
    out->positions[vi*3+0] = 0; out->positions[vi*3+1] = -half_h; out->positions[vi*3+2] = 0;
    out->normals[vi*3+0] = 0; out->normals[vi*3+1] = -1; out->normals[vi*3+2] = 0;
    out->texcoords[vi*2+0] = 0; out->texcoords[vi*2+1] = 0;
    vi++;
    int bot_ring_start = vi;
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = cosf(theta) * radius;
        float z = sinf(theta) * radius;
        out->positions[vi*3+0] = x; out->positions[vi*3+1] = -half_h; out->positions[vi*3+2] = z;
        out->normals[vi*3+0] = 0; out->normals[vi*3+1] = -1; out->normals[vi*3+2] = 0;
        out->texcoords[vi*2+0] = x; out->texcoords[vi*2+1] = z;
        vi++;
    }
    for (int i = 0; i < segments; i++) {
        out->indices[ii++] = bot_center;
        out->indices[ii++] = bot_ring_start + i;
        out->indices[ii++] = bot_ring_start + i + 1;
    }

    out->vertex_count = vi;
    out->index_count = ii;

    return true;
}

bool create_wedge_mesh(MeshData* out, float hx, float hy, float hz) {
    if (hx < 0.01f) hx = 0.01f;
    if (hy < 0.01f) hy = 0.01f;
    if (hz < 0.01f) hz = 0.01f;

    const int vert_count = 4 + 4 + 4 + 3 + 3;
    const int idx_count = 6 + 6 + 6 + 3 + 3;

    memset(out, 0, sizeof(MeshData));
    out->vertex_count = vert_count;
    out->index_count = idx_count;
    out->positions = (float*)malloc((size_t)vert_count * 3 * sizeof(float));
    out->normals = (float*)malloc((size_t)vert_count * 3 * sizeof(float));
    out->texcoords = (float*)malloc((size_t)vert_count * 2 * sizeof(float));
    out->indices = (uint32_t*)malloc((size_t)idx_count * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->texcoords || !out->indices) {
        mesh_data_free(out);
        return false;
    }

    int vi = 0, ii = 0;
    const float sx = hx * 2.0f;
    const float sy = hy * 2.0f;
    const float sz = hz * 2.0f;
    const float slope_len = sqrtf(sy * sy + sz * sz);

#define WEDGE_V(px, py, pz, nx, ny, nz, u, v) do { \
        out->positions[vi*3+0] = (px); out->positions[vi*3+1] = (py); out->positions[vi*3+2] = (pz); \
        out->normals[vi*3+0] = (nx); out->normals[vi*3+1] = (ny); out->normals[vi*3+2] = (nz); \
        out->texcoords[vi*2+0] = (u); out->texcoords[vi*2+1] = (v); \
        vi++; \
    } while (0)

    int base = vi;
    WEDGE_V(-hx, -hy, -hz, 0, -1, 0, 0, 0);
    WEDGE_V( hx, -hy, -hz, 0, -1, 0, sx, 0);
    WEDGE_V( hx, -hy,  hz, 0, -1, 0, sx, sz);
    WEDGE_V(-hx, -hy,  hz, 0, -1, 0, 0, sz);
    out->indices[ii++] = base; out->indices[ii++] = base+1; out->indices[ii++] = base+2;
    out->indices[ii++] = base; out->indices[ii++] = base+2; out->indices[ii++] = base+3;

    base = vi;
    WEDGE_V(-hx, -hy, hz, 0, 0, 1, 0, 0);
    WEDGE_V( hx, -hy, hz, 0, 0, 1, sx, 0);
    WEDGE_V( hx,  hy, hz, 0, 0, 1, sx, sy);
    WEDGE_V(-hx,  hy, hz, 0, 0, 1, 0, sy);
    out->indices[ii++] = base; out->indices[ii++] = base+1; out->indices[ii++] = base+2;
    out->indices[ii++] = base; out->indices[ii++] = base+2; out->indices[ii++] = base+3;

    {
        float f1x = 0.0f, f1y = 2.0f * hy, f1z = 2.0f * hz;
        float f2x = 2.0f * hx, f2y = 0.0f, f2z = 0.0f;
        float nx = f1y * f2z - f1z * f2y;
        float ny = f1z * f2x - f1x * f2z;
        float nz = f1x * f2y - f1y * f2x;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
        if (ny < 0.0f) { nx = -nx; ny = -ny; nz = -nz; }
        base = vi;
        WEDGE_V(-hx, -hy, -hz, nx, ny, nz, 0, slope_len);
        WEDGE_V(-hx,  hy,  hz, nx, ny, nz, 0, 0);
        WEDGE_V( hx,  hy,  hz, nx, ny, nz, sx, 0);
        WEDGE_V( hx, -hy, -hz, nx, ny, nz, sx, slope_len);
        out->indices[ii++] = base; out->indices[ii++] = base+1; out->indices[ii++] = base+2;
        out->indices[ii++] = base; out->indices[ii++] = base+2; out->indices[ii++] = base+3;
    }

    base = vi;
    WEDGE_V(-hx, -hy, -hz, -1, 0, 0, 0, 0);
    WEDGE_V(-hx, -hy,  hz, -1, 0, 0, sz, 0);
    WEDGE_V(-hx,  hy,  hz, -1, 0, 0, sz, sy);
    out->indices[ii++] = base; out->indices[ii++] = base+1; out->indices[ii++] = base+2;

    base = vi;
    WEDGE_V( hx, -hy, -hz, 1, 0, 0, 0, 0);
    WEDGE_V( hx,  hy,  hz, 1, 0, 0, sz, sy);
    WEDGE_V( hx, -hy,  hz, 1, 0, 0, sz, 0);
    out->indices[ii++] = base; out->indices[ii++] = base+1; out->indices[ii++] = base+2;

#undef WEDGE_V

    out->vertex_count = vi;
    out->index_count = ii;
    return true;
}

bool create_quad_mesh(MeshData* out) {
    memset(out, 0, sizeof(MeshData));
    out->vertex_count = 4;
    out->index_count = 6;
    out->positions = (float*)malloc(4 * 3 * sizeof(float));
    out->normals = (float*)malloc(4 * 3 * sizeof(float));
    out->texcoords = (float*)malloc(4 * 2 * sizeof(float));
    out->indices = (uint32_t*)malloc(6 * sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->texcoords || !out->indices) {
        mesh_data_free(out);
        return false;
    }
    float verts[] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f,
    };
    float norms[] = { 0,0,1, 0,0,1, 0,0,1, 0,0,1 };

    float uvs[] = { 0,1,  1,1,  1,0,  0,0 };
    uint32_t idxs[] = { 0, 1, 2, 0, 2, 3 };
    memcpy(out->positions, verts, sizeof(verts));
    memcpy(out->normals, norms, sizeof(norms));
    memcpy(out->texcoords, uvs, sizeof(uvs));
    memcpy(out->indices, idxs, sizeof(idxs));
    return true;
}
