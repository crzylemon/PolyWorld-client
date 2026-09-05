/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: accessory.c                                                                         |
|   Purpose: Accessories (Also used for Tools)                                                |
\*-------------------------------------------------------------------------------------------*/

#include "accessory.h"
#include "log.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const struct {
    const char* name;
    int part_index;
} ATTACH_MAP[] = {
    { "Hat",      ANIM_PART_HEAD },
    { "Torso",    ANIM_PART_TORSO },
    { "RightArm", ANIM_PART_RIGHT_ARM },
    { "Handle",   ANIM_PART_RIGHT_ARM },
    { "LeftArm",  ANIM_PART_LEFT_ARM },
    { "RightLeg", ANIM_PART_RIGHT_LEG },
    { "LeftLeg",  ANIM_PART_LEFT_LEG },
};
#define ATTACH_MAP_COUNT (int)(sizeof(ATTACH_MAP) / sizeof(ATTACH_MAP[0]))

typedef struct { float* d; int n, cap; } FArr;
typedef struct { uint32_t* d; int n, cap; } UArr;

static void farr_push(FArr* a, float v) {
    if (a->n >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 256;
        a->d = realloc(a->d, a->cap * sizeof(float));
    }
    a->d[a->n++] = v;
}

static void uarr_push(UArr* a, uint32_t v) {
    if (a->n >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 256;
        a->d = realloc(a->d, a->cap * sizeof(uint32_t));
    }
    a->d[a->n++] = v;
}

typedef struct { int p, n, t; } VKey;
typedef struct { VKey* d; int n, cap; } VKArr;

static int vkarr_find_or_add(VKArr* a, VKey key, bool* was_new) {
    for (int i = 0; i < a->n; i++) {
        if (a->d[i].p == key.p && a->d[i].n == key.n && a->d[i].t == key.t) {
            *was_new = false;
            return i;
        }
    }
    if (a->n >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 256;
        a->d = realloc(a->d, a->cap * sizeof(VKey));
    }
    a->d[a->n] = key;
    *was_new = true;
    return a->n++;
}

static int name_len_strip_blender(const char* s, int n) {
    int i = n;
    while (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9') i--;
    if (i > 0 && i < n && s[i - 1] == '.') return i - 1;
    return n;
}

static int match_attachment(const char* name_start, int name_len) {
    int n = name_len_strip_blender(name_start, name_len);
    for (int i = 0; i < ATTACH_MAP_COUNT; i++) {
        int ml = (int)strlen(ATTACH_MAP[i].name);
        if (n == ml && strncmp(name_start, ATTACH_MAP[i].name, ml) == 0) {
            return ATTACH_MAP[i].part_index;
        }
    }
    return -1;
}

static int name_is_glow(const char* s, int n) {
    n = name_len_strip_blender(s, n);
    if (n != 4) return 0;
    char g[4] = { 'g', 'l', 'o', 'w' };
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != g[i]) return 0;
    }
    return 1;
}

static float atlas_wrap01(float t) {
    t = t - floorf(t);
    if (t < 0.0f) t += 1.0f;
    return t;
}

static Vec3 atlas_fetch(const unsigned char* p, int w, int h, float u, float v) {
    u = atlas_wrap01(u);
    v = atlas_wrap01(v);
    int x = (int)(u * (float)(w - 1) + 0.5f);
    int y = (int)(v * (float)(h - 1) + 0.5f);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    int i = (y * w + x) * 4;
    return (Vec3){
        (float)p[i + 0] / 255.0f,
        (float)p[i + 1] / 255.0f,
        (float)p[i + 2] / 255.0f
    };
}

static Vec3 atlas_sample_uv(const unsigned char* p, int w, int h, float u, float v) {
    Vec3 acc = { 0.0f, 0.0f, 0.0f };
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            float su = u + (float)dx / (float)w;
            float sv = v + (float)dy / (float)h;
            Vec3 c = atlas_fetch(p, w, h, su, sv);
            acc.x += c.x; acc.y += c.y; acc.z += c.z;
            n++;
        }
    }
    if (n < 1) return (Vec3){ 1.0f, 1.0f, 1.0f };
    float inv = 1.0f / (float)n;
    return (Vec3){ acc.x * inv, acc.y * inv, acc.z * inv };
}

static void accessory_sample_glows(Accessory* acc) {
    if (!acc || !acc->atlas_rgba || acc->atlas_w < 1 || acc->atlas_h < 1) return;
    for (int i = 0; i < acc->glow_count; i++) {
        AccessoryGlow* g = &acc->glows[i];
        if (!g->valid) continue;
        if (!g->has_uv) {
            g->color = (Vec3){ 1.0f, 1.0f, 1.0f };
            continue;
        }
        g->color = atlas_sample_uv(acc->atlas_rgba, acc->atlas_w, acc->atlas_h, g->u, g->v);
    }
}

static void accessory_free_atlas(Accessory* acc) {
    if (!acc) return;
    free(acc->atlas_rgba);
    acc->atlas_rgba = NULL;
    acc->atlas_w = 0;
    acc->atlas_h = 0;
}

void accessory_set_atlas(Accessory* acc, const unsigned char* rgba, int w, int h) {
    if (!acc || !rgba || w < 1 || h < 1) return;
    size_t bytes = (size_t)w * (size_t)h * 4u;
    unsigned char* copy = (unsigned char*)malloc(bytes);
    if (!copy) return;
    memcpy(copy, rgba, bytes);
    free(acc->atlas_rgba);
    acc->atlas_rgba = copy;
    acc->atlas_w = w;
    acc->atlas_h = h;
    accessory_sample_glows(acc);
}

static void glow_commit(Accessory* acc, int attach, const FArr* pos,
                        float u, float v, int has_uv) {
    if (!acc || acc->glow_count >= ACCESSORY_MAX_GLOWS) return;
    if (!pos || pos->n < 3) return;
    int nv = pos->n / 3;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    for (int i = 0; i < nv; i++) {
        cx += pos->d[i * 3 + 0];
        cy += pos->d[i * 3 + 1];
        cz += pos->d[i * 3 + 2];
    }
    float inv = 1.0f / (float)nv;
    cx *= inv; cy *= inv; cz *= inv;
    float r2 = 0.0f;
    for (int i = 0; i < nv; i++) {
        float dx = pos->d[i * 3 + 0] - cx;
        float dy = pos->d[i * 3 + 1] - cy;
        float dz = pos->d[i * 3 + 2] - cz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2) r2 = d2;
    }
    AccessoryGlow* g = &acc->glows[acc->glow_count++];
    g->attach_part = (attach >= 0) ? attach : ANIM_PART_HEAD;
    g->local_pos = (Vec3){ cx, cy, cz };
    g->radius = sqrtf(r2);
    if (g->radius < 0.08f) g->radius = 0.08f;
    g->u = u;
    g->v = v;
    g->has_uv = has_uv != 0;
    g->color = (Vec3){ 1.0f, 1.0f, 1.0f };
    g->valid = true;
}

bool accessory_load(Accessory* acc, const char* obj_data, size_t len) {
    unsigned char* keep_atlas = acc->atlas_rgba;
    int keep_w = acc->atlas_w, keep_h = acc->atlas_h;
    acc->atlas_rgba = NULL;
    memset(acc, 0, sizeof(Accessory));
    acc->atlas_rgba = keep_atlas;
    acc->atlas_w = keep_w;
    acc->atlas_h = keep_h;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        acc->parts[i].attach_part = -1;
    }

    FArr raw_pos = {0}, raw_norm = {0}, raw_tc = {0};

    FArr part_pos[ACCESSORY_MAX_PARTS] = {{0}};
    FArr part_norm[ACCESSORY_MAX_PARTS] = {{0}};
    FArr part_tc[ACCESSORY_MAX_PARTS] = {{0}};
    UArr part_idx[ACCESSORY_MAX_PARTS] = {{0}};
    VKArr part_vkeys[ACCESSORY_MAX_PARTS] = {{0}};
    int part_attach[ACCESSORY_MAX_PARTS];
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) part_attach[i] = -1;

    FArr glow_pos[ACCESSORY_MAX_GLOWS] = {{0}};
    int glow_attach[ACCESSORY_MAX_GLOWS];
    float glow_u[ACCESSORY_MAX_GLOWS], glow_v[ACCESSORY_MAX_GLOWS];
    int glow_has_uv[ACCESSORY_MAX_GLOWS];
    int glow_has_face[ACCESSORY_MAX_GLOWS];
    for (int i = 0; i < ACCESSORY_MAX_GLOWS; i++) {
        glow_attach[i] = -1;
        glow_u[i] = 0.0f;
        glow_v[i] = 0.0f;
        glow_has_uv[i] = 0;
        glow_has_face[i] = 0;
    }

    int cur_slot = -1;
    int cur_glow = -1;
    int slot_count = 0;
    int last_attach = -1;
    int first_attach = -1;

    const char* p = obj_data;
    const char* end_ptr = obj_data + len;

    while (p < end_ptr) {
        const char* line_end = p;
        while (line_end < end_ptr && *line_end != '\n' && *line_end != '\r') line_end++;

        while (p < line_end && (*p == ' ' || *p == '\t')) p++;

        if (p < line_end && *p == 'o' && p+1 < line_end && p[1] == ' ') {
            const char* name_start = p + 2;
            int name_len = (int)(line_end - name_start);
            while (name_len > 0 && (name_start[name_len-1] == ' ' || name_start[name_len-1] == '\r')) name_len--;

            cur_slot = -1;
            cur_glow = -1;

            if (name_is_glow(name_start, name_len)) {
                if (acc->glow_count < ACCESSORY_MAX_GLOWS) {
                    cur_glow = acc->glow_count;
                    glow_attach[cur_glow] = last_attach;
                    acc->glow_count++;
                }
            } else {
                int attach = match_attachment(name_start, name_len);
                if (attach >= 0) {
                    last_attach = attach;
                    if (first_attach < 0) first_attach = attach;
                    if (slot_count < ACCESSORY_MAX_PARTS) {
                        cur_slot = slot_count;
                        part_attach[cur_slot] = attach;
                        slot_count++;
                    }
                }
            }
        } else if (p < line_end && *p == 'v' && p+1 < line_end && p[1] == ' ') {
            const char* cp = p + 2;
            float x = strtof(cp, (char**)&cp);
            float y = strtof(cp, (char**)&cp);
            float z = strtof(cp, (char**)&cp);
            farr_push(&raw_pos, x);
            farr_push(&raw_pos, y);
            farr_push(&raw_pos, z);
        } else if (p+1 < line_end && *p == 'v' && p[1] == 'n' && p+2 < line_end && p[2] == ' ') {
            const char* cp = p + 3;
            float x = strtof(cp, (char**)&cp);
            float y = strtof(cp, (char**)&cp);
            float z = strtof(cp, (char**)&cp);
            farr_push(&raw_norm, x);
            farr_push(&raw_norm, y);
            farr_push(&raw_norm, z);
        } else if (p+1 < line_end && *p == 'v' && p[1] == 't' && p+2 < line_end && p[2] == ' ') {
            const char* cp = p + 3;
            float u = strtof(cp, (char**)&cp);
            float v = strtof(cp, (char**)&cp);
            farr_push(&raw_tc, u);
            farr_push(&raw_tc, v);
        } else if (p < line_end && *p == 'f' && p+1 < line_end && (p[1] == ' ' || p[1] == '\t')) {
            if (cur_slot >= 0 || cur_glow >= 0) {
                const char* cp = p + 2;
                int pi[64], ni[64], ti[64];
                int nv = 0;
                int num_p = raw_pos.n / 3;
                int num_n = raw_norm.n / 3;
                int num_t = raw_tc.n / 2;

                while (nv < 64 && cp < line_end) {
                    while (cp < line_end && (*cp == ' ' || *cp == '\t')) cp++;
                    if (cp >= line_end) break;

                    char* ep;
                    long pv = strtol(cp, &ep, 10);
                    if (ep == cp) break;
                    cp = ep;
                    pi[nv] = (pv > 0) ? (int)pv - 1 : num_p + (int)pv;
                    ni[nv] = -1;
                    ti[nv] = -1;

                    if (cp < line_end && *cp == '/') {
                        cp++;
                        if (cp < line_end && *cp != '/') {
                            long tv = strtol(cp, &ep, 10);
                            if (ep != cp) ti[nv] = (tv > 0) ? (int)tv - 1 : num_t + (int)tv;
                            cp = ep;
                        }
                        if (cp < line_end && *cp == '/') {
                            cp++;
                            long nval = strtol(cp, &ep, 10);
                            if (ep != cp) ni[nv] = (nval > 0) ? (int)nval - 1 : num_n + (int)nval;
                            cp = ep;
                        }
                    }
                    nv++;
                }

                if (nv >= 3 && cur_glow >= 0) {
                    if (!glow_has_face[cur_glow]) {
                        glow_has_face[cur_glow] = 1;
                        if (ti[0] >= 0 && ti[1] >= 0 && ti[2] >= 0 && raw_tc.n >= 2) {
                            int t0 = ti[0] * 2, t1 = ti[1] * 2, t2 = ti[2] * 2;
                            if (t0 + 1 < raw_tc.n && t1 + 1 < raw_tc.n && t2 + 1 < raw_tc.n) {
                                glow_u[cur_glow] = (raw_tc.d[t0] + raw_tc.d[t1] + raw_tc.d[t2]) / 3.0f;
                                glow_v[cur_glow] = (raw_tc.d[t0 + 1] + raw_tc.d[t1 + 1] + raw_tc.d[t2 + 1]) / 3.0f;
                                glow_has_uv[cur_glow] = 1;
                            }
                        }
                    }
                    for (int i = 0; i < nv; i++) {
                        if (pi[i] < 0 || pi[i] * 3 + 2 >= raw_pos.n) continue;
                        int pb = pi[i] * 3;
                        farr_push(&glow_pos[cur_glow], raw_pos.d[pb]);
                        farr_push(&glow_pos[cur_glow], raw_pos.d[pb + 1]);
                        farr_push(&glow_pos[cur_glow], raw_pos.d[pb + 2]);
                    }
                } else if (nv >= 3 && cur_slot >= 0) {
                    for (int i = 0; i < nv; i++) {
                        if (ni[i] == -1) {
                            int b0 = pi[0]*3, b1 = pi[1]*3, b2 = pi[2]*3;
                            float ax = raw_pos.d[b1] - raw_pos.d[b0];
                            float ay = raw_pos.d[b1+1] - raw_pos.d[b0+1];
                            float az = raw_pos.d[b1+2] - raw_pos.d[b0+2];
                            float bx = raw_pos.d[b2] - raw_pos.d[b0];
                            float by = raw_pos.d[b2+1] - raw_pos.d[b0+1];
                            float bz = raw_pos.d[b2+2] - raw_pos.d[b0+2];
                            float nx = ay*bz - az*by;
                            float ny = az*bx - ax*bz;
                            float nz = ax*by - ay*bx;
                            float l = sqrtf(nx*nx + ny*ny + nz*nz);
                            if (l > 1e-8f) { nx/=l; ny/=l; nz/=l; }
                            int gen_idx = raw_norm.n / 3;
                            farr_push(&raw_norm, nx);
                            farr_push(&raw_norm, ny);
                            farr_push(&raw_norm, nz);
                            ni[i] = gen_idx;
                        }
                    }

                    for (int i = 1; i < nv - 1; i++) {
                        int face_verts[3] = {0, i, i+1};
                        for (int fv = 0; fv < 3; fv++) {
                            int vi = face_verts[fv];
                            VKey key = { pi[vi], ni[vi], ti[vi] };
                            bool was_new = false;
                            int out_idx = vkarr_find_or_add(&part_vkeys[cur_slot], key, &was_new);

                            if (was_new) {
                                int pb = pi[vi] * 3;
                                farr_push(&part_pos[cur_slot], raw_pos.d[pb]);
                                farr_push(&part_pos[cur_slot], raw_pos.d[pb+1]);
                                farr_push(&part_pos[cur_slot], raw_pos.d[pb+2]);

                                int nb = ni[vi] * 3;
                                farr_push(&part_norm[cur_slot], raw_norm.d[nb]);
                                farr_push(&part_norm[cur_slot], raw_norm.d[nb+1]);
                                farr_push(&part_norm[cur_slot], raw_norm.d[nb+2]);

                                if (ti[vi] >= 0 && raw_tc.n > 0) {
                                    int tb = ti[vi] * 2;
                                    farr_push(&part_tc[cur_slot], raw_tc.d[tb]);
                                    farr_push(&part_tc[cur_slot], raw_tc.d[tb+1]);
                                } else {
                                    farr_push(&part_tc[cur_slot], 0.0f);
                                    farr_push(&part_tc[cur_slot], 0.0f);
                                }
                            }
                            uarr_push(&part_idx[cur_slot], (uint32_t)out_idx);
                        }
                    }
                }
            }
        }

        p = line_end;
        if (p < end_ptr && *p == '\r') p++;
        if (p < end_ptr && *p == '\n') p++;
    }

    int parsed_glows = acc->glow_count;
    acc->glow_count = 0;
    int fallback_attach = (first_attach >= 0) ? first_attach : ANIM_PART_HEAD;
    for (int i = 0; i < parsed_glows; i++) {
        int attach = glow_attach[i];
        if (attach < 0) attach = fallback_attach;
        glow_commit(acc, attach, &glow_pos[i], glow_u[i], glow_v[i], glow_has_uv[i]);
    }

    int loaded = 0;
    for (int i = 0; i < slot_count; i++) {
        int vc = part_pos[i].n / 3;
        int ic = part_idx[i].n;
        if (vc == 0 || ic == 0) continue;

        MeshData md = {0};
        md.vertex_count = vc;
        md.index_count = ic;
        md.positions = part_pos[i].d;
        md.normals = part_norm[i].d;
        md.texcoords = part_tc[i].n > 0 ? part_tc[i].d : NULL;
        md.indices = part_idx[i].d;

        if (mesh_upload(&md, &acc->parts[i].mesh)) {
            acc->parts[i].attach_part = part_attach[i];
            acc->parts[i].valid = true;
            loaded++;
        }
    }

    free(raw_pos.d); free(raw_norm.d); free(raw_tc.d);
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        free(part_pos[i].d); free(part_norm[i].d);
        free(part_tc[i].d); free(part_idx[i].d);
        free(part_vkeys[i].d);
    }
    for (int i = 0; i < ACCESSORY_MAX_GLOWS; i++)
        free(glow_pos[i].d);

    accessory_sample_glows(acc);

    acc->part_count = loaded;
    acc->loaded = (loaded > 0 || acc->glow_count > 0);
    return acc->loaded;
}

void accessory_unload(Accessory* acc) {
    if (!acc) return;
    for (int i = 0; i < ACCESSORY_MAX_PARTS; i++) {
        if (acc->parts[i].valid) {
            acc->parts[i].valid = false;
        }
    }
    accessory_free_atlas(acc);
    acc->loaded = false;
    acc->part_count = 0;
    acc->glow_count = 0;
    memset(acc->glows, 0, sizeof(acc->glows));
}
