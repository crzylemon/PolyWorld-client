/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emote_clip.h                                                                        |
|   Purpose: .pwemote parse / sample / cache                                                  |
\*-------------------------------------------------------------------------------------------*/

#ifndef EMOTE_CLIP_H
#define EMOTE_CLIP_H

#include "pw_features.h"
#include "avatar_anim.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define EMOTE_CLIP_MAX_DURATION 8.0f
#define EMOTE_CLIP_MAX_FRAMES   (24 * 8)
#define EMOTE_CLIP_MAX_BYTES    (256 * 1024)
#define EMOTE_CLIP_CACHE_SIZE   32

#define EMOTE_CLIP_SAMPLE_ID    999u

typedef struct {
    float t;
    Vec3 rot[AVATAR_PART_COUNT];
    Vec3 pos[AVATAR_PART_COUNT];
} EmoteClipFrame;

typedef struct EmoteClip {
    int version;
    float fps;
    bool loop;
    float duration;
    int frame_count;
    EmoteClipFrame* frames;
} EmoteClip;

#ifdef PW_ENABLE_CUSTOM_EMOTES

EmoteClip* emote_clip_parse(const char* json, size_t len);

void emote_clip_free(EmoteClip* clip);

void emote_clip_sample(const EmoteClip* clip, float time_sec,
                       Vec3 out_rot[AVATAR_PART_COUNT],
                       Vec3 out_pos[AVATAR_PART_COUNT]);

void emote_clip_set_host(const char* host);

void emote_clip_request(uint32_t catalog_id);

const EmoteClip* emote_clip_get(uint32_t catalog_id);

bool emote_clip_failed(uint32_t catalog_id);

void emote_clip_cache_clear(void);

#else

static inline EmoteClip* emote_clip_parse(const char* json, size_t len) {
    (void)json; (void)len;
    return NULL;
}
static inline void emote_clip_free(EmoteClip* clip) { (void)clip; }
static inline void emote_clip_sample(const EmoteClip* clip, float time_sec,
                                     Vec3 out_rot[AVATAR_PART_COUNT],
                                     Vec3 out_pos[AVATAR_PART_COUNT]) {
    (void)clip; (void)time_sec;
    if (out_rot) {
        for (int i = 0; i < AVATAR_PART_COUNT; i++) out_rot[i] = (Vec3){0};
    }
    if (out_pos) {
        for (int i = 0; i < AVATAR_PART_COUNT; i++) out_pos[i] = (Vec3){0};
    }
}
static inline void emote_clip_set_host(const char* host) { (void)host; }
static inline void emote_clip_request(uint32_t catalog_id) { (void)catalog_id; }
static inline const EmoteClip* emote_clip_get(uint32_t catalog_id) {
    (void)catalog_id;
    return NULL;
}
static inline bool emote_clip_failed(uint32_t catalog_id) {
    (void)catalog_id;
    return true;
}
static inline void emote_clip_cache_clear(void) {}

#endif

#endif
