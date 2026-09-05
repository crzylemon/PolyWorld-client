/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: game_menu.c                                                                         |
|   Purpose: pause menu                                                                       |
\*-------------------------------------------------------------------------------------------*/

#include "game_menu.h"
#include "audio.h"
#include "font.h"
#include "platform.h"
#include "input.h"
#include "ui_theme.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#else
#include <GL/glew.h>
#endif

static unsigned int s_quad_shader;
static int s_quad_u_projection;
static int s_quad_u_tex;
static int s_quad_u_alpha;
static int s_quad_u_tint;

static unsigned int s_text_shader;
static int s_text_u_projection;
static int s_text_u_tex;
static int s_text_u_color;
static unsigned int s_font_texture;
static unsigned int s_text_vao;
static unsigned int s_text_vbo;

void game_menu_set_shaders(unsigned int quad_prog, int qp, int qt, int qa, int qtint,
                           unsigned int text_prog, int tp, int tt, int tc,
                           unsigned int font_tex, unsigned int vao, unsigned int vbo) {
    s_quad_shader = quad_prog;
    s_quad_u_projection = qp;
    s_quad_u_tex = qt;
    s_quad_u_alpha = qa;
    s_quad_u_tint = qtint;
    s_text_shader = text_prog;
    s_text_u_projection = tp;
    s_text_u_tex = tt;
    s_text_u_color = tc;
    s_font_texture = font_tex;
    s_text_vao = vao;
    s_text_vbo = vbo;
}

static void menu_draw_nineslice(unsigned int tex, float x, float y, float w, float h,
                                float border, float alpha, int sw, int sh) {
    if (!tex) return;
    if (w < border * 2.0f) w = border * 2.0f;
    if (h < border * 2.0f) h = border * 2.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)sw;
    proj[5] = -2.0f / (float)sh;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(s_quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(s_quad_u_alpha, alpha);
    if (ui_theme_is_dark())
        glUniform4f(s_quad_u_tint, 0.22f, 0.24f, 0.18f, 1.0f);
    else
        glUniform4f(s_quad_u_tint, 1.0f, 1.0f, 1.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(s_quad_u_tex, 0);

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
    glBindVertexArray(s_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)vi * sizeof(float), verts);
    glDrawArrays(GL_TRIANGLES, 0, 9 * 6);
    glBindVertexArray(0);
}

static void menu_draw_text(const char* text, float x, float y, float scale,
                           float r, float g, float b, float a, int sw, int sh) {
    if (!text || !text[0]) return;
    float pixel_h = 8.0f * scale;
    font_draw_scaled(text, x, y, pixel_h, r, g, b, a, sw, sh);
}

static void menu_draw_overlay(float alpha, int sw, int sh) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_quad_shader);
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)sw;
    proj[5] = -2.0f / (float)sh;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(s_quad_u_projection, 1, GL_FALSE, proj);
    glUniform1f(s_quad_u_alpha, alpha);
    glUniform4f(s_quad_u_tint, 0.0f, 0.0f, 0.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font_texture);
    glUniform1i(s_quad_u_tex, 0);

    float verts[] = {
        0,        0,        0.5f, 0.5f,
        (float)sw, 0,       0.5f, 0.5f,
        (float)sw, (float)sh, 0.5f, 0.5f,
        0,        0,        0.5f, 0.5f,
        (float)sw, (float)sh, 0.5f, 0.5f,
        0,        (float)sh, 0.5f, 0.5f,
    };
    glBindVertexArray(s_text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_text_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

typedef struct {
    float x, y, w, h;
    const char* label;
    int action;
} MenuButton;

enum {
    SET_ACT_QUALITY = 0,
    SET_ACT_BENCHMARK,
    SET_ACT_LIGHTING,
    SET_ACT_FOG,
    SET_ACT_RENDER_SCALE,
    SET_ACT_GLOW_LEAK,
    SET_ACT_UI_SCALE,
    SET_ACT_FPS,
    SET_ACT_FULLSCREEN,
    SET_ACT_FORCE_MOBILE,
    SET_ACT_BACK,
    SET_ACT_HUB_GRAPHICS,
    SET_ACT_HUB_AUDIO,
    SET_ACT_HUB_VR,
    SET_ACT_VR_TURN,
    SET_ACT_VR_VIGNETTE,
    SET_ACT_DARK_MODE,
    SET_ACT_VOL_MASTER,
    SET_ACT_VOL_MUSIC,
    SET_ACT_VOL_SFX
};

static void seed_manual_from_preset(GameMenu* m, GfxQuality preset) {
    GfxQuality q = preset;
    if (q == GFX_QUALITY_AUTO) {
#if defined(__ANDROID__)
        q = platform_is_chromebook() ? GFX_QUALITY_LOW : GFX_QUALITY_POTATO;
#elif defined(PW_IOS)
        q = GFX_QUALITY_POTATO;
#elif defined(__EMSCRIPTEN__)
        q = GFX_QUALITY_MEDIUM;
#else
        q = GFX_QUALITY_HIGH;
#endif
    }
    m->manual_fog = (q >= GFX_QUALITY_LOW);
    if (q <= GFX_QUALITY_POTATO) m->manual_render_scale = PW_MOBILE ? 0.38f : 0.42f;
    else if (q <= GFX_QUALITY_LOW) m->manual_render_scale = 0.65f;
    else if (q <= GFX_QUALITY_MEDIUM) m->manual_render_scale = 0.75f;
    else m->manual_render_scale = 1.0f;
    if (q >= GFX_QUALITY_LOW) m->manual_glow_leak = GFX_GLOW_LEAK_DISTANCE;
    else m->manual_glow_leak = GFX_GLOW_LEAK_NONE;
}

static void get_main_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    (void)sw;
    float panel_w = 280.0f * uis;
    float btn_h = 44.0f * uis;
    float btn_gap = 10.0f * uis;
    float panel_x = ((float)sw - panel_w) * 0.5f;

    float start_y = (float)sh * 0.28f - m->menu_scroll_y;

    int n = 0;
    out[n++] = (MenuButton){ panel_x, start_y + 0 * (btn_h + btn_gap), panel_w, btn_h, "Resume", -1 };
    out[n++] = (MenuButton){ panel_x, start_y + 1 * (btn_h + btn_gap), panel_w, btn_h,
                             m->reset_enabled ? "Reset Character" : "Reset Disabled", -1 };
    if (!m->studio_playtest) {
        float y = start_y + (float)n * (btn_h + btn_gap);
        out[n++] = (MenuButton){ panel_x, y, panel_w, btn_h, "Avatar Editor", -1 };
        y = start_y + (float)n * (btn_h + btn_gap);
        out[n++] = (MenuButton){ panel_x, y, panel_w, btn_h, "Catalog", -1 };
    }
    {
        float y = start_y + (float)n * (btn_h + btn_gap);
        out[n++] = (MenuButton){ panel_x, y, panel_w, btn_h, "Settings", -1 };
    }
    {
        float y = start_y + (float)n * (btn_h + btn_gap);
        out[n++] = (MenuButton){ panel_x, y, panel_w, btn_h,
                                 m->studio_playtest ? "Stop Playtest" : "Leave Game", -1 };
    }
    *count = n;

    float content_bottom = start_y + (float)n * (btn_h + btn_gap) + m->menu_scroll_y;
    float max_scroll = content_bottom - (float)sh + 24.0f * uis;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    m->menu_scroll_max = max_scroll;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
}

static void get_settings_hub_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    (void)sw;
    float panel_w = 300.0f * uis;
    float btn_h = 44.0f * uis;
    float btn_gap = 10.0f * uis;
    float panel_x = ((float)sw - panel_w) * 0.5f;
    float start_y = (float)sh * 0.32f - m->menu_scroll_y;

    int n = 0;
    int row = 0;
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             "Graphics", SET_ACT_HUB_GRAPHICS };
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             "Audio", SET_ACT_HUB_AUDIO };
#if defined(VR)
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             "VR Comfort", SET_ACT_HUB_VR };
#endif
    {
        static char s_dark_label[64];
        snprintf(s_dark_label, sizeof(s_dark_label), "Dark Mode: %s", m->dark_mode ? "On" : "Off");
        out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                 s_dark_label, SET_ACT_DARK_MODE };
    }
    row++;
    out[n++] = (MenuButton){ panel_x, start_y + row * (btn_h + btn_gap), panel_w, btn_h,
                             "Back", SET_ACT_BACK };
    *count = n;

    float content_bottom = start_y + (float)(row + 1) * (btn_h + btn_gap) + m->menu_scroll_y;
    float max_scroll = content_bottom - (float)sh + 24.0f * uis;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    m->menu_scroll_max = max_scroll;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
}

static float cycle_volume(float v) {
    int step = (int)(v * 10.0f + 0.5f);
    if (step < 0) step = 0;
    if (step > 10) step = 10;
    step = (step + 1) % 11;
    return (float)step / 10.0f;
}

static void apply_menu_volumes(GameMenu* m) {
    audio_set_volume_levels(m->vol_master, m->vol_music, m->vol_sfx);
}

static void get_audio_settings_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    (void)sw;
    float panel_w = 320.0f * uis;
    float btn_h = 44.0f * uis;
    float btn_gap = 10.0f * uis;
    float panel_x = ((float)sw - panel_w) * 0.5f;
    float start_y = (float)sh * 0.30f - m->menu_scroll_y;

    int n = 0;
    int row = 0;
    static char s_master[64], s_music[64], s_sfx[64];
    snprintf(s_master, sizeof(s_master), "Master: %.0f%%", m->vol_master * 100.0f);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_master, SET_ACT_VOL_MASTER };
    snprintf(s_music, sizeof(s_music), "Music: %.0f%%", m->vol_music * 100.0f);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_music, SET_ACT_VOL_MUSIC };
    snprintf(s_sfx, sizeof(s_sfx), "SFX: %.0f%%", m->vol_sfx * 100.0f);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_sfx, SET_ACT_VOL_SFX };
    row++;
    out[n++] = (MenuButton){ panel_x, start_y + row * (btn_h + btn_gap), panel_w, btn_h,
                             "Back", SET_ACT_BACK };
    *count = n;

    float content_bottom = start_y + (float)(row + 1) * (btn_h + btn_gap) + m->menu_scroll_y;
    float max_scroll = content_bottom - (float)sh + 24.0f * uis;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    m->menu_scroll_max = max_scroll;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
}

#if defined(VR)
static void get_vr_settings_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    (void)sw;
    float panel_w = 340.0f * uis;
    float btn_h = 44.0f * uis;
    float btn_gap = 10.0f * uis;
    float panel_x = ((float)sw - panel_w) * 0.5f;
    float start_y = (float)sh * 0.30f - m->menu_scroll_y;

    int n = 0;
    int row = 0;
    static char s_turn[72], s_vig[72];
    const char* turn_names[] = { "Off", "Snap", "Smooth" };
    int tm = m->vr_turn;
    if (tm < 0) tm = 0;
    if (tm > 2) tm = 2;
    snprintf(s_turn, sizeof(s_turn), "Seated turn: %s", turn_names[tm]);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_turn, SET_ACT_VR_TURN };

    const char* vig_names[] = { "Off", "Low", "Medium", "High" };
    int vg = m->vr_vignette;
    if (vg < 0) vg = 0;
    if (vg > 3) vg = 3;
    snprintf(s_vig, sizeof(s_vig), "Comfort vignette: %s", vig_names[vg]);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_vig, SET_ACT_VR_VIGNETTE };

    row++;
    out[n++] = (MenuButton){ panel_x, start_y + row * (btn_h + btn_gap), panel_w, btn_h,
                             "Back", SET_ACT_BACK };
    *count = n;

    float content_bottom = start_y + (float)(row + 1) * (btn_h + btn_gap) + m->menu_scroll_y;
    float max_scroll = content_bottom - (float)sh + 24.0f * uis;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    m->menu_scroll_max = max_scroll;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
}
#endif

static void get_settings_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    (void)sw;
    float panel_w = 320.0f * uis;
    float btn_h = 44.0f * uis;
    float btn_gap = 10.0f * uis;
    float panel_x = ((float)sw - panel_w) * 0.5f;
    float start_y = ((m->quality == GFX_QUALITY_MANUAL) ? (float)sh * 0.14f : (float)sh * 0.30f)
                    - m->menu_scroll_y;

    int n = 0;
    int row = 0;
    const char* quality_names[] = {
        "Potato", "Low", "Medium", "High", "Ultra", "Super", "Auto", "Manual"
    };
    static char s_quality_label[64];
    snprintf(s_quality_label, sizeof(s_quality_label), "Graphics: %s",
             quality_names[(int)m->quality >= 0 && (int)m->quality <= 7 ? (int)m->quality : 6]);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_quality_label, SET_ACT_QUALITY };

    static char s_bench_label[64];
    if (m->benchmark_running) {
        snprintf(s_bench_label, sizeof(s_bench_label), "Benchmarking...");
    } else if (m->benchmark_label[0]) {
        snprintf(s_bench_label, sizeof(s_bench_label), "%s", m->benchmark_label);
    } else {
        snprintf(s_bench_label, sizeof(s_bench_label), "Run Graphics Benchmark");
    }
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_bench_label, SET_ACT_BENCHMARK };

    static char s_lighting_label[64];
    snprintf(s_lighting_label, sizeof(s_lighting_label), "Lighting: %s",
             m->lighting_tech == GFX_LIGHTING_VOXEL ? "Voxel" : "Shadow Map");
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_lighting_label, SET_ACT_LIGHTING };

    if (m->quality == GFX_QUALITY_MANUAL) {
        static char s_fog[64], s_rs[64], s_glow[72];
        snprintf(s_fog, sizeof(s_fog), "Fog: %s", m->manual_fog ? "On" : "Off");
        out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                 s_fog, SET_ACT_FOG };

        {
            float rs = m->manual_render_scale;
            if (rs < 0.4f) rs = 0.4f;
            if (rs > 1.0f) rs = 1.0f;
            snprintf(s_rs, sizeof(s_rs), "Render scale: %.0f%%", rs * 100.0f);
            out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                     s_rs, SET_ACT_RENDER_SCALE };
        }

        {
            float rs = m->manual_render_scale;
            if (rs < 0.4f) rs = 0.4f;
            if (rs > 1.0f) rs = 1.0f;
            snprintf(s_rs, sizeof(s_rs), "Render scale: %.0f%%", rs * 100.0f);
            out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                     s_rs, SET_ACT_RENDER_SCALE };
        }

        const char* glow_names[] = { "None", "Distance" };
        int gi = (int)m->manual_glow_leak;
        if (gi < 0) gi = 0;
        if (gi > 1) gi = 1;
        snprintf(s_glow, sizeof(s_glow), "Glow Leak: %s", glow_names[gi]);
        out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                 s_glow, SET_ACT_GLOW_LEAK };
    }

    static char s_scale_label[64];
    snprintf(s_scale_label, sizeof(s_scale_label), "UI Scale: %.0f%%", m->ui_scale * 100.0f);
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_scale_label, SET_ACT_UI_SCALE };

    static char s_fps_label[64];
    if (m->fps_limit == 0) {
        snprintf(s_fps_label, sizeof(s_fps_label), "FPS Limit: Auto");
    } else {
        snprintf(s_fps_label, sizeof(s_fps_label), "FPS Limit: %d", m->fps_limit);
    }
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_fps_label, SET_ACT_FPS };

    static char s_fs_label[64];
    snprintf(s_fs_label, sizeof(s_fs_label), "Fullscreen: %s", m->fullscreen ? "On" : "Off");
    out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                             s_fs_label, SET_ACT_FULLSCREEN };

    bool show_touch_toggle = true;
#ifdef __ANDROID__
    show_touch_toggle = platform_is_chromebook();
#endif
    if (show_touch_toggle) {
        static char s_mobile_label[64];
        snprintf(s_mobile_label, sizeof(s_mobile_label), "Touch Controls: %s",
                 m->force_mobile_controls ? "On" : "Off");
        out[n++] = (MenuButton){ panel_x, start_y + row++ * (btn_h + btn_gap), panel_w, btn_h,
                                 s_mobile_label, SET_ACT_FORCE_MOBILE };
    }

    row++;
    out[n++] = (MenuButton){ panel_x, start_y + row * (btn_h + btn_gap), panel_w, btn_h,
                             "Back", SET_ACT_BACK };
    *count = n;

    float content_bottom = start_y + (float)(row + 1) * (btn_h + btn_gap) + m->menu_scroll_y;
    float max_scroll = content_bottom - (float)sh + 24.0f * uis;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    m->menu_scroll_max = max_scroll;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
}

static void menu_fill_page_buttons(GameMenu* m, float uis, int sw, int sh, MenuButton* out, int* count) {
    if (m->page == MENU_PAGE_MAIN)
        get_main_buttons(m, uis, sw, sh, out, count);
    else if (m->page == MENU_PAGE_SETTINGS_HUB)
        get_settings_hub_buttons(m, uis, sw, sh, out, count);
    else if (m->page == MENU_PAGE_SETTINGS_AUDIO)
        get_audio_settings_buttons(m, uis, sw, sh, out, count);
#if defined(VR)
    else if (m->page == MENU_PAGE_SETTINGS_VR)
        get_vr_settings_buttons(m, uis, sw, sh, out, count);
#endif
    else
        get_settings_buttons(m, uis, sw, sh, out, count);
}

static void menu_clip_rect(GameMenu* m, float uis, int sw, int sh,
                           float* out_top, float* out_bottom) {
    (void)sw;
    float title_scale = 3.0f * uis;
    float title_y = (m->page == MENU_PAGE_SETTINGS && m->quality == GFX_QUALITY_MANUAL)
                        ? (float)sh * 0.08f : (float)sh * 0.22f;
    float top = title_y + 8.0f * title_scale + 8.0f * uis;
    float bottom = (float)sh - 28.0f * uis;
    if (bottom < top + 40.0f * uis) bottom = top + 40.0f * uis;
    if (out_top) *out_top = top;
    if (out_bottom) *out_bottom = bottom;
}

static bool menu_btn_in_clip(const MenuButton* btn, float clip_top, float clip_bottom) {
    return btn->y + btn->h > clip_top && btn->y < clip_bottom;
}

static GameMenu* s_menu_persist;

void game_menu_init(GameMenu* m, float ui_scale) {
    memset(m, 0, sizeof(GameMenu));
    s_menu_persist = m;
    m->ui_scale = ui_scale;
    m->quality = GFX_QUALITY_AUTO;
    m->hovered_item = -1;
    m->reset_enabled = true;
    m->fullscreen = false;
    m->manual_fog = true;
    m->manual_render_scale = 1.0f;
    m->manual_glow_leak = GFX_GLOW_LEAK_DISTANCE;
    m->vol_master = 1.0f;
    m->vol_music = 1.0f;
    m->vol_sfx = 1.0f;
#if defined(VR)
    m->vr_turn = 1;
    m->vr_vignette = 2;
#endif
#if defined(__ANDROID__)
    m->fps_limit = 0;
#endif
}

void game_menu_toggle(GameMenu* m) {
    m->open = !m->open;
    m->page = MENU_PAGE_MAIN;
    m->hovered_item = -1;
    m->menu_scroll_y = 0.0f;
    m->menu_drag_active = false;
    m->menu_drag_moved = false;

    platform_set_cursor_captured(false);
}

void game_menu_set_dark_mode(bool dark) {
    ui_theme_set_dark(dark);
    if (!s_menu_persist) return;
    s_menu_persist->dark_mode = dark;
    game_menu_save_settings(s_menu_persist);
}

MenuAction game_menu_on_click(GameMenu* m, float x, float y, int screen_width, int screen_height) {
    if (!m->open) return MENU_ACTION_NONE;

    float uis = m->ui_scale > 0.1f ? m->ui_scale : 1.0f;
    float clip_top = 0.0f, clip_bottom = (float)screen_height;
    menu_clip_rect(m, uis, screen_width, screen_height, &clip_top, &clip_bottom);
    if (y < clip_top || y > clip_bottom) return MENU_ACTION_NONE;

    MenuButton buttons[20];
    int count = 0;

    if (m->page == MENU_PAGE_MAIN) {
        get_main_buttons(m, uis, screen_width, screen_height, buttons, &count);
        for (int i = 0; i < count; i++) {
            if (!menu_btn_in_clip(&buttons[i], clip_top, clip_bottom)) continue;
            if (x >= buttons[i].x && x <= buttons[i].x + buttons[i].w &&
                y >= buttons[i].y && y <= buttons[i].y + buttons[i].h) {
                const char* label = buttons[i].label ? buttons[i].label : "";
                if (strcmp(label, "Resume") == 0) {
                    m->open = false;
                    return MENU_ACTION_RESUME;
                }
                if (strncmp(label, "Reset", 5) == 0) {
                    if (!m->reset_enabled) {
                        return MENU_ACTION_RESPAWN;
                    }
                    m->open = false;
                    return MENU_ACTION_RESPAWN;
                }
                if (strcmp(label, "Avatar Editor") == 0) {
                    return MENU_ACTION_AVATAR_EDITOR;
                }
                if (strcmp(label, "Catalog") == 0) {
                    return MENU_ACTION_CATALOG;
                }
                if (strcmp(label, "Settings") == 0) {
                    m->page = MENU_PAGE_SETTINGS_HUB;
                    m->hovered_item = -1;
                    m->menu_scroll_y = 0.0f;
                    return MENU_ACTION_NONE;
                }
                if (strcmp(label, "Leave Game") == 0 || strcmp(label, "Stop Playtest") == 0) {
                    m->open = false;
                    return MENU_ACTION_LEAVE_GAME;
                }
            }
        }
    } else if (m->page == MENU_PAGE_SETTINGS_HUB ||
               m->page == MENU_PAGE_SETTINGS ||
               m->page == MENU_PAGE_SETTINGS_AUDIO) {
        menu_fill_page_buttons(m, uis, screen_width, screen_height, buttons, &count);
        for (int i = 0; i < count; i++) {
            if (!menu_btn_in_clip(&buttons[i], clip_top, clip_bottom)) continue;
            if (x < buttons[i].x || x > buttons[i].x + buttons[i].w ||
                y < buttons[i].y || y > buttons[i].y + buttons[i].h)
                continue;
            switch (buttons[i].action) {
                case SET_ACT_HUB_GRAPHICS:
                    m->page = MENU_PAGE_SETTINGS;
                    m->hovered_item = -1;
                    m->menu_scroll_y = 0.0f;
                    return MENU_ACTION_NONE;
                case SET_ACT_HUB_AUDIO:
                    m->page = MENU_PAGE_SETTINGS_AUDIO;
                    m->hovered_item = -1;
                    m->menu_scroll_y = 0.0f;
                    return MENU_ACTION_NONE;
#if defined(VR)
                case SET_ACT_HUB_VR:
                    m->page = MENU_PAGE_SETTINGS_VR;
                    m->hovered_item = -1;
                    m->menu_scroll_y = 0.0f;
                    return MENU_ACTION_NONE;
                case SET_ACT_VR_TURN:
                    m->vr_turn = (m->vr_turn + 1) % 3;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_VR_VIGNETTE:
                    m->vr_vignette = (m->vr_vignette + 1) % 4;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
#endif
                case SET_ACT_DARK_MODE:
                    game_menu_set_dark_mode(!m->dark_mode);
                    return MENU_ACTION_NONE;
                case SET_ACT_VOL_MASTER:
                    m->vol_master = cycle_volume(m->vol_master);
                    apply_menu_volumes(m);
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_VOL_MUSIC:
                    m->vol_music = cycle_volume(m->vol_music);
                    apply_menu_volumes(m);
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_VOL_SFX:
                    m->vol_sfx = cycle_volume(m->vol_sfx);
                    apply_menu_volumes(m);
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_QUALITY: {
                    GfxQuality prev = m->quality;
                    m->quality = (GfxQuality)(((int)m->quality + 1) % 8);
                    if (m->quality == GFX_QUALITY_MANUAL)
                        seed_manual_from_preset(m, prev == GFX_QUALITY_MANUAL ? GFX_QUALITY_HIGH : prev);
                    m->benchmark_label[0] = '\0';
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                }
                case SET_ACT_LIGHTING:
                    m->lighting_tech = (m->lighting_tech == GFX_LIGHTING_VOXEL)
                        ? GFX_LIGHTING_SHADOWMAP : GFX_LIGHTING_VOXEL;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_BENCHMARK:
                    if (!m->benchmark_running) {
                        game_menu_start_benchmark(m, false);
                        return MENU_ACTION_BENCHMARK;
                    }
                    return MENU_ACTION_NONE;
                case SET_ACT_FOG:
                    m->manual_fog = !m->manual_fog;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_RENDER_SCALE: {
                    float rs = m->manual_render_scale;
                    if (rs < 0.55f) rs = 0.65f;
                    else if (rs < 0.70f) rs = 0.75f;
                    else if (rs < 0.80f) rs = 0.85f;
                    else if (rs < 0.90f) rs = 1.00f;
                    else if (rs < 1.05f) rs = 0.50f;
                    else rs = 0.50f;
                    m->manual_render_scale = rs;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                }
                case SET_ACT_GLOW_LEAK:
                    m->manual_glow_leak = (GfxGlowLeakMode)(((int)m->manual_glow_leak + 1) % 2);
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_UI_SCALE:
                    if (m->ui_scale < 0.9f) m->ui_scale = 1.0f;
                    else if (m->ui_scale < 1.1f) m->ui_scale = 1.25f;
                    else if (m->ui_scale < 1.3f) m->ui_scale = 1.5f;
                    else if (m->ui_scale < 1.6f) m->ui_scale = 2.0f;
                    else if (m->ui_scale < 2.1f) m->ui_scale = 2.5f;
                    else if (m->ui_scale < 2.6f) m->ui_scale = 3.0f;
                    else if (m->ui_scale < 3.1f) m->ui_scale = 3.5f;
                    else if (m->ui_scale < 3.6f) m->ui_scale = 4.0f;
                    else m->ui_scale = 0.75f;
                    m->menu_scroll_y = 0.0f;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_FPS:
                    if (m->fps_limit == 0) m->fps_limit = 30;
                    else if (m->fps_limit == 30) m->fps_limit = 60;
                    else if (m->fps_limit == 60) m->fps_limit = 144;
                    else if (m->fps_limit == 144) m->fps_limit = 180;
                    else if (m->fps_limit == 180) m->fps_limit = 240;
                    else m->fps_limit = 0;
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_FULLSCREEN:
                    m->fullscreen = !m->fullscreen;
                    platform_set_fullscreen(m->fullscreen);
                    m->fullscreen = platform_is_fullscreen();
                    game_menu_save_settings(m);
                    return MENU_ACTION_NONE;
                case SET_ACT_FORCE_MOBILE:
                    m->force_mobile_controls = !m->force_mobile_controls;
                    game_menu_save_settings(m);
#ifdef __EMSCRIPTEN__
                    EM_ASM({
                        var el = document.getElementById('touch-controls');
                        if (!el) return;
                        var force = ($0 === 1);
                        var isMobile = ('ontouchstart' in window) || (navigator.maxTouchPoints > 0);
                        if (force || isMobile) el.classList.add('visible');
                        else el.classList.remove('visible');
                        try {
                            if (Module && Module._set_mobile_mode)
                                Module._set_mobile_mode((force || isMobile) ? 1 : 0);
                        } catch (e) {}
                    }, m->force_mobile_controls ? 1 : 0);
#endif
                    return MENU_ACTION_NONE;
                case SET_ACT_BACK:
                    if (m->page == MENU_PAGE_SETTINGS_HUB)
                        m->page = MENU_PAGE_MAIN;
                    else
                        m->page = MENU_PAGE_SETTINGS_HUB;
                    m->hovered_item = -1;
                    m->menu_scroll_y = 0.0f;
                    return MENU_ACTION_NONE;
            }
        }
    }

    return MENU_ACTION_NONE;
}

void game_menu_on_mouse_move(GameMenu* m, float x, float y, int screen_width, int screen_height) {
    if (!m->open) { m->hovered_item = -1; return; }

    float uis = m->ui_scale > 0.1f ? m->ui_scale : 1.0f;
    float clip_top = 0.0f, clip_bottom = (float)screen_height;
    menu_clip_rect(m, uis, screen_width, screen_height, &clip_top, &clip_bottom);

    MenuButton buttons[20];
    int count = 0;

    if (m->page == MENU_PAGE_MAIN) {
        get_main_buttons(m, uis, screen_width, screen_height, buttons, &count);
    } else {
        menu_fill_page_buttons(m, uis, screen_width, screen_height, buttons, &count);
    }

    m->hovered_item = -1;
    if (y < clip_top || y > clip_bottom) return;
    for (int i = 0; i < count; i++) {
        if (!menu_btn_in_clip(&buttons[i], clip_top, clip_bottom)) continue;
        if (x >= buttons[i].x && x <= buttons[i].x + buttons[i].w &&
            y >= buttons[i].y && y <= buttons[i].y + buttons[i].h) {
            m->hovered_item = i;
            break;
        }
    }
}

bool game_menu_on_scroll(GameMenu* m, float delta, int screen_height) {
    (void)screen_height;
    if (!m || !m->open) return false;
    float uis = m->ui_scale > 0.1f ? m->ui_scale : 1.0f;

    m->menu_scroll_y += delta * 56.0f * uis;
    if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
    if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
    return true;
}

void game_menu_on_mousedown(GameMenu* m, float x, float y, int screen_width, int screen_height) {
    if (!m || !m->open) return;
    float uis = m->ui_scale > 0.1f ? m->ui_scale : 1.0f;
    float clip_top = 0.0f, clip_bottom = (float)screen_height;
    menu_clip_rect(m, uis, screen_width, screen_height, &clip_top, &clip_bottom);

    MenuButton buttons[20];
    int count = 0;
    menu_fill_page_buttons(m, uis, screen_width, screen_height, buttons, &count);
    (void)count;

    if (y < clip_top || y > clip_bottom) {
        m->menu_drag_active = false;
        m->menu_drag_moved = false;
        return;
    }
    m->menu_drag_active = true;
    m->menu_drag_moved = false;
    m->menu_drag_last_y = y;
    m->menu_drag_start_x = x;
    m->menu_drag_start_y = y;
}

MenuAction game_menu_on_mouseup(GameMenu* m, float x, float y, int screen_width, int screen_height) {
    if (!m || !m->open) return MENU_ACTION_NONE;
    bool was_drag = m->menu_drag_active;
    bool moved = m->menu_drag_moved;
    m->menu_drag_active = false;
    m->menu_drag_moved = false;
    m->menu_click_pending = false;
    if (!was_drag) return MENU_ACTION_NONE;
    if (moved) return MENU_ACTION_NONE;
    return game_menu_on_click(m, x, y, screen_width, screen_height);
}

MenuAction game_menu_poll_pending_click(GameMenu* m, int screen_width, int screen_height) {
    if (!m || !m->open || !m->menu_click_pending) return MENU_ACTION_NONE;
    m->menu_click_pending = false;
    return game_menu_on_click(m, m->menu_click_x, m->menu_click_y, screen_width, screen_height);
}

void game_menu_render(GameMenu* m, int screen_width, int screen_height) {
    if (!m->open) return;

    float uis = m->ui_scale > 0.1f ? m->ui_scale : 1.0f;
    int sw = screen_width, sh = screen_height;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    menu_draw_overlay(m->benchmark_running ? 0.2f : (ui_theme_is_dark() ? 0.62f : 0.5f), sw, sh);

    float title_scale = 3.0f * uis;
    const char* title = "Menu";
    if (m->page == MENU_PAGE_SETTINGS_HUB) title = "Settings";
    else if (m->page == MENU_PAGE_SETTINGS) title = "Graphics";
    else if (m->page == MENU_PAGE_SETTINGS_AUDIO) title = "Audio";
    else if (m->page == MENU_PAGE_SETTINGS_VR) title = "VR Comfort";
    float title_w = font_text_width_scaled(title, 8.0f * title_scale);
    float title_x = ((float)sw - title_w) * 0.5f;
    float title_y = (m->page == MENU_PAGE_SETTINGS && m->quality == GFX_QUALITY_MANUAL)
                        ? (float)sh * 0.08f : (float)sh * 0.22f;
    menu_draw_text(title, title_x + 1, title_y + 1, title_scale, 0, 0, 0, 0.5f, sw, sh);
    menu_draw_text(title, title_x, title_y, title_scale, 1, 1, 1, 1, sw, sh);

    MenuButton buttons[20];
    int count = 0;
    menu_fill_page_buttons(m, uis, sw, sh, buttons, &count);

    float border = 8.0f * uis;
    float text_scale = 2.0f * uis;
    float clip_top = 0.0f, clip_bottom = (float)sh;
    menu_clip_rect(m, uis, sw, sh, &clip_top, &clip_bottom);

    int sci_y = (int)((float)sh - clip_bottom);
    int sci_h = (int)(clip_bottom - clip_top);
    if (sci_h < 1) sci_h = 1;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, sci_y, sw, sci_h);

    for (int i = 0; i < count; i++) {
        MenuButton* btn = &buttons[i];
        if (!menu_btn_in_clip(btn, clip_top, clip_bottom)) continue;

        float btn_alpha = (m->hovered_item == i) ? 1.0f : 0.85f;
        if (m->nineslice_tex) {
            menu_draw_nineslice(m->nineslice_tex, btn->x, btn->y, btn->w, btn->h,
                                border, btn_alpha, sw, sh);
        }

        float tw = font_text_width_scaled(btn->label, 8.0f * text_scale);
        float tx = btn->x + (btn->w - tw) * 0.5f;
        float ty = btn->y + (btn->h - 8.0f * text_scale) * 0.5f;
        float brightness = (m->hovered_item == i) ? 1.0f : 0.8f;
        menu_draw_text(btn->label, tx, ty, text_scale, brightness, brightness, brightness, 1.0f, sw, sh);
    }

    glDisable(GL_SCISSOR_TEST);

    if (m->menu_scroll_max > 1.0f) {
        const char* hint = "Drag or scroll for more";
        float hs = 1.25f * uis;
        float hw = font_text_width_scaled(hint, 8.0f * hs);
        menu_draw_text(hint, ((float)sw - hw) * 0.5f, (float)sh - 18.0f * uis, hs,
                       0.75f, 0.75f, 0.75f, 0.9f, sw, sh);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void game_menu_update(GameMenu* m, float dt) {
    if (!m) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    if (m->open && m->menu_drag_active) {
        const InputState* in = input_get_state();
#ifdef __ANDROID__
        const float drag_slop = 4.0f;
#else
        const float drag_slop = 6.0f;
#endif

        float y = in->mouse_y;
        float ddy = m->menu_drag_last_y - y;
        m->menu_drag_last_y = y;
        float move_x = in->mouse_x - m->menu_drag_start_x;
        float move_y = y - m->menu_drag_start_y;
        if (!m->menu_drag_moved && (fabsf(move_x) > drag_slop || fabsf(move_y) > drag_slop))
            m->menu_drag_moved = true;
        if (m->menu_drag_moved && fabsf(ddy) > 0.01f) {
            m->menu_scroll_y += ddy;
            if (m->menu_scroll_y < 0.0f) m->menu_scroll_y = 0.0f;
            if (m->menu_scroll_y > m->menu_scroll_max) m->menu_scroll_y = m->menu_scroll_max;
        }

        if (!input_mouse_left_held() && m->menu_drag_moved) {
#ifndef __ANDROID__
            m->menu_drag_active = false;
            m->menu_drag_moved = false;
#endif
        }
    }

    if (!m->benchmark_running) return;

    extern int g_fps_limit_override;
    g_fps_limit_override = -1;

    m->benchmark_timer += dt;
    if (m->benchmark_timer > 0.35f) {
        m->benchmark_frames++;
        m->benchmark_dt_sum += dt;
    }

    if (m->benchmark_timer < 2.6f) return;

    float avg_fps = 0.0f;
    if (m->benchmark_dt_sum > 0.001f && m->benchmark_frames > 0)
        avg_fps = (float)m->benchmark_frames / m->benchmark_dt_sum;

    GfxQuality picked = GFX_QUALITY_MEDIUM;
#if defined(__ANDROID__)
    if (avg_fps < 28.0f) picked = GFX_QUALITY_POTATO;
    else if (avg_fps < 42.0f) picked = GFX_QUALITY_LOW;
    else picked = GFX_QUALITY_MEDIUM;
#else

    if (avg_fps < 35.0f) picked = GFX_QUALITY_POTATO;
    else if (avg_fps < 50.0f) picked = GFX_QUALITY_LOW;
    else if (avg_fps < 70.0f) picked = GFX_QUALITY_MEDIUM;
    else if (avg_fps < 100.0f) picked = GFX_QUALITY_HIGH;
    else if (avg_fps < 140.0f) picked = GFX_QUALITY_ULTRA;
    else picked = GFX_QUALITY_SUPER;
#endif

    g_fps_limit_override = 0;
    m->quality = picked;
    m->benchmark_running = false;
    const char* names[] = {
        "Potato", "Low", "Medium", "High", "Ultra", "Super", "Auto", "Manual"
    };
    snprintf(m->benchmark_label, sizeof(m->benchmark_label),
             "Result: %s (%.0f FPS)", names[(int)picked], avg_fps);

    if (m->benchmark_first_run) {
        m->startup_benchmark_done = true;
        m->first_run_active = false;
        m->benchmark_first_run = false;
    }
    game_menu_save_settings(m);
}

void game_menu_start_benchmark(GameMenu* m, bool first_run) {
    if (!m || m->benchmark_running) return;
    m->benchmark_running = true;
    m->benchmark_first_run = first_run;
    if (first_run) m->first_run_active = true;
    m->benchmark_timer = 0.0f;
    m->benchmark_frames = 0;
    m->benchmark_dt_sum = 0.0f;
    snprintf(m->benchmark_label, sizeof(m->benchmark_label), "Benchmarking...");
    extern int g_fps_limit_override;
    g_fps_limit_override = -1;
}

bool game_menu_needs_first_run(const GameMenu* m) {
    if (!m) return false;
    return !m->skip_startup_benchmark && !m->startup_benchmark_done;
}

bool game_menu_first_run_active(const GameMenu* m) {
    return m && m->first_run_active;
}

void game_menu_begin_first_run(GameMenu* m) {
    if (!m || m->first_run_active || m->benchmark_running) return;
    m->first_run_active = true;
    game_menu_start_benchmark(m, true);
}

GfxQuality game_menu_get_effective_quality(GameMenu* m) {
    if (m->benchmark_running)
        return GFX_QUALITY_HIGH;
    if (m->quality == GFX_QUALITY_AUTO) {
#if defined(__ANDROID__)
        return GFX_QUALITY_POTATO;
#elif defined(PW_IOS)
        return GFX_QUALITY_POTATO;
#elif defined(__EMSCRIPTEN__)
        return GFX_QUALITY_MEDIUM;
#else
        return GFX_QUALITY_HIGH;
#endif
    }
    return m->quality;
}

#ifdef __EMSCRIPTEN__

void game_menu_load_settings(GameMenu* m) {
    int quality = EM_ASM_INT({
        var v = localStorage.getItem('pw_gfx_quality');
        return v !== null ? parseInt(v) : -1;
    });
    if (quality >= 0 && quality <= 7) {
        m->quality = (GfxQuality)quality;
    }
    float scale = EM_ASM_DOUBLE({
        var v = localStorage.getItem('pw_ui_scale');
        return v !== null ? parseFloat(v) : -1.0;
    });
    if (scale >= 0.5 && scale <= 3.0) {
        m->ui_scale = (float)scale;
    }
    int fs = EM_ASM_INT({
        var v = localStorage.getItem('pw_fullscreen');
        return v !== null ? parseInt(v) : -1;
    });
    if (fs == 0 || fs == 1) {
        m->fullscreen = (fs == 1);
        platform_set_fullscreen(m->fullscreen);
        m->fullscreen = platform_is_fullscreen();
    }
    int mf = EM_ASM_INT({
        var v = localStorage.getItem('pw_manual_fog');
        return v !== null ? parseInt(v) : -1;
    });
    if (mf == 0 || mf == 1) m->manual_fog = (mf == 1);
    int gl = EM_ASM_INT({
        var v = localStorage.getItem('pw_manual_glow_leak');
        return v !== null ? parseInt(v) : -1;
    });
    if (gl >= 0 && gl <= 1) m->manual_glow_leak = (GfxGlowLeakMode)gl;
    int lt = EM_ASM_INT({
        var v = localStorage.getItem('pw_lighting_tech');
        return v !== null ? parseInt(v) : -1;
    });
    if (lt == 0 || lt == 1) m->lighting_tech = (GfxLightingMode)lt;
    int fmc = EM_ASM_INT({
        var v = localStorage.getItem('pw_force_mobile');
        return v !== null ? parseInt(v) : -1;
    });
    if (fmc == 0 || fmc == 1) {
        m->force_mobile_controls = (fmc == 1);
        EM_ASM({
            var el = document.getElementById('touch-controls');
            if (!el) return;
            var force = ($0 === 1);
            var isMobile = ('ontouchstart' in window) || (navigator.maxTouchPoints > 0);
            if (force || isMobile) el.classList.add('visible');
            else el.classList.remove('visible');
        }, m->force_mobile_controls ? 1 : 0);
    }
    int skip = EM_ASM_INT({
        var v = localStorage.getItem('pw_skip_startup_benchmark');
        return v !== null ? parseInt(v) : -1;
    });
    if (skip == 0 || skip == 1) m->skip_startup_benchmark = (skip == 1);
    int done = EM_ASM_INT({
        var v = localStorage.getItem('pw_startup_benchmark_done');
        return v !== null ? parseInt(v) : -1;
    });
    if (done == 0 || done == 1) m->startup_benchmark_done = (done == 1);
    double vm = EM_ASM_DOUBLE({
        var v = localStorage.getItem('pw_vol_master');
        return v !== null ? parseFloat(v) : -1.0;
    });
    if (vm >= 0.0 && vm <= 1.0) m->vol_master = (float)vm;
    double vmu = EM_ASM_DOUBLE({
        var v = localStorage.getItem('pw_vol_music');
        return v !== null ? parseFloat(v) : -1.0;
    });
    if (vmu >= 0.0 && vmu <= 1.0) m->vol_music = (float)vmu;
    double vs = EM_ASM_DOUBLE({
        var v = localStorage.getItem('pw_vol_sfx');
        return v !== null ? parseFloat(v) : -1.0;
    });
    if (vs >= 0.0 && vs <= 1.0) m->vol_sfx = (float)vs;
    int dm = EM_ASM_INT({
        var v = localStorage.getItem('pw_dark_mode');
        return v !== null ? parseInt(v) : -1;
    });
    if (dm == 0 || dm == 1) m->dark_mode = (dm == 1);
    ui_theme_set_dark(m->dark_mode);
    apply_menu_volumes(m);
    int vt = EM_ASM_INT({
        var v = localStorage.getItem('pw_vr_turn');
        return v !== null ? parseInt(v) : -1;
    });
    if (vt >= 0 && vt <= 2) m->vr_turn = vt;
    int vv = EM_ASM_INT({
        var v = localStorage.getItem('pw_vr_vignette');
        return v !== null ? parseInt(v) : -1;
    });
    if (vv >= 0 && vv <= 3) m->vr_vignette = vv;
}

void game_menu_save_settings(GameMenu* m) {
    EM_ASM({
        localStorage.setItem('pw_gfx_quality', $0.toString());
        localStorage.setItem('pw_ui_scale', $1.toFixed(2));
        localStorage.setItem('pw_fullscreen', $2 ? '1' : '0');
        localStorage.setItem('pw_manual_fog', $3 ? '1' : '0');
        localStorage.setItem('pw_manual_glow_leak', $4.toString());
        localStorage.setItem('pw_force_mobile', $5 ? '1' : '0');
        localStorage.setItem('pw_skip_startup_benchmark', $6 ? '1' : '0');
        localStorage.setItem('pw_startup_benchmark_done', $7 ? '1' : '0');
        localStorage.setItem('pw_vol_master', $8.toFixed(2));
        localStorage.setItem('pw_vol_music', $9.toFixed(2));
        localStorage.setItem('pw_vol_sfx', $10.toFixed(2));
        localStorage.setItem('pw_lighting_tech', $11.toString());
        localStorage.setItem('pw_dark_mode', $12 ? '1' : '0');
        localStorage.setItem('pw_vr_turn', $13.toString());
        localStorage.setItem('pw_vr_vignette', $14.toString());
    }, (int)m->quality, (double)m->ui_scale, m->fullscreen ? 1 : 0,
       m->manual_fog ? 1 : 0,
       (int)m->manual_glow_leak,
       m->force_mobile_controls ? 1 : 0,
       m->skip_startup_benchmark ? 1 : 0,
       m->startup_benchmark_done ? 1 : 0,
       (double)m->vol_master, (double)m->vol_music, (double)m->vol_sfx,
       (int)m->lighting_tech,
       m->dark_mode ? 1 : 0,
       m->vr_turn, m->vr_vignette);
}

#else

void game_menu_load_settings(GameMenu* m) {
    char path[512];
    if (!platform_userdata_path("options.cfg", path, sizeof(path))) return;
    FILE* f = fopen(path, "r");
    if (!f) return;

    int bench_ver = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int val;
        float fval;
        if (sscanf(line, "gfx_quality=%d", &val) == 1) {
            if (val >= 0 && val <= 7) m->quality = (GfxQuality)val;
        }
        if (sscanf(line, "ui_scale=%f", &fval) == 1) {
            if (fval >= 0.5f && fval <= 4.0f) m->ui_scale = fval;
        }
        if (sscanf(line, "fullscreen=%d", &val) == 1) {
            m->fullscreen = (val != 0);
        }
        if (sscanf(line, "manual_fog=%d", &val) == 1) {
            m->manual_fog = (val != 0);
        }
        {
            float fval = 0.0f;
            if (sscanf(line, "manual_render_scale=%f", &fval) == 1) {
                if (fval >= 0.4f && fval <= 1.0f) m->manual_render_scale = fval;
            }
        }
        if (sscanf(line, "manual_glow_leak=%d", &val) == 1) {
            if (val >= 0 && val <= 1) m->manual_glow_leak = (GfxGlowLeakMode)val;
        }
        if (sscanf(line, "lighting_tech=%d", &val) == 1) {
            if (val == 0 || val == 1) m->lighting_tech = (GfxLightingMode)val;
        }
        if (sscanf(line, "skip_startup_benchmark=%d", &val) == 1) {
            m->skip_startup_benchmark = (val != 0);
        }
        if (sscanf(line, "startup_benchmark_done=%d", &val) == 1) {
            m->startup_benchmark_done = (val != 0);
        }
        if (sscanf(line, "force_mobile_controls=%d", &val) == 1) {
            m->force_mobile_controls = (val != 0);
        }
        if (sscanf(line, "vol_master=%f", &fval) == 1) {
            if (fval >= 0.0f && fval <= 1.0f) m->vol_master = fval;
        }
        if (sscanf(line, "vol_music=%f", &fval) == 1) {
            if (fval >= 0.0f && fval <= 1.0f) m->vol_music = fval;
        }
        if (sscanf(line, "vol_sfx=%f", &fval) == 1) {
            if (fval >= 0.0f && fval <= 1.0f) m->vol_sfx = fval;
        }
        if (sscanf(line, "dark_mode=%d", &val) == 1) {
            m->dark_mode = (val != 0);
        }
        if (sscanf(line, "vr_turn=%d", &val) == 1) {
            if (val >= 0 && val <= 2) m->vr_turn = val;
        }
        if (sscanf(line, "vr_vignette=%d", &val) == 1) {
            if (val >= 0 && val <= 3) m->vr_vignette = val;
        }
        if (sscanf(line, "bench_ver=%d", &val) == 1) {
            bench_ver = val;
        }
    }
    fclose(f);

    if (bench_ver < 2 && !m->skip_startup_benchmark &&
        m->quality <= GFX_QUALITY_LOW) {
        m->quality = GFX_QUALITY_AUTO;
        m->startup_benchmark_done = false;
    }
    if (m->fullscreen) {
        platform_set_fullscreen(true);
        m->fullscreen = platform_is_fullscreen();
    }
    ui_theme_set_dark(m->dark_mode);
    apply_menu_volumes(m);
}

void game_menu_save_settings(GameMenu* m) {
    char path[512];
    if (!platform_userdata_path("options.cfg", path, sizeof(path))) return;
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "gfx_quality=%d\n", (int)m->quality);
    fprintf(f, "ui_scale=%.2f\n", m->ui_scale);
    fprintf(f, "fullscreen=%d\n", m->fullscreen ? 1 : 0);
    fprintf(f, "manual_fog=%d\n", m->manual_fog ? 1 : 0);
    fprintf(f, "manual_render_scale=%.3f\n", m->manual_render_scale);
    fprintf(f, "manual_glow_leak=%d\n", (int)m->manual_glow_leak);
    fprintf(f, "lighting_tech=%d\n", (int)m->lighting_tech);
    fprintf(f, "skip_startup_benchmark=%d\n", m->skip_startup_benchmark ? 1 : 0);
    fprintf(f, "startup_benchmark_done=%d\n", m->startup_benchmark_done ? 1 : 0);
    fprintf(f, "force_mobile_controls=%d\n", m->force_mobile_controls ? 1 : 0);
    fprintf(f, "vol_master=%.2f\n", m->vol_master);
    fprintf(f, "vol_music=%.2f\n", m->vol_music);
    fprintf(f, "vol_sfx=%.2f\n", m->vol_sfx);
    fprintf(f, "dark_mode=%d\n", m->dark_mode ? 1 : 0);
    fprintf(f, "vr_turn=%d\n", m->vr_turn);
    fprintf(f, "vr_vignette=%d\n", m->vr_vignette);
    fprintf(f, "bench_ver=2\n");
    fclose(f);
}

#endif
