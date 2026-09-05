/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emote_clip.c                                                                        |
|   Purpose: .pwemote json clips                                                              |
\*-------------------------------------------------------------------------------------------*/

#include "emote_clip.h"

#ifndef PW_ENABLE_CUSTOM_EMOTES
#else

#include "platform.h"
#include "prod_urls.h"
#include "log.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    EMOTE_CACHE_EMPTY = 0,
    EMOTE_CACHE_LOADING,
    EMOTE_CACHE_READY,
    EMOTE_CACHE_FAILED
} EmoteCacheStatus;

typedef struct {
    uint32_t id;
    EmoteCacheStatus status;
    EmoteClip* clip;
} EmoteCacheSlot;

static EmoteCacheSlot g_cache[EMOTE_CLIP_CACHE_SIZE];
static char g_host[256] = PW_SITE_ORIGIN;

void emote_clip_set_host(const char* host) {
    if (!host || !host[0]) {
        snprintf(g_host, sizeof(g_host), "%s", PW_SITE_ORIGIN);
        return;
    }
    size_t n = strlen(host);
    while (n > 0 && host[n - 1] == '/') n--;
    if (n >= sizeof(g_host)) n = sizeof(g_host) - 1;
    memcpy(g_host, host, n);
    g_host[n] = '\0';
}

static const char* skip_ws(const char* p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static int part_name_to_index(const char* name, size_t len) {
    static const char* names[AVATAR_PART_COUNT] = {
        "Head", "Torso", "Right_Arm", "Left_Arm", "Right_Leg", "Left_Leg"
    };
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        if (strlen(names[i]) == len && strncmp(names[i], name, len) == 0)
            return i;
    }
    return -1;
}

static const char* parse_number(const char* p, float* out) {
    p = skip_ws(p);
    if (!p || !*p) return NULL;
    char* end = NULL;
    float v = strtof(p, &end);
    if (end == p) return NULL;
    *out = v;
    return end;
}

static const char* parse_vec3_array(const char* p, Vec3* out) {
    p = skip_ws(p);
    if (!p || *p != '[') return NULL;
    p++;
    float a = 0, b = 0, c = 0;
    p = parse_number(p, &a);
    if (!p) return NULL;
    p = skip_ws(p);
    if (*p == ',') {
        p++;
        p = parse_number(p, &b);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            p = parse_number(p, &c);
            if (!p) return NULL;
        }
    }
    p = skip_ws(p);
    if (*p != ']') return NULL;
    out->x = a; out->y = b; out->z = c;
    return p + 1;
}

static int find_key(const char* obj, const char* key, const char** value_out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = obj;
    while ((pos = strstr(pos, search)) != NULL) {
        const char* after = pos + strlen(search);
        after = skip_ws(after);
        if (*after == ':') {
            *value_out = skip_ws(after + 1);
            return 1;
        }
        pos = after;
    }
    return 0;
}

static const char* object_end(const char* p) {
    if (!p || *p != '{') return NULL;
    int depth = 0;
    bool in_str = false;
    for (const char* s = p; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) { s++; continue; }
            if (*s == '"') in_str = false;
            continue;
        }
        if (*s == '"') { in_str = true; continue; }
        if (*s == '{') depth++;
        else if (*s == '}') {
            depth--;
            if (depth == 0) return s + 1;
        }
    }
    return NULL;
}

static int cmp_frames(const void* a, const void* b) {
    float ta = ((const EmoteClipFrame*)a)->t;
    float tb = ((const EmoteClipFrame*)b)->t;
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

static int parse_parts_object(const char* parts_obj, EmoteClipFrame* fr) {
    const char* end = object_end(parts_obj);
    if (!end) return 0;
    const char* p = parts_obj + 1;
    while (p < end - 1) {
        p = skip_ws(p);
        if (p >= end - 1 || *p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') return 0;
        p++;
        const char* name = p;
        while (p < end && *p && *p != '"') p++;
        if (p >= end || *p != '"') return 0;
        size_t nlen = (size_t)(p - name);
        p++;
        p = skip_ws(p);
        if (*p != ':') return 0;
        p = skip_ws(p + 1);
        if (*p != '{') return 0;
        const char* part_end = object_end(p);
        if (!part_end) return 0;

        int pi = part_name_to_index(name, nlen);
        if (pi >= 0) {
            const char* rval = NULL;
            const char* pval = NULL;
            if (find_key(p, "r", &rval)) {
                if (!parse_vec3_array(rval, &fr->rot[pi])) return 0;
            }
            if (find_key(p, "p", &pval)) {
                if (!parse_vec3_array(pval, &fr->pos[pi])) return 0;
            }
        }
        p = part_end;
    }
    return 1;
}

EmoteClip* emote_clip_parse(const char* json, size_t len) {
    if (!json || len == 0 || len > EMOTE_CLIP_MAX_BYTES) return NULL;

    char* buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, json, len);
    buf[len] = '\0';

    EmoteClip* clip = (EmoteClip*)calloc(1, sizeof(EmoteClip));
    if (!clip) { free(buf); return NULL; }

    const char* v = NULL;
    clip->version = 1;
    if (find_key(buf, "v", &v))
        clip->version = (int)strtol(v, NULL, 10);
    if (clip->version != 1) { free(buf); emote_clip_free(clip); return NULL; }

    clip->fps = 24.0f;
    if (find_key(buf, "fps", &v)) {
        float f = 24.0f;
        if (parse_number(v, &f)) clip->fps = f;
    }

    clip->loop = true;
    if (find_key(buf, "loop", &v)) {
        v = skip_ws(v);
        if (strncmp(v, "false", 5) == 0) clip->loop = false;
        else if (*v == '0') clip->loop = false;
    }

    clip->duration = 1.0f;
    if (find_key(buf, "duration", &v)) {
        float d = 1.0f;
        if (parse_number(v, &d)) clip->duration = d;
    }
    if (clip->duration <= 0.0f || clip->duration > EMOTE_CLIP_MAX_DURATION) {
        free(buf); emote_clip_free(clip); return NULL;
    }

    const char* frames_val = NULL;
    if (!find_key(buf, "frames", &frames_val)) {
        free(buf); emote_clip_free(clip); return NULL;
    }
    frames_val = skip_ws(frames_val);
    if (*frames_val != '[') { free(buf); emote_clip_free(clip); return NULL; }

    EmoteClipFrame* frames = (EmoteClipFrame*)calloc((size_t)EMOTE_CLIP_MAX_FRAMES, sizeof(EmoteClipFrame));
    if (!frames) { free(buf); emote_clip_free(clip); return NULL; }

    const char* p = frames_val + 1;
    int count = 0;
    while (*p && count < EMOTE_CLIP_MAX_FRAMES) {
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') { free(frames); free(buf); emote_clip_free(clip); return NULL; }
        const char* fr_end = object_end(p);
        if (!fr_end) { free(frames); free(buf); emote_clip_free(clip); return NULL; }

        EmoteClipFrame fr;
        memset(&fr, 0, sizeof(fr));
        const char* tval = NULL;
        if (find_key(p, "t", &tval)) {
            if (!parse_number(tval, &fr.t)) {
                free(frames); free(buf); emote_clip_free(clip); return NULL;
            }
        }
        const char* parts = NULL;
        if (find_key(p, "parts", &parts)) {
            parts = skip_ws(parts);
            if (*parts == '{') {
                if (!parse_parts_object(parts, &fr)) {
                    free(frames); free(buf); emote_clip_free(clip); return NULL;
                }
            }
        }
        frames[count++] = fr;
        p = fr_end;
    }

    if (count == 0) { free(frames); free(buf); emote_clip_free(clip); return NULL; }

    qsort(frames, (size_t)count, sizeof(EmoteClipFrame), cmp_frames);
    clip->frames = frames;
    clip->frame_count = count;
    free(buf);
    return clip;
}

void emote_clip_free(EmoteClip* clip) {
    if (!clip) return;
    free(clip->frames);
    free(clip);
}

static void lerp_vec3(Vec3* out, Vec3 a, Vec3 b, float t) {
    out->x = a.x + (b.x - a.x) * t;
    out->y = a.y + (b.y - a.y) * t;
    out->z = a.z + (b.z - a.z) * t;
}

void emote_clip_sample(const EmoteClip* clip, float time_sec,
                       Vec3 out_rot[AVATAR_PART_COUNT],
                       Vec3 out_pos[AVATAR_PART_COUNT]) {
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        out_rot[i] = (Vec3){0, 0, 0};
        out_pos[i] = (Vec3){0, 0, 0};
    }
    if (!clip || !clip->frames || clip->frame_count <= 0) return;

    float t = time_sec;
    if (clip->loop && clip->duration > 0.0f) {
        t = fmodf(t, clip->duration);
        if (t < 0.0f) t += clip->duration;
    } else if (t > clip->duration) {
        t = clip->duration;
    }
    if (t < 0.0f) t = 0.0f;

    const EmoteClipFrame* frames = clip->frames;
    int n = clip->frame_count;
    if (t <= frames[0].t) {
        memcpy(out_rot, frames[0].rot, sizeof(Vec3) * AVATAR_PART_COUNT);
        memcpy(out_pos, frames[0].pos, sizeof(Vec3) * AVATAR_PART_COUNT);
        return;
    }
    if (t >= frames[n - 1].t) {
        memcpy(out_rot, frames[n - 1].rot, sizeof(Vec3) * AVATAR_PART_COUNT);
        memcpy(out_pos, frames[n - 1].pos, sizeof(Vec3) * AVATAR_PART_COUNT);
        return;
    }

    int hi = 1;
    while (hi < n && frames[hi].t < t) hi++;
    int lo = hi - 1;
    float span = frames[hi].t - frames[lo].t;
    float u = (span > 1e-6f) ? (t - frames[lo].t) / span : 0.0f;
    for (int i = 0; i < AVATAR_PART_COUNT; i++) {
        lerp_vec3(&out_rot[i], frames[lo].rot[i], frames[hi].rot[i], u);
        lerp_vec3(&out_pos[i], frames[lo].pos[i], frames[hi].pos[i], u);
    }
}

static EmoteCacheSlot* cache_find(uint32_t id) {
    for (int i = 0; i < EMOTE_CLIP_CACHE_SIZE; i++) {
        if (g_cache[i].status != EMOTE_CACHE_EMPTY && g_cache[i].id == id)
            return &g_cache[i];
    }
    return NULL;
}

static EmoteCacheSlot* cache_alloc(uint32_t id) {
    EmoteCacheSlot* s = cache_find(id);
    if (s) return s;
    for (int i = 0; i < EMOTE_CLIP_CACHE_SIZE; i++) {
        if (g_cache[i].status == EMOTE_CACHE_EMPTY) {
            g_cache[i].id = id;
            g_cache[i].status = EMOTE_CACHE_LOADING;
            g_cache[i].clip = NULL;
            return &g_cache[i];
        }
    }
    for (int i = 0; i < EMOTE_CLIP_CACHE_SIZE; i++) {
        if (g_cache[i].status == EMOTE_CACHE_FAILED) {
            emote_clip_free(g_cache[i].clip);
            g_cache[i].id = id;
            g_cache[i].status = EMOTE_CACHE_LOADING;
            g_cache[i].clip = NULL;
            return &g_cache[i];
        }
    }
    emote_clip_free(g_cache[0].clip);
    g_cache[0].id = id;
    g_cache[0].status = EMOTE_CACHE_LOADING;
    g_cache[0].clip = NULL;
    return &g_cache[0];
}

static void cache_finish(uint32_t id, EmoteClip* clip) {
    EmoteCacheSlot* s = cache_find(id);
    if (!s) {
        emote_clip_free(clip);
        return;
    }
    if (s->status == EMOTE_CACHE_READY && s->clip) {
        emote_clip_free(clip);
        return;
    }
    emote_clip_free(s->clip);
    s->clip = clip;
    s->status = clip ? EMOTE_CACHE_READY : EMOTE_CACHE_FAILED;
}

static void on_emote_http_loaded(const char* path, const uint8_t* data, size_t len, void* user);

static void on_emote_local_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    uint32_t id = (uint32_t)(uintptr_t)user;
    (void)path;
    if (data && len > 0 && len <= EMOTE_CLIP_MAX_BYTES) {
        EmoteClip* clip = emote_clip_parse((const char*)data, len);
        if (clip) {
            cache_finish(id, clip);
            return;
        }
        PW_LOG("[EmoteClip] local parse failed id=%u\n", id);
    }
    char url[384];
    snprintf(url, sizeof(url), "%s/uploads/emotes/%u.pwemote", g_host, id);
    platform_load_file(url, on_emote_http_loaded, (void*)(uintptr_t)id);
}

static void on_emote_http_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    uint32_t id = (uint32_t)(uintptr_t)user;
    (void)path;
    if (!data || len == 0 || len > EMOTE_CLIP_MAX_BYTES) {
        cache_finish(id, NULL);
        return;
    }
    EmoteClip* clip = emote_clip_parse((const char*)data, len);
    if (!clip)
        PW_LOG("[EmoteClip] HTTP parse failed id=%u\n", id);
    cache_finish(id, clip);
}

void emote_clip_request(uint32_t catalog_id) {
    if (catalog_id == 0) return;
    EmoteCacheSlot* s = cache_find(catalog_id);
    if (s && (s->status == EMOTE_CACHE_READY || s->status == EMOTE_CACHE_LOADING ||
              s->status == EMOTE_CACHE_FAILED))
        return;

    s = cache_alloc(catalog_id);
    if (!s) return;

    char path[128];
    snprintf(path, sizeof(path), "assets/emotes/%u.pwemote", catalog_id);
    platform_load_file(path, on_emote_local_loaded, (void*)(uintptr_t)catalog_id);
}

const EmoteClip* emote_clip_get(uint32_t catalog_id) {
    if (catalog_id == 0) return NULL;
    EmoteCacheSlot* s = cache_find(catalog_id);
    if (!s) {
        emote_clip_request(catalog_id);
        return NULL;
    }
    if (s->status == EMOTE_CACHE_READY) return s->clip;
    if (s->status == EMOTE_CACHE_EMPTY)
        emote_clip_request(catalog_id);
    return NULL;
}

bool emote_clip_failed(uint32_t catalog_id) {
    EmoteCacheSlot* s = cache_find(catalog_id);
    return s && s->status == EMOTE_CACHE_FAILED;
}

void emote_clip_cache_clear(void) {
    for (int i = 0; i < EMOTE_CLIP_CACHE_SIZE; i++) {
        emote_clip_free(g_cache[i].clip);
        g_cache[i].clip = NULL;
        g_cache[i].id = 0;
        g_cache[i].status = EMOTE_CACHE_EMPTY;
    }
}

#endif
