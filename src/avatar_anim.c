/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar_anim.c                                                                       |
|   Purpose: R6 part anims                                                                    |
\*-------------------------------------------------------------------------------------------*/

#include "avatar_anim.h"
#include "emote_clip.h"
#include "log.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const struct {
    const char* display_name;
    Vec3 pivot;
    Vec3 center;
} PART_INFO[AVATAR_PART_COUNT] = {

    [ANIM_PART_HEAD]      = { "Head",      {0.0f, 8.00f, 0.0f}, {0.0f, 9.25f, 0.0f} },

    [ANIM_PART_TORSO]     = { "Torso",     {0.0f, 6.0f, 0.0f},  {0.0f, 6.0f, 0.0f} },

    [ANIM_PART_RIGHT_ARM] = { "Right Arm", {0.0f, 7.5f, 3.0f},  {0.0f, 5.5f, 3.0f} },
    [ANIM_PART_LEFT_ARM]  = { "Left Arm",  {0.0f, 7.5f, -3.0f}, {0.0f, 5.5f, -3.0f} },
    [ANIM_PART_RIGHT_LEG] = { "Right Leg", {0.0f, 4.0f, 1.0f},  {0.0f, 2.0f, 1.0f} },
    [ANIM_PART_LEFT_LEG]  = { "Left Leg",  {0.0f, 4.0f, -1.0f}, {0.0f, 2.0f, -1.0f} },
};

static const char* PART_ALIASES[AVATAR_PART_COUNT][12] = {
    [ANIM_PART_HEAD] = {
        "Head", "head", NULL
    },
    [ANIM_PART_TORSO] = {
        "Torso", "torso", "Cube.004", NULL
    },
    [ANIM_PART_RIGHT_ARM] = {
        "Right_Arm", "Right Arm", "RightArm", "right_arm", "right arm", "rightarm", "Cube", NULL
    },
    [ANIM_PART_LEFT_ARM] = {
        "Left_Arm", "Left Arm", "LeftArm", "left_arm", "left arm", "leftarm", "Cube.001", NULL
    },
    [ANIM_PART_RIGHT_LEG] = {
        "Right_Leg", "Right Leg", "RightLeg", "right_leg", "right leg", "rightleg", "Cube.003", NULL
    },
    [ANIM_PART_LEFT_LEG] = {
        "Left_Leg", "Left Leg", "LeftLeg", "left_leg", "left leg", "leftleg", "Cube.002", NULL
    },
};

static bool part_alias_equals(const char* a, int a_len, const char* b) {
    int bi = 0;
    int ai = 0;
    while (ai < a_len && b[bi] != '\0') {
        char ca = a[ai];
        char cb = b[bi];

        if (ca == ' ' || ca == '_' || ca == '-') { ai++; continue; }
        if (cb == ' ' || cb == '_' || cb == '-') { bi++; continue; }
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        ai++;
        bi++;
    }
    while (ai < a_len && (a[ai] == ' ' || a[ai] == '_' || a[ai] == '-')) ai++;
    while (b[bi] == ' ' || b[bi] == '_' || b[bi] == '-') bi++;
    return ai == a_len && b[bi] == '\0';
}

static int match_avatar_part(const char* name_start, int name_len) {

    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        for (int a = 0; PART_ALIASES[i][a] != NULL; a++) {
            const char* alias = PART_ALIASES[i][a];
            int alen = (int)strlen(alias);
            if (name_len == alen && strncmp(name_start, alias, alen) == 0) {
                return i;
            }
        }
    }

    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        for (int a = 0; PART_ALIASES[i][a] != NULL; a++) {
            if (part_alias_equals(name_start, name_len, PART_ALIASES[i][a])) {
                return i;
            }
        }
    }
    return -1;
}

typedef struct { float* d; int n, cap; } FArr;
typedef struct { uint32_t* d; int n, cap; } UArr;

static void farr_push(FArr* a, float v) {
    if (a->n >= a->cap) { a->cap = a->cap ? a->cap*2 : 256; a->d = realloc(a->d, a->cap*sizeof(float)); }
    a->d[a->n++] = v;
}
static void uarr_push(UArr* a, uint32_t v) {
    if (a->n >= a->cap) { a->cap = a->cap ? a->cap*2 : 256; a->d = realloc(a->d, a->cap*sizeof(uint32_t)); }
    a->d[a->n++] = v;
}

typedef struct { int pi, ni, ti; } VKey;
typedef struct { VKey* d; int n, cap; int* bucket; int bcap; } VKArr;

static uint32_t vkey_hash(VKey k) {
    uint32_t h = (uint32_t)k.pi * 73856093u;
    h ^= (uint32_t)k.ni * 19349663u;
    h ^= (uint32_t)k.ti * 83492791u;
    return h;
}

static void vkarr_rehash(VKArr* a, int nb) {
    int* nbuck = (int*)malloc((size_t)nb * sizeof(int));
    if (!nbuck) return;
    for (int i = 0; i < nb; i++) nbuck[i] = -1;
    int mask = nb - 1;
    for (int i = 0; i < a->n; i++) {
        uint32_t h = vkey_hash(a->d[i]) & (uint32_t)mask;
        while (nbuck[h] >= 0) h = (h + 1u) & (uint32_t)mask;
        nbuck[h] = i;
    }
    free(a->bucket);
    a->bucket = nbuck;
    a->bcap = nb;
}

static int vkarr_find_or_add(VKArr* a, VKey k, bool* was_new) {
    if (a->bcap <= 0) vkarr_rehash(a, 64);
    if (a->bcap > 0 && a->n * 2 >= a->bcap) vkarr_rehash(a, a->bcap * 2);
    if (a->bcap > 0 && a->bucket) {
        int mask = a->bcap - 1;
        uint32_t h = vkey_hash(k) & (uint32_t)mask;
        for (;;) {
            int i = a->bucket[h];
            if (i < 0) break;
            if (a->d[i].pi == k.pi && a->d[i].ni == k.ni && a->d[i].ti == k.ti) {
                *was_new = false;
                return i;
            }
            h = (h + 1u) & (uint32_t)mask;
        }
        if (a->n >= a->cap) {
            a->cap = a->cap ? a->cap * 2 : 64;
            a->d = realloc(a->d, (size_t)a->cap * sizeof(VKey));
        }
        a->d[a->n] = k;
        a->bucket[h] = a->n;
        *was_new = true;
        return a->n++;
    }
    for (int i = 0; i < a->n; i++) {
        if (a->d[i].pi == k.pi && a->d[i].ni == k.ni && a->d[i].ti == k.ti) {
            *was_new = false;
            return i;
        }
    }
    if (a->n >= a->cap) { a->cap = a->cap ? a->cap*2 : 64; a->d = realloc(a->d, a->cap*sizeof(VKey)); }
    a->d[a->n] = k;
    *was_new = true;
    return a->n++;
}

bool avatar_anim_load(AvatarAnim* anim, const char* obj_data, size_t len) {
    memset(anim, 0, sizeof(AvatarAnim));

    FArr raw_pos = {0}, raw_norm = {0}, raw_tc = {0};

    FArr part_pos[AVATAR_PART_COUNT] = {{0}};
    FArr part_norm[AVATAR_PART_COUNT] = {{0}};
    FArr part_tc[AVATAR_PART_COUNT] = {{0}};
    UArr part_idx[AVATAR_PART_COUNT] = {{0}};
    VKArr part_vkeys[AVATAR_PART_COUNT] = {{0}};

    int cur_part = -1;
    const char* p = obj_data;
    const char* end = obj_data + len;

    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;

        while (p < line_end && (*p == ' ' || *p == '\t')) p++;

        if (p < line_end && ((p[0] == 'o' || p[0] == 'g') && p+1 < line_end && p[1] == ' ')) {

            const char* name_start = p + 2;
            int name_len = (int)(line_end - name_start);
            while (name_len > 0 && (name_start[name_len-1] == ' ' || name_start[name_len-1] == '\r')) name_len--;

            cur_part = match_avatar_part(name_start, name_len);
            if (cur_part >= 0) {
                PW_LOG("[AvatarAnim] Matched '%.*s' -> %s\n",
                       name_len, name_start, PART_INFO[cur_part].display_name);
            } else {
                PW_LOG("[AvatarAnim] Unrecognized object '%.*s' (skipped)\n",
                       name_len, name_start);
            }
        } else if (p < line_end && *p == 'v' && p+1 < line_end && p[1] == ' ') {

            const char* cp = p + 2;
            float x, y, z;
            x = strtof(cp, (char**)&cp);
            y = strtof(cp, (char**)&cp);
            z = strtof(cp, (char**)&cp);
            farr_push(&raw_pos, x);
            farr_push(&raw_pos, y);
            farr_push(&raw_pos, z);
        } else if (p+1 < line_end && *p == 'v' && p[1] == 'n' && p+2 < line_end && p[2] == ' ') {

            const char* cp = p + 3;
            float x, y, z;
            x = strtof(cp, (char**)&cp);
            y = strtof(cp, (char**)&cp);
            z = strtof(cp, (char**)&cp);
            farr_push(&raw_norm, x);
            farr_push(&raw_norm, y);
            farr_push(&raw_norm, z);
        } else if (p+1 < line_end && *p == 'v' && p[1] == 't' && p+2 < line_end && p[2] == ' ') {

            const char* cp = p + 3;
            float u, v;
            u = strtof(cp, (char**)&cp);
            v = strtof(cp, (char**)&cp);
            farr_push(&raw_tc, u);
            farr_push(&raw_tc, v);
        } else if (p < line_end && *p == 'f' && p+1 < line_end && (p[1] == ' ' || p[1] == '\t')) {

            if (cur_part >= 0) {
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

                if (nv >= 3) {

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
                            int out_idx = vkarr_find_or_add(&part_vkeys[cur_part], key, &was_new);

                            if (was_new) {
                                int pb = pi[vi] * 3;
                                farr_push(&part_pos[cur_part], raw_pos.d[pb]);
                                farr_push(&part_pos[cur_part], raw_pos.d[pb+1]);
                                farr_push(&part_pos[cur_part], raw_pos.d[pb+2]);
                                int nb = ni[vi] * 3;
                                farr_push(&part_norm[cur_part], raw_norm.d[nb]);
                                farr_push(&part_norm[cur_part], raw_norm.d[nb+1]);
                                farr_push(&part_norm[cur_part], raw_norm.d[nb+2]);
                                if (ti[vi] >= 0 && raw_tc.n > 0) {
                                    int tb = ti[vi] * 2;
                                    farr_push(&part_tc[cur_part], raw_tc.d[tb]);
                                    farr_push(&part_tc[cur_part], raw_tc.d[tb+1]);
                                } else {
                                    farr_push(&part_tc[cur_part], 0.0f);
                                    farr_push(&part_tc[cur_part], 0.0f);
                                }
                            }
                            uarr_push(&part_idx[cur_part], (uint32_t)out_idx);
                        }
                    }
                }
            }
        }

        p = line_end;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        anim->parts[i].pivot = PART_INFO[i].pivot;

        int vc = part_pos[i].n / 3;
        int ic = part_idx[i].n;
        if (vc == 0 || ic == 0) {
            PW_LOG("[AvatarAnim] Part '%s' has no geometry\n", PART_INFO[i].display_name);
            continue;
        }

        MeshData md = {0};
        md.vertex_count = vc;
        md.index_count = ic;
        md.positions = part_pos[i].d;
        md.normals = part_norm[i].d;
        md.texcoords = part_tc[i].n > 0 ? part_tc[i].d : NULL;
        md.indices = part_idx[i].d;

        if (mesh_upload(&md, &anim->parts[i].mesh)) {
            anim->parts[i].valid = true;
        }

    }

    free(raw_pos.d); free(raw_norm.d); free(raw_tc.d);
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        free(part_pos[i].d);
        free(part_norm[i].d);
        free(part_tc[i].d);
        free(part_idx[i].d);
        free(part_vkeys[i].d);
        free(part_vkeys[i].bucket);
    }

    int loaded = 0;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) if (anim->parts[i].valid) loaded++;
    PW_LOG("[AvatarAnim] Loaded %d/6 parts\n", loaded);
    return loaded > 0;
}

void avatar_anim_clear(AvatarAnim* anim) {
    if (!anim) return;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        if (anim->parts[i].valid) {
            mesh_gpu_free(&anim->parts[i].mesh);
            anim->parts[i].valid = false;
        }
        memset(&anim->parts[i], 0, sizeof(anim->parts[i]));
    }
}

void avatar_anim_detach(AvatarAnim* anim) {
    if (!anim) return;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        memset(&anim->parts[i], 0, sizeof(anim->parts[i]));
    }
}

static void avatar_anim_move_part(AvatarAnim* dst, AvatarAnim* src, int part) {
    if (!dst || !src || part < 0 || part >= AVATAR_PART_COUNT) return;
    if (dst->parts[part].valid)
        mesh_gpu_free(&dst->parts[part].mesh);
    dst->parts[part] = src->parts[part];
    src->parts[part].valid = false;
    memset(&src->parts[part].mesh, 0, sizeof(src->parts[part].mesh));
}

void avatar_anim_apply_mesh_flags(AvatarAnim* dst,
                                  const AvatarAnim* legacy_body,
                                  const AvatarAnim* new_body,
                                  int mesh_flags) {
    if (!dst) return;
    bool shirt_new = (mesh_flags & 1) != 0;
    bool pants_new = (mesh_flags & 2) != 0;
    bool head_new = (mesh_flags & 4) != 0;
    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        bool use_new = false;
        if (p == ANIM_PART_HEAD) use_new = head_new;
        else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) use_new = pants_new;
        else use_new = shirt_new;
        const AvatarAnim* src = use_new ? new_body : legacy_body;
        if (!src) src = legacy_body ? legacy_body : new_body;
        if (!src || !src->parts[p].valid) {

            const AvatarAnim* alt = (src == new_body) ? legacy_body : new_body;
            if (alt && alt->parts[p].valid) src = alt;
            else continue;
        }

        dst->parts[p] = src->parts[p];
    }
}

bool avatar_anim_compose(AvatarAnim* out,
                         const char* legacy_data, size_t legacy_len,
                         const char* new_data, size_t new_len,
                         int mesh_flags) {
    if (!out) return false;
    bool shirt_new = (mesh_flags & 1) != 0;
    bool pants_new = (mesh_flags & 2) != 0;
    bool head_new = (mesh_flags & 4) != 0;
    bool need_legacy = !shirt_new || !pants_new || !head_new;
    bool need_new = shirt_new || pants_new || head_new;

    AvatarAnim leg = {0}, neu = {0};
    if (need_legacy) {
        if (!legacy_data || legacy_len == 0 || !avatar_anim_load(&leg, legacy_data, legacy_len)) {
            avatar_anim_clear(&leg);
            return false;
        }
    }
    if (need_new) {
        if (!new_data || new_len == 0 || !avatar_anim_load(&neu, new_data, new_len)) {
            avatar_anim_clear(&leg);
            avatar_anim_clear(&neu);
            return false;
        }
    }

    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        if (out->parts[p].valid)
            mesh_gpu_free(&out->parts[p].mesh);
        out->parts[p].valid = false;
        memset(&out->parts[p].mesh, 0, sizeof(out->parts[p].mesh));
    }

    for (int p = 0; p < AVATAR_PART_COUNT; p++) {
        bool use_new = false;
        if (p == ANIM_PART_HEAD) use_new = head_new;
        else if (p == ANIM_PART_RIGHT_LEG || p == ANIM_PART_LEFT_LEG) use_new = pants_new;
        else use_new = shirt_new;
        AvatarAnim* src = use_new ? &neu : &leg;
        if (!src->parts[p].valid) {
            AvatarAnim* alt = use_new ? &leg : &neu;
            if (alt->parts[p].valid) src = alt;
            else continue;
        }
        avatar_anim_move_part(out, src, p);
    }

    avatar_anim_clear(&leg);
    avatar_anim_clear(&neu);
    int loaded = 0;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) if (out->parts[i].valid) loaded++;
    PW_LOG("[AvatarAnim] Composed %d/6 parts (flags=%d)\n", loaded, mesh_flags);
    return loaded > 0;
}

static Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }

static Vec3 v3_lerp(Vec3 a, Vec3 b, float t) {
    return (Vec3){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

static Vec3 pose_mirror_lr(Vec3 r) {
    return (Vec3){-r.x, -r.y, r.z};
}

static Vec3 pose_wave(Vec3 a, Vec3 b, float phase) {
    float t = (sinf(phase) + 1.0f) * 0.5f;
    return v3_lerp(a, b, t);
}

static void smooth_vec3(Vec3* cur, Vec3 target, float rate, float dt) {
    float a = 1.0f - expf(-rate * dt);
    if (a > 1.0f) a = 1.0f;
    *cur = v3_lerp(*cur, target, a);
    float dx = target.x - cur->x, dy = target.y - cur->y, dz = target.z - cur->z;
    if (dx * dx + dy * dy + dz * dz < 1e-6f)
        *cur = target;
}

void avatar_anim_update(AvatarAnim* anim, AnimState state, float speed, float dt) {
    AnimState prev = anim->state;
    anim->state = state;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    if (state == ANIM_STATE_WALKING) {
        anim->walk_phase += speed * dt * 0.4f;
        if (anim->walk_phase > 2.0f * (float)M_PI) anim->walk_phase -= 2.0f * (float)M_PI;
    } else if (anim->walk_phase != 0.0f) {

        anim->walk_phase *= expf(-12.0f * dt);
        if (fabsf(anim->walk_phase) < 0.01f) anim->walk_phase = 0.0f;
    }

    if (state == ANIM_STATE_CLIMBING) {
        float climb_spd = fabsf(speed);
        if (climb_spd > 0.5f) {
            anim->climb_phase += climb_spd * dt * 0.55f;
            if (anim->climb_phase > 2.0f * (float)M_PI) anim->climb_phase -= 2.0f * (float)M_PI;
        }
    } else if (anim->climb_phase != 0.0f) {
        anim->climb_phase *= expf(-10.0f * dt);
        if (fabsf(anim->climb_phase) < 0.01f) anim->climb_phase = 0.0f;
    }

    if (state == ANIM_STATE_DANCING || state == ANIM_STATE_DANCING2 || state == ANIM_STATE_DANCING3) {
        anim->dance_phase += dt * 8.0f;
    } else {
        anim->dance_phase = 0.0f;
    }

    if (state == ANIM_STATE_EMOTE) {
#ifdef PW_ENABLE_CUSTOM_EMOTES
        if (prev != ANIM_STATE_EMOTE)
            anim->emote_time = 0.0f;
        if (!anim->emote_clip && anim->emote_id)
            anim->emote_clip = emote_clip_get(anim->emote_id);
        if (anim->emote_clip)
            anim->emote_time += dt;
#else
        (void)prev;
        anim->emote_time = 0.0f;
        anim->emote_clip = NULL;
#endif
    } else {
        anim->emote_time = 0.0f;
    }

    Vec3 target_rot[AVATAR_PART_COUNT] = {{0}};
    Vec3 target_pos[AVATAR_PART_COUNT] = {{0}};

    switch (state) {
        case ANIM_STATE_JUMPING: {

            target_rot[ANIM_PART_HEAD]      = v3(0.0f, 0.0f, 5.0f);
            target_pos[ANIM_PART_HEAD]      = v3(0.04f, -0.04f, 0.0f);
            target_rot[ANIM_PART_RIGHT_ARM] = v3(5.0f, 5.0f, 170.0f);
            target_rot[ANIM_PART_LEFT_ARM]  = pose_mirror_lr(target_rot[ANIM_PART_RIGHT_ARM]);
            target_rot[ANIM_PART_RIGHT_LEG] = v3(0.0f, -2.0f, 5.0f);
            target_rot[ANIM_PART_LEFT_LEG]  = v3(0.0f, 2.0f, -5.0f);
            break;
        }
        case ANIM_STATE_CLIMBING: {

            float climb_spd = fabsf(speed);
            target_rot[ANIM_PART_HEAD]  = v3(0.0f, 0.0f, 0.0f);
            target_rot[ANIM_PART_TORSO] = v3(0.0f, 0.0f, 0.0f);
            target_pos[ANIM_PART_HEAD]  = v3(0.0f, 0.0f, 0.0f);

            if (climb_spd < 0.5f) {

                Vec3 r_arm = v3(0.0f, 10.0f, 145.0f);
                Vec3 r_leg = v3(0.0f, -4.0f, 10.0f);
                target_rot[ANIM_PART_RIGHT_ARM] = r_arm;
                target_rot[ANIM_PART_LEFT_ARM]  = pose_mirror_lr(r_arm);
                target_rot[ANIM_PART_RIGHT_LEG] = r_leg;
                target_rot[ANIM_PART_LEFT_LEG]  = pose_mirror_lr(r_leg);
            } else {
                float phase = anim->climb_phase;
                float opp = phase + (float)M_PI;

                Vec3 r_arm_a = v3(0.0f, 10.0f, 165.0f);
                Vec3 r_arm_b = v3(0.0f, 10.0f, 100.0f);
                target_rot[ANIM_PART_RIGHT_ARM] = pose_wave(r_arm_a, r_arm_b, phase);
                target_rot[ANIM_PART_LEFT_ARM]  = pose_wave(pose_mirror_lr(r_arm_a), pose_mirror_lr(r_arm_b), opp);

                Vec3 r_leg_a = v3(0.0f, -5.0f, 35.0f);
                Vec3 r_leg_b = v3(0.0f, -5.0f, -35.0f);
                target_rot[ANIM_PART_RIGHT_LEG] = pose_wave(r_leg_a, r_leg_b, phase);
                target_rot[ANIM_PART_LEFT_LEG]  = pose_wave(pose_mirror_lr(r_leg_a), pose_mirror_lr(r_leg_b), opp);
            }
            break;
        }
        case ANIM_STATE_WALKING: {
            float phase = anim->walk_phase;
            float opp = phase + (float)M_PI;

            target_rot[ANIM_PART_HEAD] = pose_wave(v3(0.0f, -2.0f, 1.0f), v3(0.0f, 2.0f, 1.0f), phase);
            target_pos[ANIM_PART_HEAD] = v3(0.02f, 0.0f, 0.0f);
            target_rot[ANIM_PART_TORSO] = v3(0.0f, 0.0f, 0.0f);

            Vec3 r_arm_a = v3(0.0f, 0.0f, -35.0f);
            Vec3 r_arm_b = v3(0.0f, 0.0f, 35.0f);
            target_rot[ANIM_PART_RIGHT_ARM] = pose_wave(r_arm_a, r_arm_b, phase);
            target_rot[ANIM_PART_LEFT_ARM]  = pose_wave(pose_mirror_lr(r_arm_a), pose_mirror_lr(r_arm_b), opp);

            Vec3 r_leg_a = v3(0.0f, -2.0f, 25.0f);
            Vec3 r_leg_b = v3(0.0f, -2.0f, -25.0f);
            target_rot[ANIM_PART_RIGHT_LEG] = pose_wave(r_leg_a, r_leg_b, phase);
            target_rot[ANIM_PART_LEFT_LEG]  = pose_wave(pose_mirror_lr(r_leg_a), pose_mirror_lr(r_leg_b), opp);
            break;
        }
        case ANIM_STATE_DANCING: {
            float dp = anim->dance_phase;
            target_rot[ANIM_PART_RIGHT_LEG].z = sinf(dp * 0.5f) * 60.0f;
            target_rot[ANIM_PART_LEFT_LEG].z = -sinf(dp * 0.5f) * 60.0f;
            float cycle = fmodf(dp, 8.0f * (float)M_PI) / (4.0f * (float)M_PI);
            if (cycle >= 1.0f) {
                float arm_phase = dp;
                target_rot[ANIM_PART_RIGHT_ARM].z = 180.0f + sinf(arm_phase) * 60.0f;
                target_rot[ANIM_PART_LEFT_ARM].z = 180.0f + -sinf(arm_phase) * 60.0f;
            } else {
                float arm_phase = dp;
                target_rot[ANIM_PART_RIGHT_ARM].z = 90.0f + sinf(arm_phase) * 45.0f;
                target_rot[ANIM_PART_LEFT_ARM].z = 90.0f + -sinf(arm_phase) * 45.0f;
            }
            break;
        }
        case ANIM_STATE_DANCING2: {
            float dp = anim->dance_phase;
            float step = fmodf(dp, 2.0f * (float)M_PI);
            target_rot[ANIM_PART_RIGHT_ARM].z = (step < (float)M_PI) ? 180.0f : 0.0f;
            target_rot[ANIM_PART_LEFT_ARM].z = (step < (float)M_PI) ? 0.0f : 180.0f;
            target_rot[ANIM_PART_RIGHT_LEG].z = sinf(dp) * 40.0f;
            target_rot[ANIM_PART_LEFT_LEG].z = -sinf(dp) * 40.0f;
            target_rot[ANIM_PART_HEAD].z = sinf(dp * 2.0f) * 15.0f;
            break;
        }
        case ANIM_STATE_DANCING3: {
            float dp = anim->dance_phase;
            target_rot[ANIM_PART_RIGHT_ARM].z = 90.0f + sinf(dp * 1.5f) * 90.0f;
            target_rot[ANIM_PART_LEFT_ARM].z = 90.0f + cosf(dp * 1.5f) * 90.0f;
            target_rot[ANIM_PART_RIGHT_LEG].z = sinf(dp) * 30.0f;
            target_rot[ANIM_PART_LEFT_LEG].z = sinf(dp) * 30.0f;
            target_rot[ANIM_PART_HEAD].z = sinf(dp * 0.75f) * 20.0f;
            break;
        }
        case ANIM_STATE_EMOTE: {
#ifdef PW_ENABLE_CUSTOM_EMOTES
            if (anim->emote_clip)
                emote_clip_sample(anim->emote_clip, anim->emote_time, target_rot, target_pos);
#endif
            break;
        }
        default:
            break;
    }

    if (anim->tool_hold && state != ANIM_STATE_DEAD)
        target_rot[ANIM_PART_RIGHT_ARM] = v3(0.0f, 0.0f, 90.0f);

    const float rot_rate = 22.0f;
    const float pos_rate = 18.0f;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        if (anim->vr_ik && (i == ANIM_PART_HEAD ||
                            i == ANIM_PART_LEFT_ARM || i == ANIM_PART_RIGHT_ARM))
            continue;
        smooth_vec3(&anim->rot[i], target_rot[i], rot_rate, dt);
        smooth_vec3(&anim->pos[i], target_pos[i], pos_rate, dt);
    }
}

Mat4 avatar_anim_get_part_matrix(const AvatarAnim* anim, int part_index,
                                  Vec3 position, float yaw, float scale) {

    Mat4 base = mat4_identity();
    base = mat4_multiply(base, mat4_translate(position));
    base = mat4_multiply(base, mat4_rotate_y(yaw));
    base = mat4_multiply(base, mat4_scale((Vec3){scale, scale, scale}));

    Vec3 r = anim->rot[part_index];
    Vec3 p = anim->pos[part_index];
    bool has_rot = (r.x != 0.0f || r.y != 0.0f || r.z != 0.0f);
    bool has_pos = (p.x != 0.0f || p.y != 0.0f || p.z != 0.0f);
    if (!has_rot && !has_pos) return base;

    Vec3 pivot = PART_INFO[part_index].pivot;

    Mat4 anim_transform = mat4_identity();
    if (has_pos)
        anim_transform = mat4_multiply(anim_transform, mat4_translate(p));

    if (has_rot) {
        Mat4 to_pivot = mat4_translate((Vec3){pivot.x, pivot.y, pivot.z});

        Mat4 rot = mat4_multiply(mat4_rotate_z(r.z),
                   mat4_multiply(mat4_rotate_y(r.y), mat4_rotate_x(r.x)));
        Mat4 from_pivot = mat4_translate((Vec3){-pivot.x, -pivot.y, -pivot.z});
        Mat4 pivoted = mat4_multiply(to_pivot, mat4_multiply(rot, from_pivot));
        anim_transform = mat4_multiply(anim_transform, pivoted);
    }

    return mat4_multiply(base, anim_transform);
}

Vec3 avatar_anim_get_part_center(int part_index) {
    if (part_index < 0 || part_index >= AVATAR_PART_COUNT) return (Vec3){0, 0, 0};
    return PART_INFO[part_index].center;
}
