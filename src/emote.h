/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emote.h                                                                             |
|   Purpose: emote slots + id -> AnimState                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef EMOTE_H
#define EMOTE_H

#include <stdint.h>
#include <stdio.h>
#include "avatar_anim.h"

#define PW_MAX_EQUIPPED_EMOTES 8
#define PW_EMOTE_NAME_LEN 32

#define PW_EMOTE_DANCE1_ID 100u
#define PW_EMOTE_DANCE2_ID 101u
#define PW_EMOTE_DANCE3_ID 102u

#define PW_EMOTE_ANIM_NONE 0
#define PW_EMOTE_ANIM_DANCE1 1
#define PW_EMOTE_ANIM_DANCE2 2
#define PW_EMOTE_ANIM_DANCE3 3

static inline void emote_default_loadout(uint32_t out[PW_MAX_EQUIPPED_EMOTES]) {
    out[0] = PW_EMOTE_DANCE1_ID;
    out[1] = PW_EMOTE_DANCE2_ID;
    out[2] = PW_EMOTE_DANCE3_ID;
    for (int i = 3; i < PW_MAX_EQUIPPED_EMOTES; i++) out[i] = 0;
}

static inline void emote_default_anim_bases(uint8_t out[PW_MAX_EQUIPPED_EMOTES]) {
    out[0] = PW_EMOTE_ANIM_DANCE1;
    out[1] = PW_EMOTE_ANIM_DANCE2;
    out[2] = PW_EMOTE_ANIM_DANCE3;
    for (int i = 3; i < PW_MAX_EQUIPPED_EMOTES; i++) out[i] = PW_EMOTE_ANIM_NONE;
}

static inline void emote_default_names(char out[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN]) {
    snprintf(out[0], PW_EMOTE_NAME_LEN, "Dance 1");
    snprintf(out[1], PW_EMOTE_NAME_LEN, "Dance 2");
    snprintf(out[2], PW_EMOTE_NAME_LEN, "Dance 3");
    for (int i = 3; i < PW_MAX_EQUIPPED_EMOTES; i++) out[i][0] = '\0';
}

static inline AnimState emote_base_to_anim(uint8_t base) {
    if (base == PW_EMOTE_ANIM_DANCE1) return ANIM_STATE_DANCING;
    if (base == PW_EMOTE_ANIM_DANCE2) return ANIM_STATE_DANCING2;
    if (base == PW_EMOTE_ANIM_DANCE3) return ANIM_STATE_DANCING3;
    return ANIM_STATE_IDLE;
}

static inline AnimState emote_id_to_anim(uint32_t id) {
    if (id == PW_EMOTE_DANCE1_ID) return ANIM_STATE_DANCING;
    if (id == PW_EMOTE_DANCE2_ID) return ANIM_STATE_DANCING2;
    if (id == PW_EMOTE_DANCE3_ID) return ANIM_STATE_DANCING3;
    return ANIM_STATE_IDLE;
}

static inline int emote_anim_is_hold(AnimState s) {
    return (s >= ANIM_STATE_DANCING && s <= ANIM_STATE_DANCING3) || s == ANIM_STATE_EMOTE;
}

static inline uint8_t emote_id_to_base(uint32_t id) {
    if (id == PW_EMOTE_DANCE1_ID) return PW_EMOTE_ANIM_DANCE1;
    if (id == PW_EMOTE_DANCE2_ID) return PW_EMOTE_ANIM_DANCE2;
    if (id == PW_EMOTE_DANCE3_ID) return PW_EMOTE_ANIM_DANCE3;
    return PW_EMOTE_ANIM_NONE;
}

static inline int emote_id_is_builtin(uint32_t id) {
    return id == PW_EMOTE_DANCE1_ID || id == PW_EMOTE_DANCE2_ID || id == PW_EMOTE_DANCE3_ID;
}

#endif
