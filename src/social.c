/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: social.c                                                                            |
|   Purpose: friend requests + player card                                                    |
\*-------------------------------------------------------------------------------------------*/

#include "social.h"
#include "font.h"
#include "platform.h"
#include "log.h"
#include "shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#else
#include <GL/glew.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif
#endif

extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern void stbi_image_free(void*);

#ifndef PW_SITE_HOST
#define PW_SITE_HOST "https://polyworld.games"
#endif

static void load_headshot_into(unsigned int* tex_out, bool* loaded_out, uint32_t user_id);

typedef struct {
    unsigned int* tex;
    bool* loaded;
} HeadshotSlot;

static void on_headshot_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    (void)path;
    HeadshotSlot* slot = (HeadshotSlot*)user;
    if (!slot || !data || len < 32) { free(slot); return; }
    int w, h, c;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    if (!px) { free(slot); return; }
    if (*slot->tex) glDeleteTextures(1, slot->tex);
    glGenTextures(1, slot->tex);
    glBindTexture(GL_TEXTURE_2D, *slot->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    *slot->loaded = true;
    stbi_image_free(px);
    free(slot);
}

static void load_headshot_into(unsigned int* tex_out, bool* loaded_out, uint32_t user_id) {
    if (!tex_out || !loaded_out || user_id == 0) return;
    *loaded_out = false;
    HeadshotSlot* slot = (HeadshotSlot*)malloc(sizeof(HeadshotSlot));
    if (!slot) return;
    slot->tex = tex_out;
    slot->loaded = loaded_out;
    char url[256];
#ifdef __EMSCRIPTEN__
    snprintf(url, sizeof(url), "/uploads/avatars/%u.png", user_id);
#else
    snprintf(url, sizeof(url), "%s/uploads/avatars/%u.png", PW_SITE_HOST, user_id);
#endif
    platform_load_file(url, on_headshot_loaded, slot);
}

void social_init(SocialUI* s) {
    memset(s, 0, sizeof(*s));
    s->ui_scale = 1.0f;
}

void social_set_nineslice(SocialUI* s, unsigned int tex) {
    if (s) s->nineslice_tex = tex;
}

void social_set_shaders(SocialUI* s, unsigned int quad_prog, int u_proj, int u_tex,
                        int u_alpha, int u_tint, unsigned int vao, unsigned int vbo) {
    if (!s) return;
    s->quad_shader = quad_prog;
    s->quad_u_projection = u_proj;
    s->quad_u_tex = u_tex;
    s->quad_u_alpha = u_alpha;
    s->quad_u_tint = u_tint;
    s->quad_vao = vao;
    s->quad_vbo = vbo;
}

void social_close_card(SocialUI* s) {
    if (!s) return;
    s->card_open = false;
    s->target_pid = 0;
    s->target_uid = 0;
    s->target_name[0] = '\0';
    s->status[0] = '\0';
    s->send_busy = false;
    s->friend_rel = SOCIAL_REL_UNKNOWN;
}

void social_open_card(SocialUI* s, const char* name, uint32_t pid, uint32_t account_id,
                      float screen_x, float screen_y) {
    if (!s || !name || !name[0]) return;
    social_close_card(s);
    s->card_open = true;
    s->target_pid = pid;
    s->target_uid = account_id;
    strncpy(s->target_name, name, SOCIAL_NAME_MAX - 1);
    s->card_x = screen_x;
    s->card_y = screen_y;
    s->friend_rel = (account_id == 0) ? SOCIAL_REL_NONE : SOCIAL_REL_UNKNOWN;
    if (account_id > 0)
        load_headshot_into(&s->headshot_tex, &s->headshot_loaded, account_id);
}

void social_set_friend_rel(SocialUI* s, uint32_t account_id, SocialFriendRel rel) {
    if (!s) return;
    if (s->card_open && s->target_uid == account_id) {
        s->friend_rel = rel;
        s->send_busy = false;
        s->status[0] = '\0';
    }
}

void social_push_toast(SocialUI* s, uint32_t from_id, const char* from_name) {
    if (!s || from_id == 0 || !from_name) return;

    if (s->card_open && s->target_uid == from_id)
        social_set_friend_rel(s, from_id, SOCIAL_REL_INCOMING);

    for (int i = 0; i < SOCIAL_MAX_TOASTS; i++) {
        if (s->toasts[i].active && s->toasts[i].from_id == from_id) {
            s->toasts[i].age = 0.0f;
            return;
        }
    }
    int slot = -1;
    for (int i = 0; i < SOCIAL_MAX_TOASTS; i++) {
        if (!s->toasts[i].active) { slot = i; break; }
    }
    if (slot < 0) {

        float oldest = -1.0f;
        for (int i = 0; i < SOCIAL_MAX_TOASTS; i++) {
            if (s->toasts[i].age > oldest) { oldest = s->toasts[i].age; slot = i; }
        }
    }
    if (slot < 0) return;
    SocialToast* t = &s->toasts[slot];
    memset(t, 0, sizeof(*t));
    t->active = true;
    t->from_id = from_id;
    strncpy(t->from_name, from_name, SOCIAL_NAME_MAX - 1);
    t->age = 0.0f;
    load_headshot_into(&t->headshot_tex, &t->headshot_loaded, from_id);
}

void social_update(SocialUI* s, float dt) {
    if (!s) return;
    for (int i = 0; i < SOCIAL_MAX_TOASTS; i++) {
        if (!s->toasts[i].active) continue;
        s->toasts[i].age += dt;

        if (s->toasts[i].age > 120.0f) {
            s->toasts[i].active = false;
        }
    }
}

static void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a, int sw, int sh) {
    static unsigned int prog = 0;
    if (!prog) {
        prog = shader_load_program("ui_color");
    }
    float nx0 = x / (float)sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)sh * 2.0f;
    float nx1 = (x + w) / (float)sw * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)sh * 2.0f;
    float verts[] = { nx0,ny0, nx1,ny0, nx1,ny1, nx0,ny0, nx1,ny1, nx0,ny1 };
    glUseProgram(prog);
    glUniform4f(glGetUniformLocation(prog, "u_color"), r, g, b, a);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

static void draw_nineslice(SocialUI* s, float x, float y, float w, float h,
                           float border, float alpha, int sw, int sh) {
    if (!s || !s->nineslice_tex || !s->quad_shader || !s->quad_vao) {
        draw_rect(x, y, w, h, 0.12f, 0.12f, 0.14f, alpha > 0.0f ? alpha : 0.95f, sw, sh);
        return;
    }
    if (w < border * 2.0f) w = border * 2.0f;
    if (h < border * 2.0f) h = border * 2.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s->quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)sw;
    proj[5] = -2.0f / (float)sh;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(s->quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(s->quad_u_alpha, alpha);
    glUniform4f(s->quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->nineslice_tex);
    glUniform1i(s->quad_u_tex, 0);

    const float uv_b = 50.0f / 550.0f;
    float sx[4] = { x, x + border, x + w - border, x + w };
    float sy[4] = { y, y + border, y + h - border, y + h };
    float su[4] = { 0.0f, uv_b, 1.0f - uv_b, 1.0f };
    float sv[4] = { 1.0f, 1.0f - uv_b, uv_b, 0.0f };

    float verts[9 * 6 * 4];
    int vi = 0;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            float x0 = sx[col], x1 = sx[col + 1];
            float y0 = sy[row], y1 = sy[row + 1];
            float u0 = su[col], u1 = su[col + 1];
            float v0 = sv[row], v1 = sv[row + 1];
            verts[vi++] = x0; verts[vi++] = y0; verts[vi++] = u0; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y0; verts[vi++] = u1; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y1; verts[vi++] = u1; verts[vi++] = v1;
            verts[vi++] = x0; verts[vi++] = y0; verts[vi++] = u0; verts[vi++] = v0;
            verts[vi++] = x1; verts[vi++] = y1; verts[vi++] = u1; verts[vi++] = v1;
            verts[vi++] = x0; verts[vi++] = y1; verts[vi++] = u0; verts[vi++] = v1;
        }
    }
    glBindVertexArray(s->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)vi * sizeof(float), verts);
    glDrawArrays(GL_TRIANGLES, 0, 9 * 6);
    glBindVertexArray(0);
}

static void draw_tex(unsigned int tex, float x, float y, float w, float h, int sw, int sh) {
    if (!tex) return;
    static unsigned int prog = 0;
    if (!prog) {
        prog = shader_load_program("ui_tex");
    }
    float nx0 = x / (float)sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)sh * 2.0f;
    float nx1 = (x + w) / (float)sw * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)sh * 2.0f;
    float verts[] = {
        nx0,ny0,0,0, nx1,ny0,1,0, nx1,ny1,1,1,
        nx0,ny0,0,0, nx1,ny1,1,1, nx0,ny1,0,1
    };
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

void social_render(SocialUI* s, int sw, int sh) {
    if (!s) return;
    float uis = s->ui_scale > 0.1f ? s->ui_scale : 1.0f;
    float border = 8.0f * uis;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    float toast_w = 260.0f * uis;
    float toast_h = 78.0f * uis;
    float gap = 8.0f * uis;
    float tx = (float)sw - toast_w - 16.0f * uis;
    float ty = (float)sh - 16.0f * uis;
    int drawn = 0;
    for (int i = SOCIAL_MAX_TOASTS - 1; i >= 0; i--) {
        if (!s->toasts[i].active) continue;
        SocialToast* t = &s->toasts[i];
        float y = ty - (float)(drawn + 1) * (toast_h + gap);
        draw_nineslice(s, tx, y, toast_w, toast_h, border, 1.0f, sw, sh);
        float hs = 48.0f * uis;
        if (t->headshot_loaded)
            draw_tex(t->headshot_tex, tx + 10.0f * uis, y + 14.0f * uis, hs, hs, sw, sh);
        else
            draw_rect(tx + 10.0f * uis, y + 14.0f * uis, hs, hs, 0.3f, 0.3f, 0.35f, 1.0f, sw, sh);
        font_draw_scaled(t->from_name, tx + 68.0f * uis, y + 12.0f * uis, 14.0f * uis,
                         1, 1, 1, 1, sw, sh);
        font_draw_scaled("Friend request", tx + 68.0f * uis, y + 30.0f * uis, 11.0f * uis,
                         0.75f, 0.75f, 0.75f, 1, sw, sh);
        float bw = 74.0f * uis, bh = 24.0f * uis;
        float by = y + toast_h - bh - 10.0f * uis;
        float ax = tx + 68.0f * uis;
        float dx = ax + bw + 6.0f * uis;
        draw_nineslice(s, ax, by, bw, bh, 5.0f * uis, 1.0f, sw, sh);
        font_draw_scaled("Accept", ax + 14.0f * uis, by + 5.0f * uis, 11.0f * uis, 0.45f, 0.95f, 0.50f, 1, sw, sh);
        draw_nineslice(s, dx, by, bw, bh, 5.0f * uis, 1.0f, sw, sh);
        font_draw_scaled("Decline", dx + 12.0f * uis, by + 5.0f * uis, 11.0f * uis, 0.95f, 0.45f, 0.45f, 1, sw, sh);
        drawn++;
    }

    if (s->card_open) {
        float cw = 220.0f * uis;
        float ch = 156.0f * uis;
        float cx = s->card_x;
        float cy = s->card_y;
        if (cx + cw > (float)sw - 8.0f) cx = (float)sw - cw - 8.0f;
        if (cy + ch > (float)sh - 8.0f) cy = (float)sh - ch - 8.0f;
        if (cx < 8.0f) cx = 8.0f;
        if (cy < 8.0f) cy = 8.0f;
        s->card_x = cx; s->card_y = cy;

        draw_nineslice(s, cx, cy, cw, ch, border, 1.0f, sw, sh);
        float hs = 48.0f * uis;
        if (s->headshot_loaded)
            draw_tex(s->headshot_tex, cx + 14.0f * uis, cy + 14.0f * uis, hs, hs, sw, sh);
        else
            draw_rect(cx + 14.0f * uis, cy + 14.0f * uis, hs, hs, 0.3f, 0.3f, 0.35f, 1.0f, sw, sh);
        font_draw_scaled(s->target_name, cx + 74.0f * uis, cy + 22.0f * uis, 16.0f * uis,
                         1, 1, 1, 1, sw, sh);

        float bw = cw - 28.0f * uis, bh = 30.0f * uis;
        float bx = cx + 14.0f * uis;
        float by1 = cy + 74.0f * uis;
        float by2 = by1 + bh + 8.0f * uis;
        draw_nineslice(s, bx, by1, bw, bh, 6.0f * uis, 1.0f, sw, sh);
        font_draw_scaled("View Profile", bx + 42.0f * uis, by1 + 7.0f * uis, 13.0f * uis, 1, 1, 1, 1, sw, sh);
        draw_nineslice(s, bx, by2, bw, bh, 6.0f * uis, 1.0f, sw, sh);
        {
            const char* label = "Send Friend Request";
            float lr = 0.45f, lg = 0.95f, lb = 0.50f;
            float lx = bx + 20.0f * uis;

            if (s->target_uid == 0 ||
                (s->target_name[0] && strncmp(s->target_name, "Guest", 5) == 0)) {
                label = "Guest";
                lr = lg = lb = 0.55f;
                lx = bx + 78.0f * uis;
            } else if (s->send_busy || s->friend_rel == SOCIAL_REL_UNKNOWN) {
                label = "...";
                lr = lg = lb = 0.7f;
                lx = bx + 92.0f * uis;
            } else if (s->friend_rel == SOCIAL_REL_FRIENDS) {
                label = "Unfriend";
                lr = 0.95f; lg = 0.45f; lb = 0.45f;
                lx = bx + 68.0f * uis;
            } else if (s->friend_rel == SOCIAL_REL_OUTGOING) {
                label = "Pending Request";
                lr = lg = lb = 0.7f;
                lx = bx + 36.0f * uis;
            } else if (s->friend_rel == SOCIAL_REL_INCOMING) {
                label = "Accept Request";
                lr = 0.45f; lg = 0.95f; lb = 0.50f;
                lx = bx + 40.0f * uis;
            }
            font_draw_scaled(label, lx, by2 + 7.0f * uis, 13.0f * uis, lr, lg, lb, 1, sw, sh);
        }
        if (s->status[0])
            font_draw_scaled(s->status, bx, cy + ch - 20.0f * uis, 11.0f * uis, 0.85f, 0.85f, 0.55f, 1, sw, sh);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

bool social_on_click(SocialUI* s, float x, float y, int sw, int sh,
                     int* out_action, uint32_t* out_uid, char* out_name, size_t name_cap) {
    if (!s || !out_action) return false;
    *out_action = 0;
    if (out_uid) *out_uid = 0;
    float uis = s->ui_scale > 0.1f ? s->ui_scale : 1.0f;

    float toast_w = 260.0f * uis;
    float toast_h = 78.0f * uis;
    float gap = 8.0f * uis;
    float tx = (float)sw - toast_w - 16.0f * uis;
    float ty = (float)sh - 16.0f * uis;
    int drawn = 0;
    for (int i = SOCIAL_MAX_TOASTS - 1; i >= 0; i--) {
        if (!s->toasts[i].active) continue;
        float yy = ty - (float)(drawn + 1) * (toast_h + gap);
        float bw = 74.0f * uis, bh = 24.0f * uis;
        float by = yy + toast_h - bh - 10.0f * uis;
        float ax0 = tx + 68.0f * uis, ax1 = ax0 + bw;
        float dx0 = ax1 + 6.0f * uis, dx1 = dx0 + bw;
        if (y >= by && y <= by + bh) {
            if (x >= ax0 && x <= ax1) {
                *out_action = 3;
                if (out_uid) *out_uid = s->toasts[i].from_id;
                if (out_name && name_cap) {
                    strncpy(out_name, s->toasts[i].from_name, name_cap - 1);
                    out_name[name_cap - 1] = '\0';
                }
                s->toasts[i].active = false;
                return true;
            }
            if (x >= dx0 && x <= dx1) {
                *out_action = 4;
                if (out_uid) *out_uid = s->toasts[i].from_id;
                s->toasts[i].active = false;
                return true;
            }
        }

        if (x >= tx && x <= tx + toast_w && y >= yy && y <= yy + toast_h) {
            *out_action = 1;
            if (out_uid) *out_uid = s->toasts[i].from_id;
            if (out_name && name_cap) {
                strncpy(out_name, s->toasts[i].from_name, name_cap - 1);
                out_name[name_cap - 1] = '\0';
            }
            return true;
        }
        drawn++;
    }

    if (s->card_open) {
        float cw = 220.0f * uis, ch = 156.0f * uis;
        float cx = s->card_x, cy = s->card_y;
        float bw = cw - 28.0f * uis, bh = 30.0f * uis;
        float bx = cx + 14.0f * uis;
        float by1 = cy + 74.0f * uis;
        float by2 = by1 + bh + 8.0f * uis;
        if (x >= bx && x <= bx + bw && y >= by1 && y <= by1 + bh) {
            *out_action = 1;
            if (out_uid) *out_uid = s->target_uid;
            if (out_name && name_cap) {
                strncpy(out_name, s->target_name, name_cap - 1);
                out_name[name_cap - 1] = '\0';
            }
            return true;
        }
        if (x >= bx && x <= bx + bw && y >= by2 && y <= by2 + bh) {
            if (out_uid) *out_uid = s->target_uid;
            if (out_name && name_cap) {
                strncpy(out_name, s->target_name, name_cap - 1);
                out_name[name_cap - 1] = '\0';
            }
            if (s->target_uid == 0) {
                snprintf(s->status, sizeof(s->status), "Guests can't be friended");
                return true;
            }
            if (s->send_busy || s->friend_rel == SOCIAL_REL_UNKNOWN)
                return true;
            if (s->friend_rel == SOCIAL_REL_FRIENDS) {
                *out_action = 5;
                s->send_busy = true;
            } else if (s->friend_rel == SOCIAL_REL_OUTGOING) {
                *out_action = 6;
                s->send_busy = true;
            } else if (s->friend_rel == SOCIAL_REL_INCOMING) {
                *out_action = 3;
                s->send_busy = true;
            } else {
                *out_action = 2;
                s->send_busy = true;
                snprintf(s->status, sizeof(s->status), "Sending...");
            }
            return true;
        }

        if (!(x >= cx && x <= cx + cw && y >= cy && y <= cy + ch)) {
            social_close_card(s);
            return true;
        }
        return true;
    }
    return false;
}

void social_open_profile(uint32_t user_id, const char* username) {
    char url[320];
    if (user_id > 0) {
#ifdef __EMSCRIPTEN__
        snprintf(url, sizeof(url), "/profile/%u/", user_id);
#else
        snprintf(url, sizeof(url), "%s/profile/%u/", PW_SITE_HOST, user_id);
#endif
    } else if (username && username[0]) {
#ifdef __EMSCRIPTEN__
        snprintf(url, sizeof(url), "/profile/");
#else
        snprintf(url, sizeof(url), "%s/profile/", PW_SITE_HOST);
#endif
    } else {
        return;
    }

#ifdef __EMSCRIPTEN__
    EM_ASM({
        var u = UTF8ToString($0);
        window.open(u, '_blank');
    }, url);
#elif defined(__ANDROID__)
    platform_open_url(url);
#elif defined(_WIN32)
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    system(cmd);
#endif
    (void)username;
}
