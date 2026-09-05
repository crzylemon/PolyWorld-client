/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: emote_wheel.c                                                                       |
|   Purpose: hold B, pick an emote                                                            |
\*-------------------------------------------------------------------------------------------*/

#include "emote_wheel.h"

#ifndef PW_ENABLE_CUSTOM_EMOTES

#else

#include "font.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void emote_wheel_init(EmoteWheel* w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->hover_sector = 0;
}

void emote_wheel_load_textures(EmoteWheel* w) {
    (void)w;
}

void emote_wheel_set_textures(EmoteWheel* w, unsigned int wheel_tex, unsigned int select_tex) {
    if (!w) return;
    if (wheel_tex) w->wheel_tex = wheel_tex;
    if (select_tex) w->select_tex = select_tex;
}

void emote_wheel_shutdown(EmoteWheel* w) {
    (void)w;
}

int emote_wheel_sector_at(float mouse_x, float mouse_y, int screen_w, int screen_h) {
    float cx = (float)screen_w * 0.5f;
    float cy = (float)screen_h * 0.5f;
    float dx = mouse_x - cx;
    float dy = mouse_y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    float radius = fminf((float)screen_w, (float)screen_h) * 0.36f;
    if (dist < radius * 0.06f || dist > radius * 1.08f) return -1;
    float ang = atan2f(dx, -dy);
    if (ang < 0.0f) ang += (float)(2.0 * M_PI);
    int sector = (int)(ang / (float)(M_PI / 4.0)) % 8;
    if (sector < 0) sector += 8;
    return sector;
}

bool emote_wheel_is_open(const EmoteWheel* w) {
    return w && w->open;
}

bool emote_wheel_update(EmoteWheel* w, bool hold_b, bool chat_or_menu_blocks,
                        float mouse_x, float mouse_y, int screen_w, int screen_h,
                        const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                        const uint8_t anim_bases[PW_MAX_EQUIPPED_EMOTES],
                        AnimState* out_anim,
                        uint32_t* out_emote_id) {
    if (!w) return false;
    if (out_anim) *out_anim = ANIM_STATE_IDLE;
    if (out_emote_id) *out_emote_id = 0;

    if (chat_or_menu_blocks) {
        w->open = false;
        w->was_held = false;
        return false;
    }

    if (hold_b) {
        w->open = true;
        int s = emote_wheel_sector_at(mouse_x, mouse_y, screen_w, screen_h);
        if (s >= 0) w->hover_sector = s;
        w->was_held = true;
        return false;
    }

    if (w->was_held && w->open) {
        w->was_held = false;
        w->open = false;
        int s = w->hover_sector;
        if (s < 0 || s >= PW_MAX_EQUIPPED_EMOTES || !slots || !out_anim) return false;
        uint32_t id = slots[s];
        if (id == 0) return false;
        uint8_t base = anim_bases ? anim_bases[s] : 0;
        AnimState anim = emote_base_to_anim(base);
        if (anim == ANIM_STATE_IDLE)
            anim = emote_id_to_anim(id);
        if (anim == ANIM_STATE_IDLE && !emote_id_is_builtin(id)) {
            anim = ANIM_STATE_EMOTE;
            if (out_emote_id) *out_emote_id = id;
        }
        if (anim == ANIM_STATE_IDLE) return false;
        *out_anim = anim;
        return true;
    }

    w->was_held = false;
    w->open = false;
    return false;
}

void emote_wheel_render(EmoteWheel* w, Chat* chat, int screen_w, int screen_h,
                        const uint32_t slots[PW_MAX_EQUIPPED_EMOTES],
                        const char names[PW_MAX_EQUIPPED_EMOTES][PW_EMOTE_NAME_LEN]) {
    if (!w || !w->open || !chat) return;
    float cx = (float)screen_w * 0.5f;
    float cy = (float)screen_h * 0.5f;
    float size = fminf((float)screen_w, (float)screen_h) * 0.72f;
    float x = cx - size * 0.5f;
    float y = cy - size * 0.5f;
    float radius = size * 0.5f;
    const float select_scale = 642.0f / 838.0f;
    float sel_r = radius * select_scale;

    if (w->wheel_tex) {
        chat_draw_tex_uv(chat, w->wheel_tex, x, y, size, size,
                         0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                         screen_w, screen_h);
    }

    if (w->select_tex && w->hover_sector >= 0 && w->hover_sector < 8) {
        float a0 = (float)w->hover_sector * (float)(M_PI / 4.0);
        float a1 = (float)(w->hover_sector + 1) * (float)(M_PI / 4.0);
        chat_draw_tex_wedge(chat, w->select_tex, cx, cy, sel_r, a0, a1,
                            1.0f, 1.0f, 1.0f, 1.0f, screen_w, screen_h);
    }

    float uis = chat->ui_scale > 0.1f ? chat->ui_scale : 1.0f;
    float label_scale = fmaxf(1.15f * uis, size / (48.0f * 8.0f));
    float label_scale_hot = label_scale * 1.12f;
    char label[48];
    for (int i = 0; i < PW_MAX_EQUIPPED_EMOTES; i++) {
        float ang = ((float)i + 0.5f) * (float)(M_PI / 4.0);
        float lx = cx + sinf(ang) * size * 0.32f;
        float ly = cy - cosf(ang) * size * 0.32f;
        uint32_t id = slots ? slots[i] : 0;
        if (names && names[i][0]) {
            snprintf(label, sizeof(label), "%s", names[i]);
        } else if (id == PW_EMOTE_DANCE1_ID) {
            snprintf(label, sizeof(label), "Dance 1");
        } else if (id == PW_EMOTE_DANCE2_ID) {
            snprintf(label, sizeof(label), "Dance 2");
        } else if (id == PW_EMOTE_DANCE3_ID) {
            snprintf(label, sizeof(label), "Dance 3");
        } else if (id == 0) {
            snprintf(label, sizeof(label), "Empty");
        } else {
            snprintf(label, sizeof(label), "#%u", id);
        }
        float scale = (i == w->hover_sector) ? label_scale_hot : label_scale;
        float tw = font_text_width_scaled(label, 8.0f * scale);
        chat_render_hud_text(chat, label, lx - tw * 0.5f, ly - 4.0f * scale,
                             scale, screen_w, screen_h);
    }
}

#endif
