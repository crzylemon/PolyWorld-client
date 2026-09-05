/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emote_wheel.h                                                                       |
|   Purpose: hold B, pick an emote                                                            |
\*-------------------------------------------------------------------------------------------*/

#ifndef EMOTE_WHEEL_H
#define EMOTE_WHEEL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "pw_features.h"
#include "emote.h"
#include "avatar_anim.h"
#include "chat.h"

typedef struct {
    unsigned int wheel_tex;
    unsigned int select_tex;
    bool open;
    bool was_held;
    int hover_sector;
} EmoteWheel;

#ifdef PW_ENABLE_CUSTOM_EMOTES

void emote_wheel_init(EmoteWheel* w);
void emote_wheel_load_textures(EmoteWheel* w);
void emote_wheel_set_textures(EmoteWheel* w, unsigned int wheel_tex, unsigned int select_tex);
void emote_wheel_shutdown(EmoteWheel* w);

bool emote_wheel_update(EmoteWheel* w, bool hold_b, bool chat_or_menu_blocks,
                        float mouse_x, float mouse_y, int screen_w, int screen_h,
                        const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                        const uint8_t anim_bases[PW_MAX_EQUIPPED_EMOTES],
                        AnimState* out_anim,
                        uint32_t* out_emote_id);

void emote_wheel_render(EmoteWheel* w, Chat* chat, int screen_w, int screen_h,
                        const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                        const char names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN]);

bool emote_wheel_is_open(const EmoteWheel* w);

int emote_wheel_sector_at(float mouse_x, float mouse_y, int screen_w, int screen_h);

#else

static inline void emote_wheel_init(EmoteWheel* w) {
    if (w) memset(w, 0, sizeof(*w));
}
static inline void emote_wheel_load_textures(EmoteWheel* w) { (void)w; }
static inline void emote_wheel_set_textures(EmoteWheel* w, unsigned int wheel_tex, unsigned int select_tex) {
    (void)w; (void)wheel_tex; (void)select_tex;
}
static inline void emote_wheel_shutdown(EmoteWheel* w) { (void)w; }
static inline bool emote_wheel_update(EmoteWheel* w, bool hold_b, bool chat_or_menu_blocks,
                                      float mouse_x, float mouse_y, int screen_w, int screen_h,
                                      const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                                      const uint8_t anim_bases[PW_MAX_EQUIPPED_EMOTES],
                                      AnimState* out_anim,
                                      uint32_t* out_emote_id) {
    (void)w; (void)hold_b; (void)chat_or_menu_blocks;
    (void)mouse_x; (void)mouse_y; (void)screen_w; (void)screen_h;
    (void)slots; (void)anim_bases;
    if (out_anim) *out_anim = ANIM_STATE_IDLE;
    if (out_emote_id) *out_emote_id = 0;
    return false;
}
static inline void emote_wheel_render(EmoteWheel* w, Chat* chat, int screen_w, int screen_h,
                                      const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                                      const char names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN]) {
    (void)w; (void)chat; (void)screen_w; (void)screen_h; (void)slots; (void)names;
}
static inline bool emote_wheel_is_open(const EmoteWheel* w) {
    (void)w;
    return false;
}
static inline int emote_wheel_sector_at(float mouse_x, float mouse_y, int screen_w, int screen_h) {
    (void)mouse_x; (void)mouse_y; (void)screen_w; (void)screen_h;
    return -1;
}

#endif

#endif
