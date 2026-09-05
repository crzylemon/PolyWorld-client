/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: login_screen.c                                                                      |
|   Purpose: (originally for login) Almost all pre-ingame UI                                  |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__

#include "login_screen.h"
#include "log.h"
#include "shader.h"
#include "auth.h"
#include "platform.h"
#include "font.h"
#include "input.h"
#include "ui_theme.h"
#include "game_menu.h"
#include "text_edit.h"
#include "client_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pw_gles.h"
#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

// stb_image forward declarations
extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern void stbi_image_free(void*);

static unsigned int login_load_texture_file(const char* path, int* out_w, int* out_h);
static unsigned int login_tex_from_memory(const uint8_t* data, size_t len, int* out_w, int* out_h);

static unsigned g_login_thumb_gen = 0;
static int g_login_thumbs_inflight = 0;
#define LOGIN_THUMB_MAX_INFLIGHT 4
static TextEdit s_login_user_edit;
static TextEdit s_login_pass_edit;
static bool s_login_field_drag;

// virtual canvas for the game
#define IMG_W 1091
#define IMG_H 711

// website css colors, follows ui_theme light/dark
#define COL_BG_R       (ui_theme_col()->bg[0])
#define COL_BG_G       (ui_theme_col()->bg[1])
#define COL_BG_B       (ui_theme_col()->bg[2])
#define COL_TEXT_R     (ui_theme_col()->text[0])
#define COL_TEXT_G     (ui_theme_col()->text[1])
#define COL_TEXT_B     (ui_theme_col()->text[2])
#define COL_MUTED_R    (ui_theme_col()->muted[0])
#define COL_MUTED_G    (ui_theme_col()->muted[1])
#define COL_MUTED_B    (ui_theme_col()->muted[2])
#define COL_SOFT_R     (ui_theme_col()->soft[0])
#define COL_SOFT_G     (ui_theme_col()->soft[1])
#define COL_SOFT_B     (ui_theme_col()->soft[2])
#define COL_LINE_R     (ui_theme_col()->line[0])
#define COL_LINE_G     (ui_theme_col()->line[1])
#define COL_LINE_B     (ui_theme_col()->line[2])
#define COL_LIME_R     (ui_theme_col()->lime[0])
#define COL_LIME_G     (ui_theme_col()->lime[1])
#define COL_LIME_B     (ui_theme_col()->lime[2])
#define COL_FOCUS_R    (ui_theme_col()->focus[0])
#define COL_FOCUS_G    (ui_theme_col()->focus[1])
#define COL_FOCUS_B    (ui_theme_col()->focus[2])
#define COL_TAB_BG_R   COL_SOFT_R
#define COL_TAB_BG_G   COL_SOFT_G
#define COL_TAB_BG_B   COL_SOFT_B
#define COL_TAB_ON_R   COL_TEXT_R
#define COL_TAB_ON_G   COL_TEXT_G
#define COL_TAB_ON_B   COL_TEXT_B
#define COL_PLAY_R     COL_LIME_R
#define COL_PLAY_G     COL_LIME_G
#define COL_PLAY_B     COL_LIME_B
#define COL_PLAY_BD_R  COL_LIME_R
#define COL_PLAY_BD_G  COL_LIME_G
#define COL_PLAY_BD_B  COL_LIME_B
#define COL_PLAYING_R  (ui_theme_col()->playing[0])
#define COL_PLAYING_G  (ui_theme_col()->playing[1])
#define COL_PLAYING_B  (ui_theme_col()->playing[2])
#define COL_ERR_R      (ui_theme_col()->err[0])
#define COL_ERR_G      (ui_theme_col()->err[1])
#define COL_ERR_B      (ui_theme_col()->err[2])
#define COL_BORDER_R   COL_LINE_R
#define COL_BORDER_G   COL_LINE_G
#define COL_BORDER_B   COL_LINE_B
#define COL_INPUT_R    COL_LINE_R
#define COL_INPUT_G    COL_LINE_G
#define COL_INPUT_B    COL_LINE_B
#define COL_ON_INK_R   (ui_theme_col()->on_ink[0])
#define COL_ON_INK_G   (ui_theme_col()->on_ink[1])
#define COL_ON_INK_B   (ui_theme_col()->on_ink[2])
#define COL_ON_LIME_R  (ui_theme_col()->on_lime[0])
#define COL_ON_LIME_G  (ui_theme_col()->on_lime[1])
#define COL_ON_LIME_B  (ui_theme_col()->on_lime[2])

// navbar info
#define NAV_H          52.0f
#define NAV_PAD_X      20.0f
#define NAV_LOGO_H     28.0f
#define NAV_LOGO_W     (NAV_LOGO_H * 582.0f / 151.0f)
#define SITE_BANNER_H  42.0f

// Login form stuff.
#define LOGIN_FORM_W   320.0f
#define LOGIN_FORM_X   (((float)IMG_W - LOGIN_FORM_W) * 0.5f)
#define LOGIN_TITLE_Y  (NAV_H + 36.0f)
#define LOGIN_USER_LBL_Y (NAV_H + 84.0f)
#define LOGIN_USER_Y   (NAV_H + 108.0f)
#define LOGIN_USER_H   40.0f
#define LOGIN_PASS_LBL_Y (NAV_H + 168.0f)
#define LOGIN_PASS_Y   (NAV_H + 192.0f)
#define LOGIN_PASS_H   40.0f
#define LOGIN_BTN_Y    (NAV_H + 258.0f)
#define LOGIN_BTN_H    44.0f
#define LOGIN_GUEST_Y  (NAV_H + 316.0f)
#define LOGIN_GUEST_H  44.0f
#define LOGIN_CHECK_Y  (NAV_H + 374.0f)
#define LOGIN_CHECK_SIZE 28.0f
#define LOGIN_ERR_Y    (NAV_H + 418.0f)
#define LOGIN_RADIUS   8.0f

// Games browser
#define GAMES_TITLE_GAP 28.0f
#define GAMES_TAB_H    30.0f
#define GAMES_TAB_GAP  8.0f
#define GAMES_CARD_W   200.0f
#define GAMES_CARD_H   148.0f
#define GAMES_CARD_GAP_X 18.0f
#define GAMES_CARD_GAP_Y 20.0f
#define GAMES_GRID_X   40.0f
#define GAMES_THUMB_RADIUS 8.0f

// Friends/Continue/GOTW for logged in users...
// TODO: make joining from site automaically create session file
#define HOME_TOP_GAP           68.0f
#define HOME_HEAD_H            20.0f
#define HOME_HEAD_GAP           8.0f
#define HOME_SECTION_GAP       22.0f
#define FRIEND_CHIP_W          92.0f
#define FRIEND_AVATAR_D        40.0f
#define FRIEND_CHIP_H          64.0f
#define HOME_RAIL_THUMB_W     160.0f
#define HOME_RAIL_THUMB_H      90.0f
#define HOME_RAIL_GAP_X        14.0f
#define HOME_RAIL_TILE_H      (HOME_RAIL_THUMB_H + 34.0f)
#define HOME_RAIL_MAX_VISIBLE   5
#define HOME_FRIEND_SECTION_H (HOME_HEAD_H + HOME_HEAD_GAP + FRIEND_CHIP_H + HOME_SECTION_GAP)
#define HOME_RAIL_SECTION_H   (HOME_HEAD_H + HOME_HEAD_GAP + HOME_RAIL_TILE_H + HOME_SECTION_GAP)
#define HOME_GAMES_HEAD_H     (HOME_HEAD_H + HOME_HEAD_GAP)
#define HOME_GAMES_TABS_GAP    8.0f
#define HOME_GAMES_ROWS        2
#define HOME_GAMES_ROW_GAP    12.0f
#define HOME_GAMES_RAIL_H     (HOME_GAMES_ROWS * HOME_RAIL_TILE_H + (HOME_GAMES_ROWS - 1) * HOME_GAMES_ROW_GAP)
#define HOME_GAMES_BODY_H     (GAMES_TAB_H + HOME_GAMES_TABS_GAP + HOME_GAMES_RAIL_H + HOME_SECTION_GAP)
#define GAMES_TAB_TO_GRID_GAP  46.0f // for guests...

static float games_grid_y(const LoginScreen* ls);
static float games_view_h(const LoginScreen* ls);
static void draw_site_banner(const LoginScreen* ls, int vwidth, int vheight);

static bool login_is_home_user(const LoginScreen* ls) {
    return ls->session_token[0] != '\0';
}

static bool login_banner_showing(const LoginScreen* ls) {
    return ls && ls->banner_enabled && ls->banner_message[0];
}

static float login_banner_h(const LoginScreen* ls) {
    return login_banner_showing(ls) ? SITE_BANNER_H : 0.0f;
}

static float login_chrome_bottom(const LoginScreen* ls) {
    return NAV_H + login_banner_h(ls);
}

static float login_home_top_y(const LoginScreen* ls) {
    return login_chrome_bottom(ls) + HOME_TOP_GAP;
}

static float login_ly(const LoginScreen* ls, float y) {
    return y + login_banner_h(ls);
}

static float home_games_view_w(void) {
    return (float)IMG_W - GAMES_GRID_X * 2.0f;
}
static int home_games_cols(int count) {
    if (count <= 0) return 0;
    return (count + HOME_GAMES_ROWS - 1) / HOME_GAMES_ROWS;
}
static float home_games_content_w(int count) {
    int cols = home_games_cols(count);
    if (cols <= 0) return 0.0f;
    return (float)cols * HOME_RAIL_THUMB_W + (float)(cols - 1) * HOME_RAIL_GAP_X;
}
static float home_games_max_scroll_x(const LoginScreen* ls) {
    float max = home_games_content_w(ls->game_count) - home_games_view_w();
    return max > 0.0f ? max : 0.0f;
}
static void home_games_clamp_scroll_x(LoginScreen* ls) {
    float max = home_games_max_scroll_x(ls);
    if (ls->games_rail_scroll_target < 0.0f) ls->games_rail_scroll_target = 0.0f;
    if (ls->games_rail_scroll_target > max) ls->games_rail_scroll_target = max;
    if (ls->games_rail_scroll_x < 0.0f) ls->games_rail_scroll_x = 0.0f;
    if (ls->games_rail_scroll_x > max) ls->games_rail_scroll_x = max;
}
// 2 rows, instead of the guest grid's inf rows but limited columns.
static void home_games_cell(int index, float scroll_x, float rail_y,
                            float* out_x, float* out_y) {
    int col = index / HOME_GAMES_ROWS;
    int row = index % HOME_GAMES_ROWS;
    if (out_x) *out_x = GAMES_GRID_X + (float)col * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X) - scroll_x;
    if (out_y) *out_y = rail_y + (float)row * (HOME_RAIL_TILE_H + HOME_GAMES_ROW_GAP);
}
static bool home_games_point_in_rail(const LoginScreen* ls, float x, float y) {
    float rail_y = games_grid_y(ls) - ls->games_scroll_y;
    float rail_bottom = rail_y + HOME_GAMES_RAIL_H;
    return x >= GAMES_GRID_X && x <= (float)IMG_W - GAMES_GRID_X &&
           y >= rail_y && y <= rail_bottom;
}

static float login_home_friends_y(const LoginScreen* ls) {
    return login_home_top_y(ls);
}
static float login_home_continue_y(const LoginScreen* ls) {
    return login_home_friends_y(ls) + HOME_FRIEND_SECTION_H;
}
static float login_home_gotw_y(const LoginScreen* ls) {
    float y = login_home_continue_y(ls);
    if (!ls->home_fetched || ls->continue_count > 0) y += HOME_RAIL_SECTION_H;
    return y;
}
static float login_home_games_head_y(const LoginScreen* ls) {
    float y = login_home_gotw_y(ls);
    if (!ls->home_fetched || ls->gotw_count > 0) y += HOME_RAIL_SECTION_H;
    return y;
}
static float games_tab_y(const LoginScreen* ls) {
    if (!login_is_home_user(ls)) return login_home_top_y(ls);
    return login_home_games_head_y(ls) + HOME_GAMES_HEAD_H;
}
static float games_grid_y(const LoginScreen* ls) {
    if (login_is_home_user(ls))
        return games_tab_y(ls) + GAMES_TAB_H + HOME_GAMES_TABS_GAP;
    return games_tab_y(ls) + GAMES_TAB_TO_GRID_GAP;
}

static unsigned int g_logo_tex = 0;
static unsigned int g_checked_tex = 0;
static unsigned int g_unchecked_tex = 0;
static unsigned int g_update_tex = 0;
static int g_update_tw = 0;
static int g_update_th = 0;
static int g_logo_w = 0, g_logo_h = 0;
static unsigned int g_tex_shader = 0;
static unsigned int g_color_shader = 0;
static unsigned int g_round_shader = 0;
static unsigned int g_round_tex_shader = 0;
static unsigned int g_vao = 0;
static bool g_initialized = false;

static float g_scale_x = 1.0f, g_scale_y = 1.0f;
static int g_hit_ox = 0, g_hit_oy = 0, g_hit_w = 1, g_hit_h = 1;
static int g_vp_x = 0, g_vp_y = 0, g_vp_w = 1, g_vp_h = 1;

static int games_grid_cols(void);
static float games_tile_h(void);
static float games_view_h(const LoginScreen* ls);

static void screen_to_image(int x, int y, int* out_x, int* out_y) {
    int ix = (int)(((float)(x - g_hit_ox) * (float)IMG_W) / (float)g_hit_w);
    int iy = (int)(((float)(y - g_hit_oy) * (float)IMG_H) / (float)g_hit_h);
    if (out_x) *out_x = ix;
    if (out_y) *out_y = iy;
}

static bool games_point_in_grid(const LoginScreen* ls, float x, float y) {
    (void)x;
    float top = login_is_home_user(ls) ? login_home_top_y(ls) : games_grid_y(ls);
    float view_bottom = top + games_view_h(ls);
    return y >= top && y <= view_bottom;
}

#define TXT_TITLE_H  26.0f
#define TXT_BODY_H   18.0f
#define TXT_SMALL_H  14.0f
#define TXT_META_H   13.0f

static void draw_text_scaled_h(const char* text, float x, float y, float img_h,
                               float r, float g, float b, int scr_w, int scr_h) {
    float ph = img_h * g_scale_y;
    font_draw_scaled(text, x * g_scale_x, y * g_scale_y, ph, r, g, b, 1.0f, scr_w, scr_h);
}
static void draw_text_scaled(const char* text, float x, float y, float r, float g, float b, int scr_w, int scr_h) {
    draw_text_scaled_h(text, x, y, TXT_BODY_H, r, g, b, scr_w, scr_h);
}
static void draw_text_small_scaled(const char* text, float x, float y, float r, float g, float b, int scr_w, int scr_h) {
    draw_text_scaled_h(text, x, y, TXT_SMALL_H, r, g, b, scr_w, scr_h);
}
static void draw_text_title_scaled(const char* text, float x, float y, float r, float g, float b, int scr_w, int scr_h) {
    draw_text_scaled_h(text, x, y, TXT_TITLE_H, r, g, b, scr_w, scr_h);
}

static float measure_text_h(const char* text, float img_h) {
    return font_text_width_scaled(text, img_h);
}
static float measure_text(const char* text) {
    return measure_text_h(text, TXT_BODY_H);
}
static float measure_text_small(const char* text) {
    return measure_text_h(text, TXT_SMALL_H);
}
static float measure_text_title(const char* text) {
    return measure_text_h(text, TXT_TITLE_H);
}

static void ensure_draw_shaders(void) {
    if (!g_color_shader) g_color_shader = shader_load_program("ui_color");
    if (!g_tex_shader) g_tex_shader = shader_load_program("ui_tex");
    if (!g_round_shader) g_round_shader = shader_load_program("ui_round_rgb");
    if (!g_round_tex_shader) g_round_tex_shader = shader_load_program("ui_round_tex");
}



static void draw_uv_quad_uv(float x, float y, float w, float h,
                            float u0, float v0, float u1, float v1, unsigned int prog) {
    float nx0 = x / (float)IMG_W * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)IMG_H * 2.0f;
    float nx1 = (x + w) / (float)IMG_W * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)IMG_H * 2.0f;
    float tv[] = {
        nx0,ny0, u0,v0,  nx1,ny0, u1,v0,  nx1,ny1, u1,v1,
        nx0,ny0, u0,v0,  nx1,ny1, u1,v1,  nx0,ny1, u0,v1
    };
    glUseProgram(prog);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tv), tv, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

static void draw_uv_quad(float x, float y, float w, float h, unsigned int prog) {
    draw_uv_quad_uv(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, prog);
}

static void draw_rect_img_a(float x, float y, float w, float h,
                            float r, float g, float b, float a) {
    ensure_draw_shaders();
    float nx0 = x / (float)IMG_W * 2.0f - 1.0f;
    float ny0 = 1.0f - y / (float)IMG_H * 2.0f;
    float nx1 = (x + w) / (float)IMG_W * 2.0f - 1.0f;
    float ny1 = 1.0f - (y + h) / (float)IMG_H * 2.0f;
    float rv[] = { nx0,ny0, nx1,ny0, nx1,ny1, nx0,ny0, nx1,ny1, nx0,ny1 };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_color_shader);
    glUniform4f(glGetUniformLocation(g_color_shader, "u_color"), r, g, b, a);
    unsigned int vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rv), rv, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDisable(GL_BLEND);
}

static void draw_rect_img(float x, float y, float w, float h, float r, float g, float b) {
    draw_rect_img_a(x, y, w, h, r, g, b, 1.0f);
}

static void draw_round_rect_img(float x, float y, float w, float h, float radius,
                                float r, float g, float b) {
    ensure_draw_shaders();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_round_shader);
    glUniform3f(glGetUniformLocation(g_round_shader, "u_color"), r, g, b);
    glUniform2f(glGetUniformLocation(g_round_shader, "u_size"), w, h);
    glUniform1f(glGetUniformLocation(g_round_shader, "u_radius"), radius);
    draw_uv_quad(x, y, w, h, g_round_shader);
    glDisable(GL_BLEND);
}

static void draw_rect_border_img(float x, float y, float w, float h, float bw,
                                 float fr, float fg, float fb,
                                 float br, float bg, float bb) {
    draw_rect_img(x, y, w, h, br, bg, bb);
    if (w > bw * 2.0f && h > bw * 2.0f)
        draw_rect_img(x + bw, y + bw, w - bw * 2.0f, h - bw * 2.0f, fr, fg, fb);
}

static void draw_round_border_img(float x, float y, float w, float h, float radius, float bw,
                                  float fr, float fg, float fb,
                                  float br, float bg, float bb) {
    draw_round_rect_img(x, y, w, h, radius, br, bg, bb);
    if (w > bw * 2.0f && h > bw * 2.0f)
        draw_round_rect_img(x + bw, y + bw, w - bw * 2.0f, h - bw * 2.0f,
                            fmaxf(0.0f, radius - bw), fr, fg, fb);
}

static void draw_tex_img(unsigned int tex, float x, float y, float w, float h) {
    if (!tex) return;
    ensure_draw_shaders();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_tex_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(g_tex_shader, "u_tex"), 0);
    draw_uv_quad(x, y, w, h, g_tex_shader);
    glDisable(GL_BLEND);
}

static void draw_logo_img(float x, float y, float w, float h) {
    if (!g_logo_tex) return;
    ensure_draw_shaders();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_tex_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_logo_tex);
    glUniform1i(glGetUniformLocation(g_tex_shader, "u_tex"), 0);
    draw_uv_quad(x, y, w, h, g_tex_shader);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

#define NAV_CHIP_GAMES   0
#define NAV_CHIP_AVATAR  1
#define NAV_CHIP_LOGOUT  2
#define NAV_CHIP_THEME   3
#define NAV_CHIP_CATALOG 4

static int nav_build_chips(const LoginScreen* ls, const char** labels, int* ids, int maxn) {
    int n = 0;
    if (n < maxn) { labels[n] = "Games"; ids[n++] = NAV_CHIP_GAMES; }
    if (ls->logged_in && ls->session_token[0] != '\0') {
        if (n < maxn) { labels[n] = "Catalog"; ids[n++] = NAV_CHIP_CATALOG; }
        if (n < maxn) { labels[n] = "Avatar"; ids[n++] = NAV_CHIP_AVATAR; }
    }
    if (n < maxn) { labels[n] = ui_theme_is_dark() ? "Light" : "Dark"; ids[n++] = NAV_CHIP_THEME; }
    if (ls->logged_in && ls->session_token[0] != '\0') {
        if (n < maxn) { labels[n] = "Logout"; ids[n++] = NAV_CHIP_LOGOUT; }
    }
    return n;
}

static void draw_nav_bar(const LoginScreen* ls, int vwidth, int vheight) {
    draw_rect_img(0.0f, 0.0f, (float)IMG_W, NAV_H, COL_BG_R, COL_BG_G, COL_BG_B);
    draw_rect_img(0.0f, NAV_H - 1.0f, (float)IMG_W, 1.0f, COL_LINE_R, COL_LINE_G, COL_LINE_B);

    float logo_y = (NAV_H - NAV_LOGO_H) * 0.5f;
    draw_logo_img(NAV_PAD_X, logo_y, NAV_LOGO_W, NAV_LOGO_H);

    const char* chips[6];
    int ids[6];
    int n = nav_build_chips(ls, chips, ids, 6);
    float chip_x = (float)IMG_W - NAV_PAD_X;
    for (int i = n - 1; i >= 0; i--) {
        float tw = measure_text_small(chips[i]) + 28.0f;
        chip_x -= tw + 8.0f;
        float cy = (NAV_H - 26.0f) * 0.5f;
        if (ids[i] == NAV_CHIP_GAMES) {
            draw_round_rect_img(chip_x, cy, tw, 26.0f, 13.0f, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
            draw_text_small_scaled(chips[i], chip_x + 14.0f, cy + 6.0f,
                                   COL_ON_INK_R, COL_ON_INK_G, COL_ON_INK_B, vwidth, vheight);
        } else if (ids[i] == NAV_CHIP_LOGOUT) {
            draw_round_rect_img(chip_x, cy, tw, 26.0f, 13.0f, COL_ERR_R, COL_ERR_G, COL_ERR_B);
            draw_text_small_scaled(chips[i], chip_x + 14.0f, cy + 6.0f, 1.0f, 1.0f, 1.0f, vwidth, vheight);
        } else {
            draw_round_rect_img(chip_x, cy, tw, 26.0f, 13.0f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
            draw_text_small_scaled(chips[i], chip_x + 14.0f, cy + 6.0f,
                                   COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }
    }
    draw_site_banner(ls, vwidth, vheight);
}

static int nav_chip_hit(const LoginScreen* ls, float x, float y) {
#ifdef __ANDROID__
    // Android users don't have mouses so clicks are weird. Give a bit more space
    if (y < 0.0f || y > NAV_H + 8.0f) return -1;
    float hit_pad = 10.0f;
#else
    if (y < 0.0f || y > NAV_H) return -1;
    float hit_pad = 0.0f;
#endif
    const char* chips[6];
    int ids[6];
    int n = nav_build_chips(ls, chips, ids, 6);
    float chip_x = (float)IMG_W - NAV_PAD_X;
    for (int i = n - 1; i >= 0; i--) {
        float tw = measure_text_small(chips[i]) + 28.0f;
        chip_x -= tw + 8.0f;
        float cy = (NAV_H - 26.0f) * 0.5f;
        if (x >= chip_x - hit_pad && x <= chip_x + tw + hit_pad &&
            y >= cy - hit_pad && y <= cy + 26.0f + hit_pad)
            return ids[i];
    }
    return -1;
}

static void draw_round_tex_img(unsigned int tex, float x, float y, float w, float h, float radius,
                               int src_w, int src_h) {
    if (!tex) return;
    ensure_draw_shaders();
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (src_w > 0 && src_h > 0 && w > 0.0f && h > 0.0f) {
        float tar = w / h;
        float src = (float)src_w / (float)src_h;
        if (src > tar + 0.001f) {
            float visible = tar / src;
            u0 = (1.0f - visible) * 0.5f;
            u1 = 1.0f - u0;
        } else if (src < tar - 0.001f) {
            float visible = src / tar;
            v0 = (1.0f - visible) * 0.5f;
            v1 = 1.0f - v0;
        }
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_round_tex_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(g_round_tex_shader, "u_tex"), 0);
    glUniform2f(glGetUniformLocation(g_round_tex_shader, "u_size"), w, h);
    glUniform1f(glGetUniformLocation(g_round_tex_shader, "u_radius"), radius);
    draw_uv_quad_uv(x, y, w, h, u0, v0, u1, v1, g_round_tex_shader);
    glDisable(GL_BLEND);
}

static int games_grid_cols(void) {
    return 4;
}

static float games_thumb_h(void) {
    return GAMES_CARD_W * 9.0f / 16.0f;
}

static float games_tile_h(void) {
    return games_thumb_h() + 40.0f;
}

static float games_row_pitch(void) {
    return games_tile_h() + GAMES_CARD_GAP_Y;
}

static float games_view_h(const LoginScreen* ls) {
    if (login_is_home_user(ls))
        return (float)IMG_H - login_home_top_y(ls) - 36.0f;
    return (float)IMG_H - games_grid_y(ls) - 36.0f;
}

static float games_max_scroll(const LoginScreen* ls) {
    float games_h;
    if (login_is_home_user(ls)) {
        // Two-row sideways rail
        games_h = HOME_GAMES_RAIL_H;
    } else {
        int cols = games_grid_cols();
        int rows = (ls->game_count + cols - 1) / cols;
        games_h = (rows > 0)
            ? ((float)rows * games_tile_h() + (float)(rows - 1) * GAMES_CARD_GAP_Y)
            : 0.0f;
    }
    float header_h = login_is_home_user(ls) ? (games_grid_y(ls) - login_home_top_y(ls)) : 0.0f;
    float content_h = header_h + games_h;
    float max = content_h - games_view_h(ls);
    return max > 0.0f ? max : 0.0f;
}

static void games_clamp_scroll(LoginScreen* ls) {
    float max = games_max_scroll(ls);
    if (ls->games_scroll_target < 0.0f) ls->games_scroll_target = 0.0f;
    if (ls->games_scroll_target > max) ls->games_scroll_target = max;
    if (ls->games_scroll_y < 0.0f) ls->games_scroll_y = 0.0f;
    if (ls->games_scroll_y > max) ls->games_scroll_y = max;
}

static void games_ensure_selection_visible(LoginScreen* ls) {
    if (ls->game_count <= 0) return;
    int cols = games_grid_cols();
    int row = ls->selected_game / cols;
    float pitch = games_row_pitch();
    float tile_h = games_tile_h();
    float view_h = games_view_h(ls);
    float header_h = login_is_home_user(ls) ? (games_grid_y(ls) - login_home_top_y(ls)) : 0.0f;
    float top = header_h + (float)row * pitch;
    float bot = top + tile_h;
    if (top < ls->games_scroll_target)
        ls->games_scroll_target = top;
    else if (bot > ls->games_scroll_target + view_h)
        ls->games_scroll_target = bot - view_h;
    games_clamp_scroll(ls);
}

static void games_card_rect(const LoginScreen* ls, int index,
                            float* out_x, float* out_y, float* out_tw, float* out_th) {
    if (login_is_home_user(ls)) {
        float rx, ry;
        home_games_cell(index, ls->games_rail_scroll_x,
                        games_grid_y(ls) - ls->games_scroll_y, &rx, &ry);
        if (out_x) *out_x = rx;
        if (out_y) *out_y = ry;
        if (out_tw) *out_tw = HOME_RAIL_THUMB_W;
        if (out_th) *out_th = HOME_RAIL_THUMB_H;
        return;
    }
    int cols = games_grid_cols();
    float tile_w = GAMES_CARD_W;
    float tile_h = games_tile_h();
    float thumb_h = games_thumb_h();
    float grid_w = (float)cols * tile_w + (float)(cols - 1) * GAMES_CARD_GAP_X;
    float grid_origin_x = (((float)IMG_W - grid_w) * 0.5f);
    if (grid_origin_x < GAMES_GRID_X) grid_origin_x = GAMES_GRID_X;
    int col = index % cols;
    int row = index / cols;
    if (out_x) *out_x = grid_origin_x + (float)col * (tile_w + GAMES_CARD_GAP_X);
    if (out_y) *out_y = games_grid_y(ls) + (float)row * (tile_h + GAMES_CARD_GAP_Y) - ls->games_scroll_y;
    if (out_tw) *out_tw = tile_w;
    if (out_th) *out_th = thumb_h;
}

static int games_hit_card(LoginScreen* ls, float x, float y) {
    if (ls->game_count <= 0 || ls->games_loading || !ls->games_fetched) return -1;
    if (!games_point_in_grid(ls, x, y)) return -1;
    if (login_is_home_user(ls)) {
        float rail_y = games_grid_y(ls) - ls->games_scroll_y;
        if (y < rail_y || y > rail_y + HOME_GAMES_RAIL_H) return -1;
        if (x < GAMES_GRID_X || x > (float)IMG_W - GAMES_GRID_X) return -1;
        for (int i = 0; i < ls->game_count && i < LOGIN_MAX_GAMES; i++) {
            float rx, ry;
            home_games_cell(i, ls->games_rail_scroll_x, rail_y, &rx, &ry);
            if (x >= rx && x <= rx + HOME_RAIL_THUMB_W &&
                y >= ry && y <= ry + HOME_RAIL_THUMB_H)
                return i;
        }
        return -1;
    }
    const float tile_w = GAMES_CARD_W;
    const float card_gap_x = GAMES_CARD_GAP_X;
    const float card_gap_y = GAMES_CARD_GAP_Y;
    const float grid_y = games_grid_y(ls);
    int cols = games_grid_cols();
    float tile_h = games_tile_h();
    float grid_w = (float)cols * tile_w + (float)(cols - 1) * card_gap_x;
    float grid_origin_x = (((float)IMG_W - grid_w) * 0.5f);
    if (grid_origin_x < GAMES_GRID_X) grid_origin_x = GAMES_GRID_X;
    for (int i = 0; i < ls->game_count && i < LOGIN_MAX_GAMES; i++) {
        int col = i % cols;
        int row = i / cols;
        float cx = grid_origin_x + (float)col * (tile_w + card_gap_x);
        float cy = grid_y + (float)row * (tile_h + card_gap_y) - ls->games_scroll_y;
        if (x >= cx && x <= cx + tile_w && y >= cy && y <= cy + tile_h) return i;
    }
    return -1;
}

static int games_offline_tab(const LoginScreen* ls) {
    return (ls->session_token[0] != '\0') ? 5 : 4;
}

static int games_tab_count(const LoginScreen* ls) {
    return games_offline_tab(ls) + 1;
}

static bool games_tab_is_offline(const LoginScreen* ls) {
    return ls->games_tab == games_offline_tab(ls);
}

static bool games_tab_is_my_games(const LoginScreen* ls) {
    return ls->session_token[0] != '\0' && ls->games_tab == 4;
}

static const char* games_tab_label(const LoginScreen* ls, int tab) {
    switch (tab) {
        case 0: return "Recommended";
        case 1: return "Top Playing";
        case 2: return "Popular";
        case 3: return "Newest";
        case 4: return (ls->session_token[0] != '\0') ? "My Games" : "Offline";
        case 5: return "Offline";
        default: return "";
    }
}

static const char* games_tab_sort(int tab) {
    switch (tab) {
        case 0: return "recommended";
        case 1: return "playing";
        case 2: return "popular";
        case 3: return "newest";
        default: return "recommended";
    }
}

static float games_tab_width(const LoginScreen* ls, int tab) {
    switch (tab) {
        case 0: return 128.0f;
        case 1: return 118.0f;
        case 2: return 88.0f;
        case 3: return 88.0f;
        case 4: return (ls->session_token[0] != '\0') ? 96.0f : 88.0f;
        case 5: return 88.0f;
        default: return 88.0f;
    }
}

static float games_tabs_total_width(const LoginScreen* ls) {
    int n = games_tab_count(ls);
    float total = 0.0f;
    for (int i = 0; i < n; i++) {
        total += games_tab_width(ls, i);
        if (i + 1 < n) total += GAMES_TAB_GAP;
    }
    return total;
}

static void games_tab_rect(const LoginScreen* ls, int tab, float* out_x, float* out_w) {
    float x = (((float)IMG_W - games_tabs_total_width(ls)) * 0.5f);
    int n = games_tab_count(ls);
    for (int i = 0; i < n; i++) {
        float w = games_tab_width(ls, i);
        if (i == tab) {
            if (out_x) *out_x = x;
            if (out_w) *out_w = w;
            return;
        }
        x += w + GAMES_TAB_GAP;
    }
    if (out_x) *out_x = x;
    if (out_w) *out_w = 80.0f;
}

static void login_clear_game_thumbs(LoginScreen* ls) {
    g_login_thumb_gen++;
    g_login_thumbs_inflight = 0;
    for (int i = 0; i < LOGIN_MAX_GAMES; i++) {
        if (ls->games[i].thumb_loaded && ls->games[i].thumb_tex) {
            glDeleteTextures(1, &ls->games[i].thumb_tex);
        }
        ls->games[i].thumb_tex = 0;
        ls->games[i].thumb_loaded = false;
        ls->games[i].thumb_loading = false;
    }
}

static int json_copy_string(const char* src, char* dst, int dst_max) {
    int ti = 0;
    while (*src && *src != '"' && ti < dst_max - 1) {
        if (*src == '\\' && src[1]) {
            src++;
            if (*src == '/' || *src == '\\' || *src == '"' || *src == 'n' || *src == 't') {
                if (*src == 'n') dst[ti++] = '\n';
                else if (*src == 't') dst[ti++] = '\t';
                else dst[ti++] = *src;
            } else {
                dst[ti++] = *src;
            }
            src++;
            continue;
        }
        dst[ti++] = *src++;
    }
    dst[ti] = '\0';
    return ti;
}

static const char* json_after_key(const char* json, const char* key) {
    if (!json || !key) return NULL;
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static int json_field_string(const char* json, const char* key, char* dst, int dst_max) {
    if (dst && dst_max > 0) dst[0] = '\0';
    const char* p = json_after_key(json, key);
    if (!p || *p != '"' || !dst || dst_max < 2) return 0;
    return json_copy_string(p + 1, dst, dst_max);
}

static bool json_field_bool(const char* json, const char* key, bool def) {
    const char* p = json_after_key(json, key);
    if (!p) return def;
    if (*p == 't' || *p == 'T' || *p == '1') return true;
    if (*p == 'f' || *p == 'F' || *p == '0' || (*p == 'n' && strncmp(p, "null", 4) == 0))
        return false;
    return def;
}

static void login_clear_banner_icon(LoginScreen* ls) {
    if (!ls) return;
    if (ls->banner_icon_tex) {
        glDeleteTextures(1, &ls->banner_icon_tex);
        ls->banner_icon_tex = 0;
    }
    ls->banner_icon_w = 0;
    ls->banner_icon_h = 0;
    ls->banner_icon_loaded = false;
    ls->banner_icon_loading = false;
}

static unsigned g_login_banner_gen = 0;

typedef struct {
    LoginScreen* ls;
    unsigned gen;
} LoginBannerCtx;

static void login_banner_icon_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    LoginBannerCtx* ctx = (LoginBannerCtx*)user;
    if (!ctx) return;
    LoginScreen* ls = ctx->ls;
    unsigned gen = ctx->gen;
    free(ctx);
    (void)path;
    if (!ls || gen != g_login_banner_gen) return;
    ls->banner_icon_loading = false;
    int tw = 0, thh = 0;
    unsigned int tex = login_tex_from_memory(data, len, &tw, &thh);
    if (!tex) return;
    if (ls->banner_icon_tex) glDeleteTextures(1, &ls->banner_icon_tex);
    ls->banner_icon_tex = tex;
    ls->banner_icon_w = tw;
    ls->banner_icon_h = thh;
    ls->banner_icon_loaded = true;
}

static void login_start_banner_icon(LoginScreen* ls) {
    if (!ls || !ls->banner_icon_url[0] || ls->banner_icon_loading) return;
    LoginBannerCtx* ctx = (LoginBannerCtx*)malloc(sizeof(LoginBannerCtx));
    if (!ctx) return;
    ctx->ls = ls;
    ctx->gen = g_login_banner_gen;
    ls->banner_icon_loading = true;
    platform_load_file(ls->banner_icon_url, login_banner_icon_on_loaded, ctx);
}

static void login_banner_json_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    LoginBannerCtx* ctx = (LoginBannerCtx*)user;
    if (!ctx) return;
    LoginScreen* ls = ctx->ls;
    unsigned gen = ctx->gen;
    free(ctx);
    (void)path;
    if (!ls || gen != g_login_banner_gen) return;
    ls->banner_loading = false;
    ls->banner_fetched = true;

    char prev_icon[512];
    snprintf(prev_icon, sizeof(prev_icon), "%s", ls->banner_icon_url);

    ls->banner_enabled = false;
    ls->banner_message[0] = '\0';
    ls->banner_link[0] = '\0';
    ls->banner_tone[0] = '\0';
    ls->banner_icon_url[0] = '\0';

    if (!data || len < 8) {
        login_clear_banner_icon(ls);
        return;
    }
    // don't assume the download is NUL-terminated
    char* json = (char*)malloc(len + 1);
    if (!json) return;
    memcpy(json, data, len);
    json[len] = '\0';

    bool ok = json_field_bool(json, "ok", true);
    bool enabled = json_field_bool(json, "enabled", false);
    char message[256];
    char link[512];
    char tone[16];
    char icon[512];
    json_field_string(json, "message", message, sizeof(message));
    json_field_string(json, "link_url", link, sizeof(link));
    json_field_string(json, "tone", tone, sizeof(tone));
    json_field_string(json, "icon", icon, sizeof(icon));
    free(json);

    if (!ok || !enabled || !message[0]) {
        login_clear_banner_icon(ls);
        return;
    }

    snprintf(ls->banner_message, sizeof(ls->banner_message), "%s", message);
    snprintf(ls->banner_link, sizeof(ls->banner_link), "%s", link);
    if (tone[0] == 's') snprintf(ls->banner_tone, sizeof(ls->banner_tone), "success");
    else if (tone[0] == 'w') snprintf(ls->banner_tone, sizeof(ls->banner_tone), "warn");
    else snprintf(ls->banner_tone, sizeof(ls->banner_tone), "info");
    snprintf(ls->banner_icon_url, sizeof(ls->banner_icon_url), "%s", icon);
    ls->banner_enabled = true;

    if (ls->banner_icon_url[0] == '\0') {
        login_clear_banner_icon(ls);
    } else if (strcmp(prev_icon, ls->banner_icon_url) != 0 || !ls->banner_icon_loaded) {
        login_clear_banner_icon(ls);
        login_start_banner_icon(ls);
    }
}

static void login_start_banner_fetch(LoginScreen* ls) {
    if (!ls || ls->banner_loading) return;
    LoginBannerCtx* ctx = (LoginBannerCtx*)malloc(sizeof(LoginBannerCtx));
    if (!ctx) return;
    ctx->ls = ls;
    ctx->gen = g_login_banner_gen;
    ls->banner_loading = true;
    platform_load_file(PW_SITE_ORIGIN "/api/banner.php", login_banner_json_on_loaded, ctx);
}

static void login_banner_tone_cols(const LoginScreen* ls,
                                  float* br, float* bg, float* bb,
                                  float* tr, float* tg, float* tb) {
    int tone = 0;
    if (ls && ls->banner_tone[0] == 's') tone = 1;
    else if (ls && ls->banner_tone[0] == 'w') tone = 2;
    bool dark = ui_theme_is_dark();
    if (tone == 1) {
        if (dark) { *br = 0x1a / 255.0f; *bg = 0x2e / 255.0f; *bb = 0x20 / 255.0f;
                    *tr = 0x81 / 255.0f; *tg = 0xc9 / 255.0f; *tb = 0x95 / 255.0f; }
        else      { *br = 0xe6 / 255.0f; *bg = 0xf4 / 255.0f; *bb = 0xea / 255.0f;
                    *tr = 0x13 / 255.0f; *tg = 0x73 / 255.0f; *tb = 0x33 / 255.0f; }
    } else if (tone == 2) {
        if (dark) { *br = 0x2e / 255.0f; *bg = 0x28 / 255.0f; *bb = 0x14 / 255.0f;
                    *tr = 0xfd / 255.0f; *tg = 0xd6 / 255.0f; *tb = 0x63 / 255.0f; }
        else      { *br = 0xfe / 255.0f; *bg = 0xf7 / 255.0f; *bb = 0xe0 / 255.0f;
                    *tr = 0x8a / 255.0f; *tg = 0x6d / 255.0f; *tb = 0x00 / 255.0f; }
    } else {
        if (dark) { *br = 0x1a / 255.0f; *bg = 0x27 / 255.0f; *bb = 0x40 / 255.0f;
                    *tr = 0x8a / 255.0f; *tg = 0xb4 / 255.0f; *tb = 0xf8 / 255.0f; }
        else      { *br = 0xe8 / 255.0f; *bg = 0xf0 / 255.0f; *bb = 0xfe / 255.0f;
                    *tr = 0x17 / 255.0f; *tg = 0x4e / 255.0f; *tb = 0xa6 / 255.0f; }
    }
}

static void login_ellipsis_to_width(char* dst, int dst_max, const char* src, float max_w) {
    if (!dst || dst_max < 4) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;
    if (measure_text_small(src) <= max_w) {
        snprintf(dst, dst_max, "%s", src);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", src);
    int n = (int)strlen(buf);
    while (n > 0) {
        buf[n] = '\0';
        char shown[260];
        snprintf(shown, sizeof(shown), "%s...", buf);
        if (measure_text_small(shown) <= max_w || n == 1) {
            snprintf(dst, dst_max, "%s", shown);
            return;
        }
        n--;
        while (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) n--; // UTF-8 trail
    }
    snprintf(dst, dst_max, "...");
}

static void draw_site_banner(const LoginScreen* ls, int vwidth, int vheight) {
    if (!login_banner_showing(ls)) return;
    float bh = SITE_BANNER_H;
    float by = NAV_H;
    float br, bg, bb, tr, tg, tb;
    login_banner_tone_cols(ls, &br, &bg, &bb, &tr, &tg, &tb);
    draw_rect_img(0.0f, by, (float)IMG_W, bh, br, bg, bb);
    draw_rect_img(0.0f, by + bh - 1.0f, (float)IMG_W, 1.0f, COL_LINE_R, COL_LINE_G, COL_LINE_B);

    float icon_h = 28.0f;
    float icon_w = 0.0f;
    bool have_icon = ls->banner_icon_loaded && ls->banner_icon_tex;
    if (have_icon) {
        icon_w = icon_h;
        if (ls->banner_icon_w > 0 && ls->banner_icon_h > 0) {
            icon_w = icon_h * (float)ls->banner_icon_w / (float)ls->banner_icon_h;
            if (icon_w > 52.0f) {
                icon_w = 52.0f;
                icon_h = icon_w * (float)ls->banner_icon_h / (float)ls->banner_icon_w;
                if (icon_h > 28.0f) {
                    icon_h = 28.0f;
                    icon_w = icon_h * (float)ls->banner_icon_w / (float)ls->banner_icon_h;
                }
            }
        }
    }

    float gap = have_icon ? 8.0f : 0.0f;
    float max_text = (float)IMG_W - 40.0f - icon_w - gap;
    if (max_text < 80.0f) max_text = 80.0f;
    char shown[256];
    login_ellipsis_to_width(shown, sizeof(shown), ls->banner_message, max_text);
    float text_w = measure_text_small(shown);
    float total = icon_w + gap + text_w;
    float x = ((float)IMG_W - total) * 0.5f;
    if (x < 16.0f) x = 16.0f;
    if (have_icon) {
        float iy = by + (bh - icon_h) * 0.5f;
        draw_tex_img(ls->banner_icon_tex, x, iy, icon_w, icon_h);
        x += icon_w + gap;
    }
    float ty = by + (bh - TXT_SMALL_H) * 0.5f - 1.0f;
    draw_text_small_scaled(shown, x, ty, tr, tg, tb, vwidth, vheight);
}

static bool login_banner_hit(const LoginScreen* ls, float x, float y) {
    (void)x;
    if (!login_banner_showing(ls)) return false;
    return y >= NAV_H && y < NAV_H + SITE_BANNER_H;
}

static unsigned int login_tex_from_memory(const uint8_t* data, size_t len, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!data || len < 32 || len > 4u * 1024u * 1024u) return 0;
    int tw, th_img, tc;
    unsigned char* tpx = stbi_load_from_memory(data, (int)len, &tw, &th_img, &tc, 4);
    if (!tpx) return 0;
    GLint gl_max = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max);
    if (gl_max < 64) gl_max = 2048;
    int cap = gl_max < 2048 ? gl_max : 2048;
    if (tw < 1 || th_img < 1 || tw > cap || th_img > cap) {
        stbi_image_free(tpx);
        return 0;
    }
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th_img, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(tpx);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th_img;
    return tex;
}

static unsigned int login_load_texture_url(const char* url, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!url || !url[0]) return 0;
    size_t tlen = 0;
    unsigned char* tbuf = platform_http_get(url, &tlen);
    if (!tbuf || tlen < 32) {
        free(tbuf);
        return 0;
    }
    unsigned int tex = login_tex_from_memory(tbuf, tlen, out_w, out_h);
    free(tbuf);
    return tex;
}

enum {
    LOGIN_THUMB_GAME = 0,
    LOGIN_THUMB_CONTINUE = 1,
    LOGIN_THUMB_GOTW = 2,
    LOGIN_THUMB_FRIEND = 3
};

typedef struct {
    LoginScreen* ls;
    int kind;
    int index;
    unsigned gen;
} LoginThumbCtx;

static void login_thumb_apply_placeholder(LoginScreen* ls, int kind, int index) {
    int tw = 0, thh = 0;
    unsigned int tex = login_load_texture_file("assets/placeholder.png", &tw, &thh);
    if (!tex) tex = login_load_texture_file("website/assets/images/placeholder.png", &tw, &thh);
    if (!tex || !ls) return;
    if (kind == LOGIN_THUMB_GAME && index >= 0 && index < ls->game_count) {
        ls->games[index].thumb_tex = tex;
        ls->games[index].thumb_w = tw;
        ls->games[index].thumb_h = thh;
    } else if (kind == LOGIN_THUMB_CONTINUE && index >= 0 && index < ls->continue_count) {
        ls->continue_games[index].thumb_tex = tex;
        ls->continue_games[index].thumb_w = tw;
        ls->continue_games[index].thumb_h = thh;
    } else if (kind == LOGIN_THUMB_GOTW && index >= 0 && index < ls->gotw_count) {
        ls->gotw_games[index].thumb_tex = tex;
        ls->gotw_games[index].thumb_w = tw;
        ls->gotw_games[index].thumb_h = thh;
    }
}

static void login_thumb_on_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    LoginThumbCtx* ctx = (LoginThumbCtx*)user;
    if (!ctx) return;
    LoginScreen* ls = ctx->ls;
    int kind = ctx->kind;
    int index = ctx->index;
    unsigned gen = ctx->gen;
    free(ctx);
    if (g_login_thumbs_inflight > 0) g_login_thumbs_inflight--;
    (void)path;
    if (!ls || gen != g_login_thumb_gen) return;

    int tw = 0, thh = 0;
    unsigned int tex = login_tex_from_memory(data, len, &tw, &thh);

    if (kind == LOGIN_THUMB_GAME && index >= 0 && index < ls->game_count) {
        ls->games[index].thumb_loading = false;
        ls->games[index].thumb_loaded = true;
        if (tex) {
            ls->games[index].thumb_tex = tex;
            ls->games[index].thumb_w = tw;
            ls->games[index].thumb_h = thh;
        } else {
            login_thumb_apply_placeholder(ls, kind, index);
        }
    } else if (kind == LOGIN_THUMB_CONTINUE && index >= 0 && index < ls->continue_count) {
        ls->continue_games[index].thumb_loading = false;
        ls->continue_games[index].thumb_loaded = true;
        if (tex) {
            ls->continue_games[index].thumb_tex = tex;
            ls->continue_games[index].thumb_w = tw;
            ls->continue_games[index].thumb_h = thh;
        } else {
            login_thumb_apply_placeholder(ls, kind, index);
        }
    } else if (kind == LOGIN_THUMB_GOTW && index >= 0 && index < ls->gotw_count) {
        ls->gotw_games[index].thumb_loading = false;
        ls->gotw_games[index].thumb_loaded = true;
        if (tex) {
            ls->gotw_games[index].thumb_tex = tex;
            ls->gotw_games[index].thumb_w = tw;
            ls->gotw_games[index].thumb_h = thh;
        } else {
            login_thumb_apply_placeholder(ls, kind, index);
        }
    } else if (kind == LOGIN_THUMB_FRIEND && index >= 0 && index < ls->friend_count) {
        ls->friends[index].avatar_loading = false;
        ls->friends[index].avatar_loaded = true;
        if (tex) {
            ls->friends[index].avatar_tex = tex;
            ls->friends[index].avatar_w = tw;
            ls->friends[index].avatar_h = thh;
        }
    } else if (tex) {
        glDeleteTextures(1, &tex);
    }
}

static bool login_thumb_start_url(LoginScreen* ls, int kind, int index, const char* url) {
    if (!ls || !url || !url[0]) return false;
    if (g_login_thumbs_inflight >= LOGIN_THUMB_MAX_INFLIGHT) return false;
    LoginThumbCtx* ctx = (LoginThumbCtx*)malloc(sizeof(LoginThumbCtx));
    if (!ctx) return false;
    ctx->ls = ls;
    ctx->kind = kind;
    ctx->index = index;
    ctx->gen = g_login_thumb_gen;
    g_login_thumbs_inflight++;
    if (kind == LOGIN_THUMB_GAME) ls->games[index].thumb_loading = true;
    else if (kind == LOGIN_THUMB_CONTINUE) ls->continue_games[index].thumb_loading = true;
    else if (kind == LOGIN_THUMB_GOTW) ls->gotw_games[index].thumb_loading = true;
    else if (kind == LOGIN_THUMB_FRIEND) ls->friends[index].avatar_loading = true;
    platform_load_file(url, login_thumb_on_loaded, ctx);
    return true;
}

static int login_thumb_res_p(void) {
    static int cached = 0;
    if (cached > 0) return cached;
    GLint gl_max = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max);
    int cap = 2048;
    if (gl_max > 0 && gl_max < cap) cap = gl_max;
    int want = 720;
    if (want > cap) want = cap;
    if (want < 64) want = 64;
    cached = want;
    return cached;
}

static int login_thumb_game_id(int id, const char* th) {
    if (id > 0) return id;
    if (!th || !th[0]) return 0;
    const char* p = strstr(th, "/thumbnails/");
    if (p) {
        p += 12;
        int n = atoi(p);
        if (n > 0) return n;
    }
    p = strstr(th, "thumbnail.php?");
    if (p) {
        const char* idp = strstr(p, "id=");
        if (idp) {
            int n = atoi(idp + 3);
            if (n > 0) return n;
        }
    }
    if (th[0] >= '1' && th[0] <= '9') {
        int n = atoi(th);
        if (n > 0) return n;
    }
    return 0;
}

static bool login_resolve_thumb_url(const char* th, int game_id, char* out, size_t out_sz) {
    if (!out || out_sz < 16) return false;
    if (th && (strcmp(th, "placeholder.png") == 0 || strcmp(th, "null") == 0))
        return false;
    int id = login_thumb_game_id(game_id, th);
    if (id > 0) {
        snprintf(out, out_sz, PW_THUMBNAIL_API_URL "?id=%d&res=%dp", id, login_thumb_res_p());
        return true;
    }
    if (th && th[0] && (strncmp(th, "http://", 7) == 0 || strncmp(th, "https://", 8) == 0)) {
        snprintf(out, out_sz, "%s", th);
        if (strstr(out, ":21212")) {
            char alt[320];
            const char* p = strstr(out, ":21212");
            snprintf(alt, sizeof(alt), "%.*s%s", (int)(p - out), out, p + 6);
            snprintf(out, out_sz, "%s", alt);
        }
        return true;
    }
    if (th && th[0]) {
        snprintf(out, out_sz, PW_SITE_ORIGIN "/uploads/thumbnails/%s", th);
        return true;
    }
    return false;
}

typedef struct { uint8_t* data; size_t len; } LoginFileBuf;
static void on_login_file(const char* path, const uint8_t* data, size_t len, void* user) {
    LoginFileBuf* b = (LoginFileBuf*)user; (void)path;
    if (!b || !data || !len) return;
    b->data = (uint8_t*)malloc(len);
    if (!b->data) return;
    memcpy(b->data, data, len);
    b->len = len;
}

static unsigned int login_load_texture_file(const char* path, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    LoginFileBuf buf = {0};
    platform_load_file(path, on_login_file, &buf);
    if (!buf.data) return 0;
    size_t nread = buf.len;
    unsigned char* raw = buf.data;
    int tw, th_img, tc;
    unsigned char* tpx = stbi_load_from_memory(raw, (int)nread, &tw, &th_img, &tc, 4);
    free(raw);
    if (!tpx) return 0;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th_img, 0, GL_RGBA, GL_UNSIGNED_BYTE, tpx);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(tpx);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th_img;
    return tex;
}

// Loads thumbnail by game id (sized API) or name/url fallback.
static unsigned int login_load_thumb_by_name(const char* th, int game_id, int* out_w, int* out_h) {
    unsigned int tex = 0;
    int tw = 0, thh = 0;
    char url[320];
    if (login_resolve_thumb_url(th, game_id, url, sizeof(url)))
        tex = login_load_texture_url(url, &tw, &thh);
    if (!tex) {
        tex = login_load_texture_file("assets/placeholder.png", &tw, &thh);
        if (!tex) tex = login_load_texture_file("website/assets/images/placeholder.png", &tw, &thh);
    }
    if (out_w) *out_w = tw;
    if (out_h) *out_h = thh;
    return tex;
}

// #rrggbb to 0-1 floats!
static void parse_hex_color(const char* hex, float* r, float* g, float* b) {
    float rr = 0.6f, gg = 0.6f, bb = 0.6f;
    if (hex && hex[0] == '#' && strlen(hex) >= 7) {
        unsigned int v = 0;
        if (sscanf(hex + 1, "%6x", &v) == 1) {
            rr = ((v >> 16) & 0xFF) / 255.0f;
            gg = ((v >> 8) & 0xFF) / 255.0f;
            bb = (v & 0xFF) / 255.0f;
        }
    }
    if (r) *r = rr;
    if (g) *g = gg;
    if (b) *b = bb;
}

static void friend_status_color(const char* status, float* r, float* g, float* b) {
    float rr = 0.741f, gg = 0.741f, bb = 0.741f; // offline #bdbdbd
    if (status) {
        if (strcmp(status, "online") == 0) { rr = 0.180f; gg = 0.800f; bb = 0.443f; } // #2ecc71
        else if (strcmp(status, "playing") == 0) { rr = COL_PLAYING_R; gg = COL_PLAYING_G; bb = COL_PLAYING_B; } // #3498db
        else if (strcmp(status, "developing") == 0) { rr = 0.945f; gg = 0.769f; bb = 0.059f; } // #f1c40f
    }
    if (r) *r = rr;
    if (g) *g = gg;
    if (b) *b = bb;
}

static void login_load_offline_places(LoginScreen* ls) {
    login_clear_game_thumbs(ls);
                ls->game_count = 0;
    ls->selected_game = 0;
    ls->sel_draw_valid = false;
    ls->error[0] = '\0';

    typedef struct {
        int id;
        const char* title;
        const char* desc;
        const char* path;
    } OfflinePlace;
    static const OfflinePlace places[] = {
        { -1, "Blank Baseplate",
          "Absolutely empty baseplate.",
          "assets/places/blank.xml" },
        { -2, "Flatgrass",
          "Giant field.",
          "assets/places/flatgrass.xml" },
        { -3, "Obby",
          "Short obby...",
          "assets/places/obby.xml" },
        { -4, "Park",
          "A park with some trees and benches... And a pathway.",
          "assets/places/park.xml" },
        { -5, "Script guide but no moving stuff",
          "Look dude, I'm running out of ideas for offline places!",
          "assets/places/demoplace.xml" },
        { -6, "Evil ### Jail",
          "This is actually where banned guys are sent to. But, no banned guys here dont worry!!!",
          "assets/places/sandbox.xml" },
    };

    for (int i = 0; i < (int)(sizeof(places) / sizeof(places[0])) && ls->game_count < LOGIN_MAX_GAMES; i++) {
        LoginFileBuf probe = {0};
        platform_load_file(places[i].path, on_login_file, &probe);
        if (!probe.data) {
            continue;
        }
        free(probe.data);
        int gi = ls->game_count++;
        memset(&ls->games[gi], 0, sizeof(ls->games[gi]));
        ls->games[gi].id = places[i].id;
        strncpy(ls->games[gi].title, places[i].title, sizeof(ls->games[gi].title) - 1);
        strncpy(ls->games[gi].creator, "PolyWorld", sizeof(ls->games[gi].creator) - 1);
        strncpy(ls->games[gi].description, places[i].desc, sizeof(ls->games[gi].description) - 1);
        strncpy(ls->games[gi].local_path, places[i].path, sizeof(ls->games[gi].local_path) - 1);
        ls->games[gi].plays = 0;
        ls->games[gi].playing_now = 0;
    }
    if (ls->game_count == 0) {
        snprintf(ls->error, sizeof(ls->error), "No offline places found in assets/places/.");
    }
}

static void login_fetch_games(LoginScreen* ls) {
    login_clear_game_thumbs(ls);
    ls->game_count = 0;
    ls->selected_game = 0;
    ls->sel_draw_valid = false;
    ls->error[0] = '\0';

    if (games_tab_is_offline(ls)) {
        login_load_offline_places(ls);
        return;
    }

    char url[512];
    if (games_tab_is_my_games(ls)) {
        snprintf(url, sizeof(url),
            "%s/api/games.php?action=my_list&session_token=%s",
            pw_site_origin(), ls->session_token);
    } else {
        if (ls->games_tab < 0 || ls->games_tab > 3) ls->games_tab = 0;
        const char* sort = games_tab_sort(ls->games_tab);
        if (ls->session_token[0] && strcmp(sort, "recommended") == 0) {
            snprintf(url, sizeof(url),
                "%s/api/games.php?action=public_list&sort=recommended&session_token=%s",
                pw_site_origin(), ls->session_token);
        } else {
            snprintf(url, sizeof(url),
                "%s/api/games.php?action=public_list&sort=%s",
                pw_site_origin(), sort);
        }
    }

    size_t resp_len = 0;
    char* resp = (char*)platform_http_get(url, &resp_len);
    if (!resp || resp_len == 0) {
        const char* err = platform_http_last_error();
        if (err && err[0]) {
            snprintf(ls->error, sizeof(ls->error), "Network error: %s", err);
        } else {
            snprintf(ls->error, sizeof(ls->error),
                     "Could not reach games API. Check network / TLS.");
        }
        return;
    }
    if (resp && resp_len > 0) {
                const char* cur = strstr(resp, "\"games\"");
                if (cur) cur = strchr(cur, '[');
        while (cur && ls->game_count < LOGIN_MAX_GAMES) {
                    const char* id_key = strstr(cur, "\"id\"");
                    if (!id_key) break;
                    const char* colon = strchr(id_key + 4, ':');
                    if (!colon) break;
                    colon++;
                    while (*colon == ' ' || *colon == '"') colon++;
                    ls->games[ls->game_count].id = atoi(colon);
            ls->games[ls->game_count].thumbnail[0] = '\0';
            ls->games[ls->game_count].local_path[0] = '\0';
            ls->games[ls->game_count].playing_now = 0;
            ls->games[ls->game_count].likes = 0;
            ls->games[ls->game_count].dislikes = 0;
            ls->games[ls->game_count].plays = 0;
            ls->games[ls->game_count].title[0] = '\0';
            ls->games[ls->game_count].creator[0] = '\0';
            ls->games[ls->game_count].created_at[0] = '\0';

                    const char* title_key = strstr(id_key, "\"title\"");
                    if (title_key) {
                        const char* tc = strchr(title_key + 7, ':');
                        if (tc) { tc++; while (*tc == ' ' || *tc == '"') tc++; }
                if (tc) json_copy_string(tc, ls->games[ls->game_count].title, 64);
                        }
                    const char* cr_key = strstr(id_key, "\"creator\"");
                    if (cr_key) {
                        const char* cc = strchr(cr_key + 9, ':');
                        if (cc) { cc++; while (*cc == ' ' || *cc == '"') cc++; }
                if (cc) json_copy_string(cc, ls->games[ls->game_count].creator, 32);
            }
            const char* tu_key = strstr(id_key, "\"thumbnail_url\"");
            const char* obj_end = strchr(colon, '}');
            if (tu_key && (!obj_end || tu_key < obj_end)) {
                const char* tc = strchr(tu_key + 14, ':');
                if (tc) { tc++; while (*tc == ' ' || *tc == '"') tc++; }
                if (tc) json_copy_string(tc, ls->games[ls->game_count].thumbnail, 256);
            }
            if (!ls->games[ls->game_count].thumbnail[0]) {
                const char* th_key = strstr(id_key, "\"thumbnail\"");
                if (th_key && (!obj_end || th_key < obj_end) && th_key[11] != '_') {
                    const char* tc = strchr(th_key + 11, ':');
                    if (tc) {
                        tc++;
                        while (*tc == ' ' || *tc == '\t') tc++;
                        if (*tc == '"') {
                            tc++;
                            json_copy_string(tc, ls->games[ls->game_count].thumbnail, 256);
                        }
                    }
                }
            }
                    const char* plays_key = strstr(id_key, "\"plays\"");
            if (plays_key && (!obj_end || plays_key < obj_end)) {
                        const char* pc = strchr(plays_key + 7, ':');
                        if (pc) { pc++; while (*pc == ' ' || *pc == '"') pc++; }
                        if (pc) ls->games[ls->game_count].plays = atoi(pc);
                    }
            const char* pn_key = strstr(id_key, "\"playing_now\"");
            if (pn_key && (!obj_end || pn_key < obj_end)) {
                const char* pc = strchr(pn_key + 13, ':');
                if (pc) { pc++; while (*pc == ' ' || *pc == '"') pc++; }
                if (pc) ls->games[ls->game_count].playing_now = atoi(pc);
            }
                    const char* likes_key = strstr(id_key, "\"likes\"");
            if (likes_key && (!obj_end || likes_key < obj_end)) {
                        const char* lc = strchr(likes_key + 7, ':');
                        if (lc) { lc++; while (*lc == ' ' || *lc == '"') lc++; }
                        if (lc) ls->games[ls->game_count].likes = atoi(lc);
                    }
                    const char* dis_key = strstr(id_key, "\"dislikes\"");
            if (dis_key && (!obj_end || dis_key < obj_end)) {
                        const char* dc = strchr(dis_key + 10, ':');
                        if (dc) { dc++; while (*dc == ' ' || *dc == '"') dc++; }
                        if (dc) ls->games[ls->game_count].dislikes = atoi(dc);
                    }
                    const char* date_key = strstr(id_key, "\"created_at\"");
            if (date_key && (!obj_end || date_key < obj_end)) {
                        const char* dtc = strchr(date_key + 12, ':');
                        if (dtc) { dtc++; while (*dtc == ' ' || *dtc == '"') dtc++; }
                        if (dtc) {
                            int di = 0;
                            while (*dtc && *dtc != '"' && di < 31) ls->games[ls->game_count].created_at[di++] = *dtc++;
                            ls->games[ls->game_count].created_at[di] = '\0';
                        }
                    }
                    ls->game_count++;
                    cur = strchr(colon, '}');
                    if (cur) cur++;
                }
    }
    free(resp);

    // Async thumbnails
    g_login_thumb_gen++;
    g_login_thumbs_inflight = 0;
    for (int gi = 0; gi < ls->game_count; gi++) {
        ls->games[gi].local_path[0] = '\0';
        ls->games[gi].description[0] = '\0';
        ls->games[gi].user_rating = 0;
        ls->games[gi].thumb_w = 0;
        ls->games[gi].thumb_h = 0;
        ls->games[gi].thumb_tex = 0;
        ls->games[gi].thumb_loaded = false;
        ls->games[gi].thumb_loading = false;
    }
}

// Async thumbnail loading
static bool login_load_one_game_thumb(LoginScreen* ls, int gi) {
    if (!ls || gi < 0 || gi >= ls->game_count) return false;
    if (ls->games[gi].thumb_loaded || ls->games[gi].thumb_loading) return false;
    const char* th = ls->games[gi].thumbnail;
    char url[320];
    if (login_resolve_thumb_url(th, ls->games[gi].id, url, sizeof(url))) {
        return login_thumb_start_url(ls, LOGIN_THUMB_GAME, gi, url);
    }
    // Placeholder!
    int tw = 0, thh = 0;
    unsigned int tex = login_load_texture_file("assets/placeholder.png", &tw, &thh);
    if (!tex) tex = login_load_texture_file("website/assets/images/placeholder.png", &tw, &thh);
    ls->games[gi].thumb_loaded = true;
    if (tex) {
        ls->games[gi].thumb_tex = tex;
        ls->games[gi].thumb_w = tw;
        ls->games[gi].thumb_h = thh;
    }
    return true;
}

// More async
static void login_pump_thumbs(LoginScreen* ls) {
    if (!ls || ls->phase != 1) return;

    for (int gi = 0; gi < ls->game_count; gi++) {
        if (login_load_one_game_thumb(ls, gi)) {
            if (g_login_thumbs_inflight >= LOGIN_THUMB_MAX_INFLIGHT) return;
        }
    }
    for (int i = 0; i < ls->continue_count; i++) {
        if (ls->continue_games[i].thumb_loaded || ls->continue_games[i].thumb_loading) continue;
        char url[320];
        if (login_resolve_thumb_url(ls->continue_games[i].thumbnail, ls->continue_games[i].id, url, sizeof(url))) {
            if (login_thumb_start_url(ls, LOGIN_THUMB_CONTINUE, i, url)) {
                if (g_login_thumbs_inflight >= LOGIN_THUMB_MAX_INFLIGHT) return;
            }
            continue;
        }
        ls->continue_games[i].thumb_loaded = true;
        login_thumb_apply_placeholder(ls, LOGIN_THUMB_CONTINUE, i);
        return;
    }
    for (int i = 0; i < ls->gotw_count; i++) {
        if (ls->gotw_games[i].thumb_loaded || ls->gotw_games[i].thumb_loading) continue;
        char url[320];
        if (login_resolve_thumb_url(ls->gotw_games[i].thumbnail, ls->gotw_games[i].id, url, sizeof(url))) {
            if (login_thumb_start_url(ls, LOGIN_THUMB_GOTW, i, url)) {
                if (g_login_thumbs_inflight >= LOGIN_THUMB_MAX_INFLIGHT) return;
            }
            continue;
        }
        ls->gotw_games[i].thumb_loaded = true;
        login_thumb_apply_placeholder(ls, LOGIN_THUMB_GOTW, i);
        return;
    }
    for (int fi = 0; fi < ls->friend_count; fi++) {
        if (ls->friends[fi].avatar_loaded || ls->friends[fi].avatar_loading || ls->friends[fi].id <= 0)
            continue;
        char aurl[256];
        snprintf(aurl, sizeof(aurl),
            "%s/uploads/avatars/%d.png", pw_site_origin(), ls->friends[fi].id);
        if (login_thumb_start_url(ls, LOGIN_THUMB_FRIEND, fi, aurl)) {
            if (g_login_thumbs_inflight >= LOGIN_THUMB_MAX_INFLIGHT) return;
        }
    }
}

static void login_clear_home_thumbs(LoginScreen* ls) {
    g_login_thumb_gen++;
    g_login_thumbs_inflight = 0;
    for (int i = 0; i < 16; i++) {
        if (ls->friends[i].avatar_loaded && ls->friends[i].avatar_tex)
            glDeleteTextures(1, &ls->friends[i].avatar_tex);
        ls->friends[i].avatar_tex = 0;
        ls->friends[i].avatar_loaded = false;
        ls->friends[i].avatar_loading = false;
    }
    for (int i = 0; i < 8; i++) {
        if (ls->continue_games[i].thumb_loaded && ls->continue_games[i].thumb_tex)
            glDeleteTextures(1, &ls->continue_games[i].thumb_tex);
        ls->continue_games[i].thumb_tex = 0;
        ls->continue_games[i].thumb_loaded = false;
        ls->continue_games[i].thumb_loading = false;
        if (ls->gotw_games[i].thumb_loaded && ls->gotw_games[i].thumb_tex)
            glDeleteTextures(1, &ls->gotw_games[i].thumb_tex);
        ls->gotw_games[i].thumb_tex = 0;
        ls->gotw_games[i].thumb_loaded = false;
        ls->gotw_games[i].thumb_loading = false;
    }
}

static void login_perform_logout(LoginScreen* ls) {
    auth_clear_session();
    ls->session_token[0] = '\0';
    ls->logged_in = false;
    ls->user_id = 0;
    ls->ticket[0] = '\0';
    ls->ticket_username[0] = '\0';
    ls->username[0] = '\0';
    ls->username_len = 0;
    ls->password[0] = '\0';
    ls->password_len = 0;
    ls->error[0] = '\0';
    ls->awaiting_2fa = false;
    ls->totp_challenge[0] = '\0';
    ls->phase = 0;
    ls->logout_confirm = false;
    ls->games_fetched = false;
    ls->games_loading = false;
    ls->home_fetched = false;
    ls->home_loading = false;
    login_clear_home_thumbs(ls);
    ls->game_count = 0;
    ls->friend_count = 0;
    ls->continue_count = 0;
    ls->gotw_count = 0;
}

// Logout confirm
static void logout_confirm_layout(float* panel_x, float* panel_y, float* panel_w, float* panel_h,
                                  float* cancel_x, float* yes_x, float* btn_y,
                                  float* btn_w, float* btn_h) {
    *panel_w = 380.0f;
    *panel_h = 168.0f;
    *panel_x = ((float)IMG_W - *panel_w) * 0.5f;
    *panel_y = ((float)IMG_H - *panel_h) * 0.5f;
    *btn_w = 130.0f;
    *btn_h = 40.0f;
    float gap = 16.0f;
    float row_w = *btn_w * 2.0f + gap;
    *cancel_x = *panel_x + (*panel_w - row_w) * 0.5f;
    *yes_x = *cancel_x + *btn_w + gap;
    *btn_y = *panel_y + *panel_h - 56.0f;
}

// All info stuff for logged in users.
static void login_fetch_home(LoginScreen* ls) {
    login_clear_home_thumbs(ls);
    ls->friend_count = 0;
    ls->continue_count = 0;
    ls->gotw_count = 0;
    if (!ls->session_token[0]) return;

    // Friends
    {
        char url[512];
        snprintf(url, sizeof(url),
            "%s/api/friends.php?action=list&session_token=%s",
            pw_site_origin(), ls->session_token);
        size_t resp_len = 0;
        char* resp = (char*)platform_http_get(url, &resp_len);
        if (resp && resp_len > 0) {
            const char* cur = strstr(resp, "\"friends\"");
            if (cur) cur = strchr(cur, '[');
            while (cur && ls->friend_count < 16) {
                const char* id_key = strstr(cur, "\"id\"");
                if (!id_key) break;
                const char* colon = strchr(id_key + 4, ':');
                if (!colon) break;
                colon++;
                while (*colon == ' ' || *colon == '"') colon++;
                const char* obj_end = strchr(colon, '}');
                int fi = ls->friend_count;
                memset(&ls->friends[fi], 0, sizeof(ls->friends[fi]));
                ls->friends[fi].id = atoi(colon);

                const char* un_key = strstr(id_key, "\"username\"");
                if (un_key && (!obj_end || un_key < obj_end)) {
                    const char* c = strchr(un_key + 10, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->friends[fi].username, 32); }
                }
                const char* st_key = strstr(id_key, "\"status\"");
                if (st_key && (!obj_end || st_key < obj_end)) {
                    const char* c = strchr(st_key + 8, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->friends[fi].status, 16); }
                }
                const char* ac_key = strstr(id_key, "\"avatar_color\"");
                if (ac_key && (!obj_end || ac_key < obj_end)) {
                    const char* c = strchr(ac_key + 14, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->friends[fi].avatar_color, 8); }
                }
                const char* cg_key = strstr(id_key, "\"current_game_id\"");
                if (cg_key && (!obj_end || cg_key < obj_end)) {
                    const char* c = strchr(cg_key + 18, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->friends[fi].current_game_id = atoi(c); }
                }
                const char* cs_key = strstr(id_key, "\"current_server_id\"");
                if (cs_key && (!obj_end || cs_key < obj_end)) {
                    const char* c = strchr(cs_key + 20, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->friends[fi].current_server_id = atoi(c); }
                }
                const char* pt_key = strstr(id_key, "\"playing_title\"");
                if (pt_key && (!obj_end || pt_key < obj_end)) {
                    const char* c = strchr(pt_key + 16, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->friends[fi].playing_title, 64); }
                }
                ls->friend_count++;
                cur = strchr(colon, '}');
                if (cur) cur++;
            }
        }
        free(resp);
    }

    {
        char url[512];
        snprintf(url, sizeof(url),
            "%s/api/games.php?action=continue&session_token=%s",
            pw_site_origin(), ls->session_token);
        size_t resp_len = 0;
        char* resp = (char*)platform_http_get(url, &resp_len);
        if (resp && resp_len > 0) {
            const char* cur = strstr(resp, "\"games\"");
            if (cur) cur = strchr(cur, '[');
            while (cur && ls->continue_count < 8) {
                const char* id_key = strstr(cur, "\"id\"");
                if (!id_key) break;
                const char* colon = strchr(id_key + 4, ':');
                if (!colon) break;
                colon++;
                while (*colon == ' ' || *colon == '"') colon++;
                const char* obj_end = strchr(colon, '}');
                int gi = ls->continue_count;
                memset(&ls->continue_games[gi], 0, sizeof(ls->continue_games[gi]));
                ls->continue_games[gi].id = atoi(colon);

                const char* title_key = strstr(id_key, "\"title\"");
                if (title_key && (!obj_end || title_key < obj_end)) {
                    const char* c = strchr(title_key + 7, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->continue_games[gi].title, 64); }
                }
                const char* cr_key = strstr(id_key, "\"creator\"");
                if (cr_key && (!obj_end || cr_key < obj_end)) {
                    const char* c = strchr(cr_key + 9, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->continue_games[gi].creator, 32); }
                }
                const char* tu_key = strstr(id_key, "\"thumbnail_url\"");
                if (tu_key && (!obj_end || tu_key < obj_end)) {
                    const char* c = strchr(tu_key + 14, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->continue_games[gi].thumbnail, 256); }
                }
                const char* plays_key = strstr(id_key, "\"plays\"");
                if (plays_key && (!obj_end || plays_key < obj_end)) {
                    const char* c = strchr(plays_key + 7, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->continue_games[gi].plays = atoi(c); }
                }
                const char* pn_key = strstr(id_key, "\"playing_now\"");
                if (pn_key && (!obj_end || pn_key < obj_end)) {
                    const char* c = strchr(pn_key + 13, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->continue_games[gi].playing_now = atoi(c); }
                }
                ls->continue_count++;
                cur = strchr(colon, '}');
                if (cur) cur++;
            }
        }
        free(resp);
    }

    {
        char url[256];
        snprintf(url, sizeof(url), "%s/api/games.php?action=gotw_winners", pw_site_origin());
        size_t resp_len = 0;
        char* resp = (char*)platform_http_get(url, &resp_len);
        if (resp && resp_len > 0) {
            const char* cur = strstr(resp, "\"games\"");
            if (cur) cur = strchr(cur, '[');
            while (cur && ls->gotw_count < 8) {
                const char* id_key = strstr(cur, "\"id\"");
                if (!id_key) break;
                const char* colon = strchr(id_key + 4, ':');
                if (!colon) break;
                colon++;
                while (*colon == ' ' || *colon == '"') colon++;
                const char* obj_end = strchr(colon, '}');
                int gi = ls->gotw_count;
                memset(&ls->gotw_games[gi], 0, sizeof(ls->gotw_games[gi]));
                ls->gotw_games[gi].id = atoi(colon);

                const char* title_key = strstr(id_key, "\"title\"");
                if (title_key && (!obj_end || title_key < obj_end)) {
                    const char* c = strchr(title_key + 7, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->gotw_games[gi].title, 64); }
                }
                const char* cr_key = strstr(id_key, "\"creator\"");
                if (cr_key && (!obj_end || cr_key < obj_end)) {
                    const char* c = strchr(cr_key + 9, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->gotw_games[gi].creator, 32); }
                }
                const char* tu_key = strstr(id_key, "\"thumbnail_url\"");
                if (tu_key && (!obj_end || tu_key < obj_end)) {
                    const char* c = strchr(tu_key + 14, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; json_copy_string(c, ls->gotw_games[gi].thumbnail, 256); }
                }
                const char* plays_key = strstr(id_key, "\"plays\"");
                if (plays_key && (!obj_end || plays_key < obj_end)) {
                    const char* c = strchr(plays_key + 7, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->gotw_games[gi].plays = atoi(c); }
                }
                const char* pn_key = strstr(id_key, "\"playing_now\"");
                if (pn_key && (!obj_end || pn_key < obj_end)) {
                    const char* c = strchr(pn_key + 13, ':');
                    if (c) { c++; while (*c == ' ' || *c == '"') c++; ls->gotw_games[gi].playing_now = atoi(c); }
                }
                ls->gotw_count++;
                cur = strchr(colon, '}');
                if (cur) cur++;
            }
        }
        free(resp);
    }
}

static void login_open_game_ref(LoginScreen* ls, int id, const char* title,
                                const char* creator, const char* thumbnail) {
    int idx = -1;
    for (int i = 0; i < ls->game_count; i++) {
        if (ls->games[i].id == id) { idx = i; break; }
    }
    if (idx < 0) {
        idx = (ls->game_count < LOGIN_MAX_GAMES) ? ls->game_count++ : ls->game_count - 1;
        memset(&ls->games[idx], 0, sizeof(ls->games[idx]));
        ls->games[idx].id = id;
        if (title) strncpy(ls->games[idx].title, title, sizeof(ls->games[idx].title) - 1);
        if (creator) strncpy(ls->games[idx].creator, creator, sizeof(ls->games[idx].creator) - 1);
        if (thumbnail) strncpy(ls->games[idx].thumbnail, thumbnail, sizeof(ls->games[idx].thumbnail) - 1);
        int tw = 0, thh = 0;
        unsigned int tex = login_load_thumb_by_name(thumbnail, id, &tw, &thh);
        if (tex) {
            ls->games[idx].thumb_tex = tex;
            ls->games[idx].thumb_loaded = true;
            ls->games[idx].thumb_w = tw;
            ls->games[idx].thumb_h = thh;
        }
    }
    ls->selected_game = idx;
    ls->sel_draw_valid = false;
    ls->phase = 2;
    ls->detail_fetched = false;
    ls->detail_loading = false;
    ls->error[0] = '\0';
}

static void login_fetch_game_detail(LoginScreen* ls) {
    if (ls->selected_game < 0 || ls->selected_game >= ls->game_count) return;
    if (ls->games[ls->selected_game].local_path[0]) {
        ls->detail_fetched = true;
        ls->error[0] = '\0';
        return;
    }
    int gid = ls->games[ls->selected_game].id;
    char url[640];
    if (ls->session_token[0]) {
        snprintf(url, sizeof(url),
            "%s/api/games.php?action=public_get&id=%d&session_token=%s",
            pw_site_origin(), gid, ls->session_token);
    } else {
        snprintf(url, sizeof(url),
            "%s/api/games.php?action=public_get&id=%d", pw_site_origin(), gid);
    }
    size_t resp_len = 0;
    char* resp = (char*)platform_http_get(url, &resp_len);
    if (!resp) {
        snprintf(ls->error, sizeof(ls->error), "Could not load game details.");
        return;
    }
    int gi = ls->selected_game;
    const char* desc_key = strstr(resp, "\"description\"");
    if (desc_key) {
        const char* c = strchr(desc_key + 12, ':');
        if (c) {
            c++;
            while (*c == ' ') c++;
            if (*c == '"') {
                c++;
                json_copy_string(c, ls->games[gi].description, (int)sizeof(ls->games[gi].description));
            }
        }
    }
    const char* likes_key = strstr(resp, "\"likes\"");
    if (likes_key) {
        const char* c = strchr(likes_key + 6, ':');
        if (c) ls->games[gi].likes = atoi(c + 1);
    }
    const char* dis_key = strstr(resp, "\"dislikes\"");
    if (dis_key) {
        const char* c = strchr(dis_key + 10, ':');
        if (c) ls->games[gi].dislikes = atoi(c + 1);
    }
    const char* plays_key = strstr(resp, "\"plays\"");
    if (plays_key) {
        const char* c = strchr(plays_key + 6, ':');
        if (c) ls->games[gi].plays = atoi(c + 1);
    }
    const char* playn_key = strstr(resp, "\"playing_now\"");
    if (playn_key) {
        const char* c = strchr(playn_key + 12, ':');
        if (c) ls->games[gi].playing_now = atoi(c + 1);
    }
    const char* ur_key = strstr(resp, "\"user_rating\"");
    if (ur_key) {
        const char* c = strchr(ur_key + 12, ':');
        if (c) ls->games[gi].user_rating = atoi(c + 1);
    }
    free(resp);
    ls->detail_fetched = true;
}

static void login_set_games_tab(LoginScreen* ls, int tab) {
    int n = games_tab_count(ls);
    if (tab < 0) tab = n - 1;
    if (tab >= n) tab = 0;
    if (tab == ls->games_tab && ls->games_fetched) return;
    ls->games_tab = tab;
    ls->games_fetched = false;
    ls->games_loading = false;
}

static void init_bg(void) {
    // Prefer HQ.
    LoginFileBuf logo = {0};
    platform_load_file("assets/polyworld_logo.png", on_login_file, &logo);
    if (!logo.data) platform_load_file("website/assets/images/polyworld_hq.png", on_login_file, &logo);
    if (!logo.data) platform_load_file("website/assets/images/polyworld_lq.png", on_login_file, &logo);
    if (!logo.data) {
        PW_ERR(ERR_FILE, "Could not find the PolyWorld logo, this is very serious. If you do not restore the PolyWorld logo, the game will explode and everything will come crashing down!!!!!!!");
    } else {
        unsigned char* raw = logo.data;
        long sz = (long)logo.len;
        int iw, ih, ic;
        unsigned char* pixels = stbi_load_from_memory(raw, (int)sz, &iw, &ih, &ic, 4);
        free(raw);
        if (pixels) {
            // Premultiply!!!
            for (int i = 0; i < iw * ih; i++) {
                unsigned char a = pixels[i * 4 + 3];
                pixels[i * 4 + 0] = (unsigned char)((pixels[i * 4 + 0] * a) / 255);
                pixels[i * 4 + 1] = (unsigned char)((pixels[i * 4 + 1] * a) / 255);
                pixels[i * 4 + 2] = (unsigned char)((pixels[i * 4 + 2] * a) / 255);
            }
            glGenTextures(1, &g_logo_tex);
            glBindTexture(GL_TEXTURE_2D, g_logo_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g_logo_w = iw;
            g_logo_h = ih;
            stbi_image_free(pixels);
        }
    }

    if (!g_checked_tex)
        g_checked_tex = login_load_texture_file("assets/checked.png", NULL, NULL);
    if (!g_unchecked_tex)
        g_unchecked_tex = login_load_texture_file("assets/unchecked.png", NULL, NULL);

    ensure_draw_shaders();
    glGenVertexArrays(1, &g_vao);
    g_initialized = true;
}

void login_screen_invalidate_gl(LoginScreen* ls, bool context_alive) {
    if (context_alive) {
        if (g_color_shader) glDeleteProgram(g_color_shader);
        if (g_tex_shader) glDeleteProgram(g_tex_shader);
        if (g_round_shader) glDeleteProgram(g_round_shader);
        if (g_round_tex_shader) glDeleteProgram(g_round_tex_shader);
        if (g_vao) glDeleteVertexArrays(1, &g_vao);
        if (g_logo_tex) glDeleteTextures(1, &g_logo_tex);
        if (g_checked_tex) glDeleteTextures(1, &g_checked_tex);
        if (g_unchecked_tex) glDeleteTextures(1, &g_unchecked_tex);
        if (g_update_tex) glDeleteTextures(1, &g_update_tex);
        if (ls) {
            login_clear_game_thumbs(ls);
            login_clear_home_thumbs(ls);
            g_login_banner_gen++;
            if (ls->banner_icon_tex) {
                glDeleteTextures(1, &ls->banner_icon_tex);
                ls->banner_icon_tex = 0;
            }
            ls->banner_icon_loaded = false;
            ls->banner_icon_loading = false;
            ls->banner_loading = false;
        }
    } else if (ls) {
        // no context, don't glDelete
        g_login_thumb_gen++;
        g_login_thumbs_inflight = 0;
        g_login_banner_gen++;
        ls->banner_icon_tex = 0;
        ls->banner_icon_loaded = false;
        ls->banner_icon_loading = false;
        ls->banner_loading = false;
        for (int i = 0; i < LOGIN_MAX_GAMES; i++) {
            ls->games[i].thumb_tex = 0;
            ls->games[i].thumb_loaded = false;
            ls->games[i].thumb_loading = false;
        }
        for (int i = 0; i < 16; i++) {
            ls->friends[i].avatar_tex = 0;
            ls->friends[i].avatar_loaded = false;
            ls->friends[i].avatar_loading = false;
        }
        for (int i = 0; i < 8; i++) {
            ls->continue_games[i].thumb_tex = 0;
            ls->continue_games[i].thumb_loaded = false;
            ls->continue_games[i].thumb_loading = false;
            ls->gotw_games[i].thumb_tex = 0;
            ls->gotw_games[i].thumb_loaded = false;
            ls->gotw_games[i].thumb_loading = false;
        }
        ls->games_fetched = false;
        ls->home_fetched = false;
    }
    g_color_shader = 0;
    g_tex_shader = 0;
    g_round_shader = 0;
    g_round_tex_shader = 0;
    g_vao = 0;
    g_logo_tex = 0;
    g_checked_tex = 0;
    g_unchecked_tex = 0;
    g_update_tex = 0;
    g_update_tw = g_update_th = 0;
    g_logo_w = g_logo_h = 0;
    g_initialized = false;
}

static void login_open_downloads(void) {
    char url[320];
    snprintf(url, sizeof(url), "%s/downloads/", pw_site_origin());
    platform_open_url(url);
}

#ifdef __ANDROID__
static int login_cmp_ver(const char* local, const char* remote) {
    int l[3] = {0, 0, 0};
    int r[3] = {0, 0, 0};
    sscanf(local ? local : "", "%d.%d.%d", &l[0], &l[1], &l[2]);
    sscanf(remote ? remote : "", "%d.%d.%d", &r[0], &r[1], &r[2]);
    if (r[0] != l[0]) return r[0] - l[0];
    if (r[1] != l[1]) return r[1] - l[1];
    return r[2] - l[2];
}

static void login_android_check_remote_version(LoginScreen* ls) {
    static bool checked;
    if (checked || !ls) return;
    checked = true;
    if (!pw_site_is_production()) return;
    if (strstr(CLIENT_VERSION, "demo") != NULL) return;
    char url[320];
    snprintf(url, sizeof(url), "%s/latestclient.txt", pw_site_origin());
    size_t n = 0;
    char* remote = (char*)platform_http_get(url, &n);
    if (!remote || n == 0) {
        free(remote);
        return;
    }
    while (n > 0 && (remote[n - 1] == '\n' || remote[n - 1] == '\r' || remote[n - 1] == ' '))
        remote[--n] = '\0';
    int dummy[3];
    if (sscanf(remote, "%d.%d.%d", &dummy[0], &dummy[1], &dummy[2]) >= 2) {
        if (login_cmp_ver(CLIENT_VERSION, remote) > 0)
            ls->update_required = true;
    }
    free(remote);
}
#endif

void login_screen_require_update(LoginScreen* ls) {
    if (ls) ls->update_required = true;
}

void login_screen_init(LoginScreen* ls) {
    if (g_initialized) return; // already good
    if (ls->game_id == 0 && ls->phase == 0 && !ls->logged_in && !ls->session_token[0]) {
        memset(ls, 0, sizeof(LoginScreen));
        ls->game_id = 6;
        ls->active_field = -1;
    } else if (ls->game_id == 0) {
        ls->game_id = 6;
    }
    init_bg();
    font_init();
    te_reset(&s_login_user_edit, 0);
    te_reset(&s_login_pass_edit, 0);
    s_login_field_drag = false;
#ifdef __ANDROID__
    login_android_check_remote_version(ls);
#endif
}

bool login_screen_update(LoginScreen* ls, float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    if (ls->update_required) return false;
    if (ls->ignore_pointer_until_up && !input_mouse_left_held()) {
        ls->ignore_pointer_until_up = false;
    }
    if (s_login_field_drag) {
        if (input_mouse_left_held() && ls->phase == 0 && ls->active_field >= 0) {
            const InputState* in = input_get_state();
            int ix = 0, iy = 0;
            screen_to_image((int)in->mouse_x, (int)in->mouse_y, &ix, &iy);
            TextEdit* e = (ls->active_field == 0) ? &s_login_user_edit : &s_login_pass_edit;
            const char* buf = (ls->active_field == 0) ? ls->username : ls->password;
            int nlen = (ls->active_field == 0) ? ls->username_len : ls->password_len;
            float local_x = (float)ix - (LOGIN_FORM_X + 12.0f);
            int hit;
            if (ls->active_field == 1 && !ls->awaiting_2fa) {
                char stars[64];
                int pl = nlen > 63 ? 63 : nlen;
                memset(stars, '*', (size_t)pl);
                stars[pl] = '\0';
                hit = te_hit_x(stars, 0, pl, local_x, TXT_BODY_H);
            } else {
                hit = te_hit_x(buf, 0, nlen, local_x, TXT_BODY_H);
            }
            te_mouse_drag(e, hit);
        } else {
            te_mouse_up(&s_login_user_edit);
            te_mouse_up(&s_login_pass_edit);
            s_login_field_drag = false;
        }
    }
    if (!ls->banner_fetched && !ls->banner_loading) {
        login_start_banner_fetch(ls);
    } else if (ls->banner_fetched && ls->banner_enabled && ls->banner_icon_url[0] &&
               !ls->banner_icon_loaded && !ls->banner_icon_loading) {
        login_start_banner_icon(ls);
    }
    if (ls->phase == 1) {
        const InputState* in = input_get_state();
        int ix = 0, iy = 0;
        screen_to_image((int)in->mouse_x, (int)in->mouse_y, &ix, &iy);

        if (ls->ignore_pointer_until_up) {
            ls->games_drag_active = false;
            ls->games_drag_moved = false;
            ls->games_drag_mode = 0;
        } else if (ls->games_drag_active) {
#ifdef __ANDROID__
            // Cant wait for this variable to make "dragslop" games... XD
            // A very funny variable!
            const float dragslop = 4.0f;
#else
            const float dragslop = 6.0f;
#endif
            // TODO: ending drag should decelerate? Maybe.
            if (input_mouse_left_held()) {
                float ddx = (float)ix - ls->games_drag_last_x;
                float ddy = ls->games_drag_last_y - (float)iy;
                ls->games_drag_last_x = (float)ix;
                ls->games_drag_last_y = (float)iy;
                float move_x = (float)ix - ls->games_drag_start_x;
                float move_y = (float)iy - ls->games_drag_start_y;
                if (!ls->games_drag_moved && (fabsf(move_x) > dragslop || fabsf(move_y) > dragslop)) {
                    ls->games_drag_moved = true;
                    bool on_rail = login_is_home_user(ls) &&
                        home_games_point_in_rail(ls, ls->games_drag_start_x, ls->games_drag_start_y);
                    if (ls->games_drag_mode == 1 || (on_rail && fabsf(move_x) >= fabsf(move_y)))
                        ls->games_drag_mode = 1;
                    else
                        ls->games_drag_mode = 0;
                }
                if (ls->games_drag_moved) {
                    if (ls->games_drag_mode == 1) {
                        ls->games_rail_scroll_target -= ddx;
                        home_games_clamp_scroll_x(ls);
                        ls->games_rail_scroll_x = ls->games_rail_scroll_target;
                    } else if (fabsf(ddy) > 0.01f) {
                        ls->games_scroll_target += ddy;
                        games_clamp_scroll(ls);
                        ls->games_scroll_y = ls->games_scroll_target;
                    }
                }
            } else {
                if (!ls->games_drag_moved && ls->games_fetched && !ls->games_loading) {
                    int hit = games_hit_card(ls, ls->games_drag_start_x, ls->games_drag_start_y);
                    if (hit < 0) hit = games_hit_card(ls, (float)ix, (float)iy);
                    if (hit >= 0) {
                        ls->selected_game = hit;
                        ls->phase = 2;
                        ls->detail_fetched = false;
                        ls->detail_loading = false;
                        ls->error[0] = '\0';
                    }
                }
                ls->games_drag_active = false;
                ls->games_drag_moved = false;
                ls->games_drag_mode = 0;
            }
        }

        games_clamp_scroll(ls);
        home_games_clamp_scroll_x(ls);
        float k = 1.0f - expf(-14.0f * dt);
        if (!ls->games_drag_active || !ls->games_drag_moved || ls->games_drag_mode != 0)
            ls->games_scroll_y += (ls->games_scroll_target - ls->games_scroll_y) * k;
        else
            ls->games_scroll_y = ls->games_scroll_target;
        if (!ls->games_drag_active || !ls->games_drag_moved || ls->games_drag_mode != 1)
            ls->games_rail_scroll_x += (ls->games_rail_scroll_target - ls->games_rail_scroll_x) * k;
        else
            ls->games_rail_scroll_x = ls->games_rail_scroll_target;

        if (ls->game_count > 0 && ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
            float tx, ty, tw, th;
            games_card_rect(ls, ls->selected_game, &tx, &ty, &tw, &th);
            if (!ls->sel_draw_valid) {
                ls->sel_draw_x = tx; ls->sel_draw_y = ty;
                ls->sel_draw_w = tw; ls->sel_draw_h = th;
                ls->sel_draw_valid = true;
            } else {
                float sk = 1.0f - expf(-16.0f * dt);
                ls->sel_draw_x += (tx - ls->sel_draw_x) * sk;
                ls->sel_draw_y += (ty - ls->sel_draw_y) * sk;
                ls->sel_draw_w += (tw - ls->sel_draw_w) * sk;
                ls->sel_draw_h += (th - ls->sel_draw_h) * sk;
            }
        }
    }

    // Login form
    if (ls->phase == 0 && ls->submitted && !ls->logged_in) {
        ls->submitted = false;

        AuthResult auth = {0};
        if (ls->play_as_guest) {
            ls->play_as_guest = false;
            ls->session_token[0] = '\0';
            ls->username[0] = '\0';
            ls->username_len = 0;
            ls->password[0] = '\0';
            ls->password_len = 0;
            ls->awaiting_2fa = false;
            ls->totp_challenge[0] = '\0';
            auth_clear_session();
        } else if (ls->awaiting_2fa) {
            if (ls->password_len <= 0) {
                snprintf(ls->error, sizeof(ls->error), "Enter your authenticator or backup code.");
                return false;
            }
            auth = auth_login_2fa(ls->totp_challenge, ls->password);
            if (auth.authenticated) {
                strncpy(ls->session_token, auth.token, sizeof(ls->session_token) - 1);
                if (auth.username[0]) {
                    strncpy(ls->username, auth.username, sizeof(ls->username) - 1);
                    ls->username_len = (int)strlen(ls->username);
                }
                auth_save_session(auth.token);
                ls->awaiting_2fa = false;
                ls->totp_challenge[0] = '\0';
                ls->password[0] = '\0';
                ls->password_len = 0;
            } else {
                snprintf(ls->error, sizeof(ls->error), "%s",
                         auth.error[0] ? auth.error : "Invalid authenticator or backup code.");
                if (auth.challenge_expired) {
                    ls->awaiting_2fa = false;
                    ls->totp_challenge[0] = '\0';
                    ls->password[0] = '\0';
                    ls->password_len = 0;
                }
                return false;
            }
        } else if (ls->username_len > 0 && ls->password_len > 0) {
            auth = auth_login(ls->username, ls->password);
            if (auth.authenticated) {
                strncpy(ls->session_token, auth.token, sizeof(ls->session_token) - 1);
                auth_save_session(auth.token);
            } else if (auth.needs_2fa) {
                ls->awaiting_2fa = true;
                strncpy(ls->totp_challenge, auth.challenge, sizeof(ls->totp_challenge) - 1);
                ls->totp_challenge[sizeof(ls->totp_challenge) - 1] = '\0';
                ls->password[0] = '\0';
                ls->password_len = 0;
                ls->active_field = 1;
                ls->error[0] = '\0';
                return false;
            } else {
                snprintf(ls->error, sizeof(ls->error), "%s",
                         auth.error[0] ? auth.error : "Failed to log in! Check internet connection or the username/password...");
                return false;
            }
        } else {
            // Not the PAG button.
            snprintf(ls->error, sizeof(ls->error), "You can not log in with no username or password...");
            return false;
        }
        ls->logged_in = true;
        ls->phase = 1; // Games page!
        // Remove click so it doesnt carry over to games
        ls->ignore_pointer_until_up = input_mouse_left_held();
        ls->games_drag_active = false;
        ls->games_drag_moved = false;
        ls->active_field = -1;
        if (ls->games_tab >= games_tab_count(ls))
            ls->games_tab = 0;
        ls->games_fetched = false;
        ls->games_loading = false;
        ls->home_fetched = false;
        ls->home_loading = false;
        ls->detail_fetched = false;
        ls->detail_loading = false;
    }

    // Game page/detail page
    if (ls->phase == 1 || ls->phase == 2) {
        ls->skeleton_t += dt;
        // TODO: make JSON fetch async... Actually make everything async.
        if (ls->phase == 1 && !ls->games_fetched) {
            if (!ls->games_loading) {
                ls->games_loading = true;
                login_clear_game_thumbs(ls);
                ls->game_count = 0;
                ls->selected_game = 0;
                ls->sel_draw_valid = false;
            } else {
                login_fetch_games(ls);
                ls->games_fetched = true;
                ls->games_loading = false;
            }
        } else if (ls->phase == 1 && !ls->home_fetched) {
            // Guests skip...
            if (!ls->session_token[0]) {
                ls->home_fetched = true;
                ls->home_loading = false;
                ls->friend_count = 0;
                ls->continue_count = 0;
                ls->gotw_count = 0;
            } else if (!ls->home_loading) {
                ls->home_loading = true;
                login_clear_home_thumbs(ls);
                ls->friend_count = 0;
                ls->continue_count = 0;
                ls->gotw_count = 0;
            } else {
                login_fetch_home(ls);
                ls->home_fetched = true;
                ls->home_loading = false;
            }
        } else if (ls->phase == 2 && !ls->detail_fetched) {
            if (!ls->detail_loading) {
                ls->detail_loading = true;
            } else {
                login_fetch_game_detail(ls);
                ls->detail_loading = false;
            }
        } else if (ls->phase == 1 && ls->games_fetched) {
            login_pump_thumbs(ls);
        }

        if (ls->ready_to_play) {
            ls->ready_to_play = false;
            ls->offline_play = false;
            if (ls->game_count <= 0 || ls->selected_game < 0 || ls->selected_game >= ls->game_count) {
                snprintf(ls->error, sizeof(ls->error), "No game selected...?");
            } else if (ls->games[ls->selected_game].local_path[0]) {
                int si = ls->selected_game;
                ls->game_id = ls->games[si].id;
                ls->offline_play = true;
                ls->ticket[0] = '\0';
                if (ls->username[0]) {
                    snprintf(ls->ticket_username, sizeof(ls->ticket_username), "%s", ls->username);
                } else {
                    snprintf(ls->ticket_username, sizeof(ls->ticket_username), "Unknown_PolyWorldian");
                }
                if (!ls->skin_color[0])
                    snprintf(ls->skin_color, sizeof(ls->skin_color), "#eaeaea");
                ls->user_id = 0;
                return true;
            } else {
            int sel_game_id = ls->games[ls->selected_game].id;
            ls->game_id = sel_game_id;

            bool is_guest = (ls->session_token[0] == '\0');
            JoinTicket ticket = auth_get_join_ticket(
                    is_guest ? NULL : ls->session_token, sel_game_id, is_guest, 0, false);
            if (ticket.valid) {
                snprintf(ls->ticket, sizeof(ls->ticket), "%s", ticket.ticket);
                snprintf(ls->ticket_username, sizeof(ls->ticket_username), "%s", ticket.username);
                snprintf(ls->skin_color, sizeof(ls->skin_color), "%s", ticket.skin_color);
                ls->equipped_shirt = ticket.equipped_shirt;
                ls->equipped_pants = ticket.equipped_pants;
                ls->equipped_head = ticket.equipped_head;
                ls->equipped_package = ticket.equipped_package;
                memcpy(ls->equipped_accessories, ticket.equipped_accessories,
                       sizeof(ls->equipped_accessories));
                ls->equipped_accessory = ticket.equipped_accessory;
                memcpy(ls->equipped_emotes, ticket.equipped_emotes,
                       sizeof(ls->equipped_emotes));
                memcpy(ls->emote_anims, ticket.emote_anims, sizeof(ls->emote_anims));
                memcpy(ls->emote_names, ticket.emote_names, sizeof(ls->emote_names));
                ls->user_id = ticket.user_id;
                return true; // ready to connect
            } else {
#ifdef __ANDROID__
                if (pw_error_is_client_outdated(ticket.error))
                    ls->update_required = true;
#endif
                if (ticket.error[0])
                    snprintf(ls->error, sizeof(ls->error), "Server rejected join: %s", ticket.error);
                else
                    snprintf(ls->error, sizeof(ls->error), "Server rejected join without reason?");
            }
            }
        }
    }

    return false;
}

void login_screen_render(LoginScreen* ls, int width, int height) {
#if defined(PW_QUEST)
    // Quest specific "fake screen" we render to.
    GLint cur = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur);
    if (cur != 0) {
        login_screen_render_to(ls, width, height, (unsigned int)cur);
        return;
    }
#endif
    login_screen_render_to(ls, width, height, 0);
}

void login_screen_render_to(LoginScreen* ls, int width, int height, unsigned int fbo) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    // Keep odd aspect ratio (lolol)
    // This isn't even 16:9 or 4:3 or anything.
    // It's pretty awful, TODO: make the ui scale on aspect ratio (like, uhh, stretch the sides of a loaded image past the border and also do the same with content etc)
    float target_aspect = 1091.0f / 711.0f;
    float window_aspect = (float)width / (float)height;
    int vp_x = 0, vp_y = 0, vp_w = width, vp_h = height;
    if (fbo != 0) {
        // Fill...
        vp_x = 0;
        vp_y = 0;
        vp_w = width;
        vp_h = height;
    } else if (window_aspect > target_aspect) {
        vp_w = (int)(height * target_aspect);
        if (vp_w < 1) vp_w = 1;
        if (vp_w > width) vp_w = width;
        vp_x = (width - vp_w) / 2;
    } else {
        vp_h = (int)(width / target_aspect);
        if (vp_h < 1) vp_h = 1;
        if (vp_h > height) vp_h = height;
        vp_y = (height - vp_h) / 2;
    }

    g_hit_ox = vp_x;
    g_hit_oy = height - vp_y - vp_h;
    g_hit_w = vp_w > 0 ? vp_w : 1;
    g_hit_h = vp_h > 0 ? vp_h : 1;
    g_vp_x = vp_x;
    g_vp_y = vp_y;
    g_vp_w = vp_w > 0 ? vp_w : 1;
    g_vp_h = vp_h > 0 ? vp_h : 1;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // DO NOT do any depth, cull, scissor, or blend (We are not doing that YET!)
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glViewport(0, 0, width, height);
    glClearColor(COL_BG_R, COL_BG_G, COL_BG_B, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);

    int vwidth = vp_w, vheight = vp_h;

    if (!g_initialized) { return; }

    ensure_draw_shaders();
    if (!g_color_shader || !g_tex_shader || !g_round_shader || !g_round_tex_shader) {
        // Uh oh, shaders failed. Retry next frame...
        if (g_color_shader) { glDeleteProgram(g_color_shader); g_color_shader = 0; }
        if (g_tex_shader) { glDeleteProgram(g_tex_shader); g_tex_shader = 0; }
        if (g_round_shader) { glDeleteProgram(g_round_shader); g_round_shader = 0; }
        if (g_round_tex_shader) { glDeleteProgram(g_round_tex_shader); g_round_tex_shader = 0; }
        return;
    }

    g_scale_x = (float)vwidth / (float)IMG_W;
    g_scale_y = (float)vheight / (float)IMG_H;

    if (ls->update_required) {
        if (!g_update_tex)
            g_update_tex = login_load_texture_file("assets/android_update.png", &g_update_tw, &g_update_th);
        if (g_update_tex)
            draw_tex_img(g_update_tex, 0.0f, 0.0f, (float)IMG_W, (float)IMG_H);
        else
            draw_text_title_scaled("Update required", 40.0f, 320.0f, 1.0f, 1.0f, 1.0f, vwidth, vheight);
        glEnable(GL_DEPTH_TEST);
        return;
    }

    if (ls->phase == 0) {
        // Log in section
        draw_nav_bar(ls, vwidth, vheight);

        {
            const char* title = ls->awaiting_2fa ? "Two-factor" : "Login";
            float tw = measure_text(title);
            draw_text_scaled(title, ((float)IMG_W - tw) * 0.5f, login_ly(ls, LOGIN_TITLE_Y),
                             COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }

        draw_text_small_scaled("Username", LOGIN_FORM_X, login_ly(ls, LOGIN_USER_LBL_Y),
                               COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        {
            bool focus = (ls->active_field == 0);
            float br = focus ? COL_FOCUS_R : COL_LINE_R;
            float bgc = focus ? COL_FOCUS_G : COL_LINE_G;
            float bb = focus ? COL_FOCUS_B : COL_LINE_B;
            draw_round_border_img(LOGIN_FORM_X, login_ly(ls, LOGIN_USER_Y), LOGIN_FORM_W, LOGIN_USER_H,
                                  LOGIN_RADIUS, 1.5f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, br, bgc, bb);
            float tx = LOGIN_FORM_X + 12.0f;
            float ty = login_ly(ls, LOGIN_USER_Y) + 8.0f;
            if (focus && te_has_sel(&s_login_user_edit)) {
                int lo = te_sel_lo(&s_login_user_edit);
                int hi = te_sel_hi(&s_login_user_edit);
                float x0 = tx + te_prefix_width(ls->username, lo, TXT_BODY_H);
                float x1 = tx + te_prefix_width(ls->username, hi, TXT_BODY_H);
                if (x1 > x0)
                    draw_rect_img_a(x0, ty, x1 - x0, TXT_BODY_H, COL_LIME_R, COL_LIME_G, COL_LIME_B, 0.35f);
            }
            draw_text_scaled(ls->username, tx, ty, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            if (focus && fmodf(ls->skeleton_t, 1.0f) < 0.5f) {
                float cx = tx + te_prefix_width(ls->username, s_login_user_edit.caret, TXT_BODY_H);
                draw_rect_img(cx, ty, 1.5f, TXT_BODY_H, COL_LIME_R, COL_LIME_G, COL_LIME_B);
            }
        }

        draw_text_small_scaled(ls->awaiting_2fa ? "Authenticator or backup code" : "Password",
                               LOGIN_FORM_X, login_ly(ls, LOGIN_PASS_LBL_Y),
                               COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        {
            bool focus = (ls->active_field == 1);
            float br = focus ? COL_FOCUS_R : COL_LINE_R;
            float bgc = focus ? COL_FOCUS_G : COL_LINE_G;
            float bb = focus ? COL_FOCUS_B : COL_LINE_B;
            draw_round_border_img(LOGIN_FORM_X, login_ly(ls, LOGIN_PASS_Y), LOGIN_FORM_W, LOGIN_PASS_H,
                                  LOGIN_RADIUS, 1.5f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B, br, bgc, bb);
            char pdisp[65];
            int pl = ls->password_len > 63 ? 63 : ls->password_len;
            if (ls->awaiting_2fa) {
                strncpy(pdisp, ls->password, 64);
                pdisp[64] = '\0';
            } else {
                memset(pdisp, '*', (size_t)pl);
                pdisp[pl] = '\0';
            }
            float tx = LOGIN_FORM_X + 12.0f;
            float ty = login_ly(ls, LOGIN_PASS_Y) + 8.0f;
            if (focus && te_has_sel(&s_login_pass_edit)) {
                int lo = te_sel_lo(&s_login_pass_edit);
                int hi = te_sel_hi(&s_login_pass_edit);
                float x0 = tx + te_prefix_width(pdisp, lo, TXT_BODY_H);
                float x1 = tx + te_prefix_width(pdisp, hi, TXT_BODY_H);
                if (x1 > x0)
                    draw_rect_img_a(x0, ty, x1 - x0, TXT_BODY_H, COL_LIME_R, COL_LIME_G, COL_LIME_B, 0.35f);
            }
            draw_text_scaled(pdisp, tx, ty, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            if (focus && fmodf(ls->skeleton_t, 1.0f) < 0.5f) {
                float cx = tx + te_prefix_width(pdisp, s_login_pass_edit.caret, TXT_BODY_H);
                draw_rect_img(cx, ty, 1.5f, TXT_BODY_H, COL_LIME_R, COL_LIME_G, COL_LIME_B);
            }
        }

        // .btn-play
        draw_round_rect_img(LOGIN_FORM_X, login_ly(ls, LOGIN_BTN_Y), LOGIN_FORM_W, LOGIN_BTN_H,
                            LOGIN_RADIUS, COL_LIME_R, COL_LIME_G, COL_LIME_B);
        {
            const char* lab = ls->awaiting_2fa ? "Verify" : "Login";
            float tw = measure_text(lab);
            draw_text_scaled(lab, LOGIN_FORM_X + (LOGIN_FORM_W - tw) * 0.5f, login_ly(ls, LOGIN_BTN_Y) + 10.0f,
                             COL_ON_LIME_R, COL_ON_LIME_G, COL_ON_LIME_B, vwidth, vheight);
        }

        // PAG button!
        draw_round_rect_img(LOGIN_FORM_X, login_ly(ls, LOGIN_GUEST_Y), LOGIN_FORM_W, LOGIN_GUEST_H,
                            LOGIN_RADIUS, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
        {
            const char* lab = "Play as Guest";
            float tw = measure_text(lab);
            draw_text_scaled(lab, LOGIN_FORM_X + (LOGIN_FORM_W - tw) * 0.5f, login_ly(ls, LOGIN_GUEST_Y) + 10.0f,
                             COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }

        // "Don't Run Benchmark" check
        {
            float box = LOGIN_CHECK_SIZE;
            float label_x = LOGIN_FORM_X + box + 10.0f;
            unsigned int tex = ls->skip_benchmark ? g_checked_tex : g_unchecked_tex;
            if (tex)
                draw_tex_img(tex, LOGIN_FORM_X, login_ly(ls, LOGIN_CHECK_Y), box, box);
            else {
                // Where's the checkbox image?
                // Do a warn via PW_WARN.
                PW_WARN("No checkbox image!");
                // TODO: don't warn every frame lol...
                draw_round_border_img(LOGIN_FORM_X, login_ly(ls, LOGIN_CHECK_Y), box, box, 4.0f, 1.5f,
                                      COL_SOFT_R, COL_SOFT_G, COL_SOFT_B,
                                      COL_LINE_R, COL_LINE_G, COL_LINE_B);
                if (ls->skip_benchmark)
                    draw_text_small_scaled("X", LOGIN_FORM_X + 8.0f, login_ly(ls, LOGIN_CHECK_Y) + 6.0f,
                                           COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            }
            draw_text_small_scaled("Don't run benchmark", label_x, login_ly(ls, LOGIN_CHECK_Y) + 6.0f,
                                   COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
        }

        if (ls->error[0]) {
            float tw = measure_text(ls->error);
            float ex = ((float)IMG_W - tw) * 0.5f;
            if (ex < 20.0f) ex = 20.0f;
            draw_text_scaled(ls->error, ex, login_ly(ls, LOGIN_ERR_Y), COL_ERR_R, COL_ERR_G, COL_ERR_B, vwidth, vheight);
        }

#ifdef __ANDROID__
        // Input above the keyboard.
        if (ls->active_field >= 0 && platform_ime_visible()) {
            float kb_img = (float)platform_get_ime_bottom_inset() * (float)IMG_H / (float)g_hit_h;
            if (kb_img < 120.0f) kb_img = 220.0f; // Estimate!
            float card_h = 110.0f;
            float card_y = (float)IMG_H - kb_img - card_h - 20.0f;
            if (card_y < login_chrome_bottom(ls) + 8.0f) card_y = login_chrome_bottom(ls) + 8.0f;
            draw_round_border_img(40.0f, card_y, (float)IMG_W - 80.0f, card_h, LOGIN_RADIUS, 2.0f,
                                  0.12f, 0.12f, 0.14f, COL_FOCUS_R, COL_FOCUS_G, COL_FOCUS_B);
            const char* label = (ls->active_field == 0) ? "Username"
                : (ls->awaiting_2fa ? "Authenticator or backup code" : "Password");
            draw_text_small_scaled(label, 56.0f, card_y + 14.0f, 0.85f, 0.85f, 0.88f, vwidth, vheight);
            char disp[96];
            if (ls->active_field == 0) {
                snprintf(disp, sizeof(disp), "%s|", ls->username);
            } else {
                int pl = ls->password_len > 48 ? 48 : ls->password_len;
                // Show actual characters.
                snprintf(disp, sizeof(disp), "%.*s|", pl, ls->password);
            }
            draw_text_scaled_h(disp, 56.0f, card_y + 44.0f, 28.0f,
                               1.0f, 1.0f, 1.0f, vwidth, vheight);
        }
#endif
    } else if (ls->phase == 1) {
        // Games page
        draw_nav_bar(ls, vwidth, vheight);

        bool home_user = login_is_home_user(ls);
        float scroll_off = home_user ? ls->games_scroll_y : 0.0f;

        {
            const char* title = home_user ? "Home" : "Games";
            float tw = measure_text_title(title);
            draw_text_title_scaled(title, ((float)IMG_W - tw) * 0.5f,
                                   login_chrome_bottom(ls) + GAMES_TITLE_GAP,
                                   COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }

        float tab_y = games_tab_y(ls);
        float grid_y = games_grid_y(ls);
        float view_h = games_view_h(ls);
        float clip_top = home_user ? login_home_top_y(ls) : grid_y;
        {
            int clip_h = (int)(view_h * g_scale_y + 0.5f);
            int clip_y = g_vp_y + (int)(((float)IMG_H - clip_top - view_h) * g_scale_y + 0.5f);
            if (clip_h < 1) clip_h = 1;
            glEnable(GL_SCISSOR_TEST);
            glScissor(g_vp_x, clip_y, g_vp_w, clip_h);
        }

        float pulse = 0.88f + 0.12f * sinf(ls->skeleton_t * 4.0f);
        float sk_r = COL_SOFT_R * pulse, sk_g = COL_SOFT_G * pulse, sk_b = COL_SOFT_B * pulse;
        float ln_r = 0.88f * pulse, ln_g = 0.88f * pulse, ln_b = 0.88f * pulse;

        if (home_user) {
            float fy = login_home_friends_y(ls) - scroll_off;
            draw_text_scaled("Friends", GAMES_GRID_X, fy, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            float frow_y = fy + HOME_HEAD_H + HOME_HEAD_GAP;
            if (ls->home_loading || !ls->home_fetched) {
                for (int i = 0; i < 6; i++) {
                    float ccx = GAMES_GRID_X + (float)i * FRIEND_CHIP_W;
                    float dia = FRIEND_AVATAR_D;
                    float circle_x = ccx + (FRIEND_CHIP_W - dia) * 0.5f;
                    draw_round_rect_img(circle_x, frow_y, dia, dia, dia * 0.5f, sk_r, sk_g, sk_b);
                    draw_round_rect_img(ccx + 12.0f, frow_y + dia + 6.0f, FRIEND_CHIP_W - 24.0f, 10.0f, 4.0f,
                                        ln_r, ln_g, ln_b);
                }
            } else if (ls->friend_count <= 0) {
                draw_text_small_scaled("No friends yet.", GAMES_GRID_X, frow_y + 4.0f,
                                       COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
            } else {
                for (int i = 0; i < ls->friend_count && i < 16; i++) {
                    float ccx = GAMES_GRID_X + (float)i * FRIEND_CHIP_W;
                    float dia = FRIEND_AVATAR_D;
                    float circle_x = ccx + (FRIEND_CHIP_W - dia) * 0.5f;
                    float r, g, b;
                    parse_hex_color(ls->friends[i].avatar_color, &r, &g, &b);
                    draw_round_rect_img(circle_x, frow_y, dia, dia, dia * 0.5f, r, g, b);
                    if (ls->friends[i].avatar_loaded && ls->friends[i].avatar_tex) {
                        draw_round_tex_img(ls->friends[i].avatar_tex, circle_x, frow_y, dia, dia, dia * 0.5f,
                                           ls->friends[i].avatar_w, ls->friends[i].avatar_h);
                    } else {
                        char initial[2] = { ls->friends[i].username[0] ? ls->friends[i].username[0] : '?', '\0' };
                        float iw = measure_text_small(initial);
                        draw_text_small_scaled(initial, circle_x + (dia - iw) * 0.5f, frow_y + dia * 0.5f - 7.0f,
                                               1.0f, 1.0f, 1.0f, vwidth, vheight);
                    }
                    float dot_d = 12.0f;
                    float sr, sg, sb;
                    friend_status_color(ls->friends[i].status, &sr, &sg, &sb);
                    draw_round_rect_img(circle_x + dia - dot_d * 0.7f, frow_y + dia - dot_d * 0.7f,
                                        dot_d, dot_d, dot_d * 0.5f, sr, sg, sb);
                    char uname[14];
                    strncpy(uname, ls->friends[i].username, 12);
                    uname[12] = '\0';
                    float utw = measure_text_small(uname);
                    float ux = ccx + (FRIEND_CHIP_W - utw) * 0.5f;
                    if (ux < ccx) ux = ccx;
                    draw_text_small_scaled(uname, ux, frow_y + dia + 4.0f,
                                           COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                }
            }

            if (ls->home_loading || !ls->home_fetched || ls->continue_count > 0) {
                float cy0 = login_home_continue_y(ls) - scroll_off;
                draw_text_scaled("Continue", GAMES_GRID_X, cy0, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                float rail_y = cy0 + HOME_HEAD_H + HOME_HEAD_GAP;
                if (ls->home_loading || !ls->home_fetched) {
                    for (int i = 0; i < 4; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        draw_round_rect_img(rx, rail_y, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                            sk_r, sk_g, sk_b);
                        draw_round_rect_img(rx, rail_y + HOME_RAIL_THUMB_H + 8.0f, HOME_RAIL_THUMB_W * 0.7f, 10.0f, 4.0f,
                                            ln_r, ln_g, ln_b);
                    }
                } else {
                    int show = ls->continue_count;
                    if (show > HOME_RAIL_MAX_VISIBLE) show = HOME_RAIL_MAX_VISIBLE;
                    for (int i = 0; i < show; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        if (ls->continue_games[i].thumb_loaded && ls->continue_games[i].thumb_tex) {
                            draw_round_tex_img(ls->continue_games[i].thumb_tex, rx, rail_y,
                                               HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                               ls->continue_games[i].thumb_w, ls->continue_games[i].thumb_h);
                        } else {
                            draw_round_rect_img(rx, rail_y, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                                COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
                        }
                        char trunc[24];
                        strncpy(trunc, ls->continue_games[i].title, 22);
                        trunc[22] = '\0';
                        draw_text_small_scaled(trunc, rx, rail_y + HOME_RAIL_THUMB_H + 8.0f,
                                               COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                    }
                }
            }

            {
                float gy0 = login_home_gotw_y(ls) - scroll_off;
                draw_text_scaled("Game of the Week", GAMES_GRID_X, gy0,
                                 COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                float rail_y = gy0 + HOME_HEAD_H + HOME_HEAD_GAP;
                if (ls->home_loading || !ls->home_fetched) {
                    for (int i = 0; i < 4; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        draw_round_rect_img(rx, rail_y, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                            sk_r, sk_g, sk_b);
                        draw_round_rect_img(rx, rail_y + HOME_RAIL_THUMB_H + 8.0f, HOME_RAIL_THUMB_W * 0.65f, 10.0f, 4.0f,
                                            ln_r, ln_g, ln_b);
                    }
                } else if (ls->gotw_count > 0) {
                    int show = ls->gotw_count;
                    if (show > HOME_RAIL_MAX_VISIBLE) show = HOME_RAIL_MAX_VISIBLE;
                    for (int i = 0; i < show; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        if (ls->gotw_games[i].thumb_loaded && ls->gotw_games[i].thumb_tex) {
                            draw_round_tex_img(ls->gotw_games[i].thumb_tex, rx, rail_y,
                                               HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                               ls->gotw_games[i].thumb_w, ls->gotw_games[i].thumb_h);
                        } else {
                            draw_round_rect_img(rx, rail_y, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                                COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
                        }
                        char trunc[24];
                        strncpy(trunc, ls->gotw_games[i].title, 22);
                        trunc[22] = '\0';
                        draw_text_small_scaled(trunc, rx, rail_y + HOME_RAIL_THUMB_H + 8.0f,
                                               COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                    }
                }
            }

            {
                float ghy = login_home_games_head_y(ls) - scroll_off;
                draw_text_scaled("Games", GAMES_GRID_X, ghy, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            }
        }

        int ntabs = games_tab_count(ls);
        float ty = tab_y - scroll_off;
        for (int t = 0; t < ntabs; t++) {
            float tx, tw;
            games_tab_rect(ls, t, &tx, &tw);
            bool active = (t == ls->games_tab);
            if (active) {
                draw_round_rect_img(tx, ty, tw, GAMES_TAB_H, GAMES_TAB_H * 0.5f,
                                    COL_TAB_ON_R, COL_TAB_ON_G, COL_TAB_ON_B);
            } else {
                draw_round_rect_img(tx, ty, tw, GAMES_TAB_H, GAMES_TAB_H * 0.5f,
                                    COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
            }
            const char* label = games_tab_label(ls, t);
            float text_w = measure_text_small(label);
            float label_x = tx + (tw - text_w) * 0.5f;
            if (active) {
                draw_text_small_scaled(label, label_x, ty + 8.0f,
                                       COL_ON_INK_R, COL_ON_INK_G, COL_ON_INK_B, vwidth, vheight);
            } else {
                draw_text_small_scaled(label, label_x, ty + 8.0f,
                                       COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
            }
        }

        if (home_user) {
            float rail_y = grid_y - scroll_off;
            {
                int rail_clip_h = (int)(HOME_GAMES_RAIL_H * g_scale_y + 0.5f);
                int rail_clip_y = g_vp_y + (int)(((float)IMG_H - rail_y - HOME_GAMES_RAIL_H) * g_scale_y + 0.5f);
                int rail_clip_x = g_vp_x + (int)(GAMES_GRID_X * g_scale_x + 0.5f);
                int rail_clip_w = (int)(home_games_view_w() * g_scale_x + 0.5f);
                if (rail_clip_h < 1) rail_clip_h = 1;
                if (rail_clip_w < 1) rail_clip_w = 1;
                glEnable(GL_SCISSOR_TEST);
                glScissor(rail_clip_x, rail_clip_y, rail_clip_w, rail_clip_h);
            }
            if (ls->games_loading || !ls->games_fetched) {
                for (int i = 0; i < 10; i++) {
                    float rx, ry;
                    home_games_cell(i, 0.0f, rail_y, &rx, &ry);
                    draw_round_rect_img(rx, ry, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                        sk_r, sk_g, sk_b);
                    draw_round_rect_img(rx, ry + HOME_RAIL_THUMB_H + 8.0f, HOME_RAIL_THUMB_W * 0.72f, 10.0f, 4.0f,
                                        ln_r, ln_g, ln_b);
                    draw_round_rect_img(rx, ry + HOME_RAIL_THUMB_H + 22.0f, HOME_RAIL_THUMB_W * 0.4f, 8.0f, 4.0f,
                                        ln_r, ln_g, ln_b);
                }
            } else if (ls->game_count == 0) {
                const char* empty = ls->error[0] ? ls->error : "No games found.";
                draw_text_small_scaled(empty, GAMES_GRID_X, rail_y + 20.0f,
                                       COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
            } else {
                games_clamp_scroll(ls);
                home_games_clamp_scroll_x(ls);
                float sx = ls->games_rail_scroll_x;
                if (ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
                    float hx, hy, hw, hh;
                    games_card_rect(ls, ls->selected_game, &hx, &hy, &hw, &hh);
                    if (hx + hw > GAMES_GRID_X && hx < GAMES_GRID_X + home_games_view_w())
                        draw_round_rect_img(hx - 3.0f, hy - 3.0f, hw + 6.0f, hh + 6.0f,
                                            GAMES_THUMB_RADIUS + 2.0f, COL_LIME_R, COL_LIME_G, COL_LIME_B);
                }
                for (int i = 0; i < ls->game_count && i < LOGIN_MAX_GAMES; i++) {
                    float rx, ry;
                    home_games_cell(i, sx, rail_y, &rx, &ry);
                    if (rx + HOME_RAIL_THUMB_W < GAMES_GRID_X - 4.0f) continue;
                    if (rx > GAMES_GRID_X + home_games_view_w() + 4.0f) continue;
                    if (ls->games[i].thumb_loaded && ls->games[i].thumb_tex) {
                        draw_round_tex_img(ls->games[i].thumb_tex, rx, ry,
                                           HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                           ls->games[i].thumb_w, ls->games[i].thumb_h);
                    } else {
                        draw_round_rect_img(rx, ry, HOME_RAIL_THUMB_W, HOME_RAIL_THUMB_H, GAMES_THUMB_RADIUS,
                                            COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
                    }
                    char trunc[24];
                    strncpy(trunc, ls->games[i].title, 22);
                    trunc[22] = '\0';
                    draw_text_small_scaled(trunc, rx, ry + HOME_RAIL_THUMB_H + 8.0f,
                                           COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                    char meta[40];
                    if (ls->games[i].playing_now > 0)
                        snprintf(meta, sizeof(meta), "%d playing", ls->games[i].playing_now);
                    else
                        snprintf(meta, sizeof(meta), "%d plays", ls->games[i].plays);
                    draw_text_scaled_h(meta, rx, ry + HOME_RAIL_THUMB_H + 22.0f, TXT_META_H,
                                       ls->games[i].playing_now > 0 ? COL_PLAYING_R : COL_MUTED_R,
                                       ls->games[i].playing_now > 0 ? COL_PLAYING_G : COL_MUTED_G,
                                       ls->games[i].playing_now > 0 ? COL_PLAYING_B : COL_MUTED_B,
                                       vwidth, vheight);
                }
            }
            {
                int clip_h = (int)(view_h * g_scale_y + 0.5f);
                int clip_y = g_vp_y + (int)(((float)IMG_H - clip_top - view_h) * g_scale_y + 0.5f);
                if (clip_h < 1) clip_h = 1;
                glScissor(g_vp_x, clip_y, g_vp_w, clip_h);
            }
        } else {
            // Old design, for guests.
            const float tile_w = GAMES_CARD_W;
            const float card_gap_x = GAMES_CARD_GAP_X;
            const float card_gap_y = GAMES_CARD_GAP_Y;
            int cols = games_grid_cols();
            float thumb_w = tile_w;
            float thumb_h = games_thumb_h();
            float tile_h = games_tile_h();
            float grid_w = (float)cols * tile_w + (float)(cols - 1) * card_gap_x;
            float grid_origin_x = (((float)IMG_W - grid_w) * 0.5f);
            if (grid_origin_x < GAMES_GRID_X) grid_origin_x = GAMES_GRID_X;

            if (ls->games_loading || !ls->games_fetched) {
                int skel_count = cols * 2;
                if (skel_count > 8) skel_count = 8;
                for (int i = 0; i < skel_count; i++) {
                    int col = i % cols;
                    int row = i / cols;
                    float cx = grid_origin_x + (float)col * (tile_w + card_gap_x);
                    float cy = grid_y + (float)row * (tile_h + card_gap_y) - scroll_off;
                    if (cy + tile_h < clip_top || cy > clip_top + view_h) continue;
                    draw_round_rect_img(cx, cy, thumb_w, thumb_h, GAMES_THUMB_RADIUS, sk_r, sk_g, sk_b);
                    float title_y = cy + thumb_h + 8.0f;
                    draw_round_rect_img(cx, title_y + 2.0f, tile_w * 0.72f, 12.0f, 4.0f, ln_r, ln_g, ln_b);
                    draw_round_rect_img(cx, title_y + 20.0f, tile_w * 0.42f, 10.0f, 4.0f, ln_r, ln_g, ln_b);
                }
            } else if (ls->game_count == 0) {
                const char* empty = ls->error[0]
                    ? ls->error
                    : (games_tab_is_offline(ls)
                        ? "No offline places found."
                        : (ls->games_tab == 0
                            ? "Play a few games to get recommendations."
                            : "No games found. Try the Offline tab."));
                float tw = measure_text(empty);
                draw_text_scaled(empty, ((float)IMG_W - tw) * 0.5f, grid_y + 40.0f - scroll_off,
                                 COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
            } else {
                games_clamp_scroll(ls);
                if (ls->game_count > 0 && ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
                    float hx, hy, hw, hh;
                    games_card_rect(ls, ls->selected_game, &hx, &hy, &hw, &hh);
                    draw_round_rect_img(hx - 3.0f, hy - 3.0f, hw + 6.0f, hh + 6.0f,
                                        GAMES_THUMB_RADIUS + 2.0f, COL_LIME_R, COL_LIME_G, COL_LIME_B);
                }
                for (int i = 0; i < ls->game_count && i < LOGIN_MAX_GAMES; i++) {
                    int col = i % cols;
                    int row = i / cols;
                    float cx = grid_origin_x + (float)col * (tile_w + card_gap_x);
                    float cy = grid_y + (float)row * (tile_h + card_gap_y) - ls->games_scroll_y;
                    if (ls->games[i].thumb_loaded && ls->games[i].thumb_tex) {
                        draw_round_tex_img(ls->games[i].thumb_tex, cx, cy, thumb_w, thumb_h, GAMES_THUMB_RADIUS,
                                           ls->games[i].thumb_w, ls->games[i].thumb_h);
                    } else {
                        draw_round_rect_img(cx, cy, thumb_w, thumb_h, GAMES_THUMB_RADIUS,
                                            COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
                    }
                    float title_y = cy + thumb_h + 8.0f;
                    char trunc_title[28];
                    strncpy(trunc_title, ls->games[i].title, 27);
                    trunc_title[27] = '\0';
                    draw_text_small_scaled(trunc_title, cx, title_y,
                                           COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
                    char meta[48];
                    if (ls->games[i].local_path[0]) {
                        snprintf(meta, sizeof(meta), "Offline");
                        draw_text_scaled_h(meta, cx, title_y + 16.0f, TXT_META_H,
                                           COL_PLAYING_R, COL_PLAYING_G, COL_PLAYING_B, vwidth, vheight);
                    } else if (ls->games[i].playing_now > 0) {
                        snprintf(meta, sizeof(meta), "%d playing", ls->games[i].playing_now);
                        draw_text_scaled_h(meta, cx, title_y + 16.0f, TXT_META_H,
                                           COL_PLAYING_R, COL_PLAYING_G, COL_PLAYING_B, vwidth, vheight);
                    } else {
                        snprintf(meta, sizeof(meta), "%d plays", ls->games[i].plays);
                        draw_text_scaled_h(meta, cx, title_y + 16.0f, TXT_META_H,
                                           COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
                    }
                }
            }
        }
        glDisable(GL_SCISSOR_TEST);

        if (ls->error[0]) {
            draw_text_scaled(ls->error, 30.0f, (float)IMG_H - 28.0f,
                             COL_ERR_R, COL_ERR_G, COL_ERR_B, vwidth, vheight);
        }
    } else if (ls->phase == 2 && ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
        // Detail for games
        draw_nav_bar(ls, vwidth, vheight);
        int si = ls->selected_game;

        draw_text_small_scaled("Go Back", 40.0f, login_chrome_bottom(ls) + 16.0f,
                               COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);

        float thumb_w = 360.0f;
        float thumb_h = thumb_w * 9.0f / 16.0f;
        float left_x = 40.0f;
        float top_y = login_chrome_bottom(ls) + 48.0f;
        float info_x = left_x + thumb_w + 28.0f;
        float info_w = (float)IMG_W - info_x - 40.0f;

        if (ls->detail_loading || !ls->detail_fetched) {
            float pulse = 0.88f + 0.12f * sinf(ls->skeleton_t * 4.0f);
            float br = COL_SOFT_R * pulse, bg = COL_SOFT_G * pulse, bb = COL_SOFT_B * pulse;
            float lr = 0.88f * pulse, lg = 0.88f * pulse, lb = 0.88f * pulse;
            draw_round_rect_img(left_x, top_y, thumb_w, thumb_h, 10.0f, br, bg, bb);
            draw_round_rect_img(info_x, top_y + 4.0f, info_w * 0.7f, 28.0f, 6.0f, lr, lg, lb);
            draw_round_rect_img(info_x, top_y + 42.0f, info_w * 0.4f, 14.0f, 4.0f, lr, lg, lb);
            draw_round_rect_img(info_x, top_y + 66.0f, info_w * 0.55f, 14.0f, 4.0f, lr, lg, lb);
            draw_round_rect_img(info_x, top_y + 92.0f, 160.0f, 42.0f, 8.0f, br, bg, bb);
            float desc_y = top_y + thumb_h + 24.0f;
            draw_round_rect_img(left_x, desc_y, 80.0f, 18.0f, 4.0f, lr, lg, lb);
            draw_round_rect_img(left_x, desc_y + 28.0f, thumb_w + 200.0f, 14.0f, 4.0f, lr, lg, lb);
            draw_round_rect_img(left_x, desc_y + 50.0f, thumb_w + 120.0f, 14.0f, 4.0f, lr, lg, lb);
        } else {
        if (ls->games[si].thumb_loaded && ls->games[si].thumb_tex) {
            draw_round_tex_img(ls->games[si].thumb_tex, left_x, top_y, thumb_w, thumb_h, 10.0f,
                               ls->games[si].thumb_w, ls->games[si].thumb_h);
        } else {
            draw_round_rect_img(left_x, top_y, thumb_w, thumb_h, 10.0f,
                                COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
        }

        draw_text_title_scaled(ls->games[si].title, info_x, top_y,
                               COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);

        char byline[96];
        snprintf(byline, sizeof(byline), "by %s", ls->games[si].creator);
        draw_text_small_scaled(byline, info_x, top_y + 34.0f,
                               COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);

        char stats[128];
        if (ls->games[si].local_path[0]) {
            snprintf(stats, sizeof(stats), "Offline place");
        } else if (ls->games[si].playing_now > 0) {
            snprintf(stats, sizeof(stats), "%d plays,  %d playing now",
                     ls->games[si].plays, ls->games[si].playing_now);
            } else {
            snprintf(stats, sizeof(stats), "%d plays", ls->games[si].plays);
        }
        draw_text_small_scaled(stats, info_x, top_y + 56.0f,
                               COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);

        float btn_y = top_y + 92.0f;
        float play_w = 160.0f, play_h = 42.0f;
        draw_round_rect_img(info_x, btn_y, play_w, play_h, 8.0f, COL_LIME_R, COL_LIME_G, COL_LIME_B);
        {
            const char* lab = ls->games[si].local_path[0] ? "Play Offline" : "Play";
            float tw = measure_text(lab);
            draw_text_scaled(lab, info_x + (play_w - tw) * 0.5f, btn_y + 11.0f,
                             COL_ON_LIME_R, COL_ON_LIME_G, COL_ON_LIME_B, vwidth, vheight);
        }

        // Rate game
        if (!ls->games[si].local_path[0]) {
        float vote_x = info_x + play_w + 14.0f;
        float vote_w = 72.0f, vote_h = 42.0f;
        bool liked = ls->games[si].user_rating == 1;
        bool disliked = ls->games[si].user_rating == -1;
        if (liked) {
            draw_round_rect_img(vote_x, btn_y, vote_w, vote_h, 8.0f, COL_LIME_R, COL_LIME_G, COL_LIME_B);
        } else {
            draw_round_rect_img(vote_x, btn_y, vote_w, vote_h, 8.0f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
        }
        {
            char lab[24];
            snprintf(lab, sizeof(lab), "+ %d", ls->games[si].likes);
            float tw = measure_text_small(lab);
            draw_text_small_scaled(lab, vote_x + (vote_w - tw) * 0.5f, btn_y + 13.0f,
                                   liked ? COL_ON_LIME_R : COL_TEXT_R,
                                   liked ? COL_ON_LIME_G : COL_TEXT_G,
                                   liked ? COL_ON_LIME_B : COL_TEXT_B, vwidth, vheight);
        }
        float dislike_x = vote_x + vote_w + 8.0f;
        if (disliked) {
            draw_round_rect_img(dislike_x, btn_y, vote_w, vote_h, 8.0f, 0.85f, 0.35f, 0.35f);
        } else {
            draw_round_rect_img(dislike_x, btn_y, vote_w, vote_h, 8.0f, COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
        }
        {
            char lab[24];
            snprintf(lab, sizeof(lab), "- %d", ls->games[si].dislikes);
            float tw = measure_text_small(lab);
            draw_text_small_scaled(lab, dislike_x + (vote_w - tw) * 0.5f, btn_y + 13.0f,
                                   COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }
        }

        float desc_y = top_y + thumb_h + 24.0f;
        draw_text_scaled("About", left_x, desc_y, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        const char* desc = ls->games[si].description[0] ? ls->games[si].description : "No description.";
        // Wrap into 2 lines
        // TODO: Let infinite wraps happen, and add servers display...
        char line1[120], line2[120];
        line1[0] = line2[0] = '\0';
        {
            size_t n = strlen(desc);
            if (n <= 90) {
                strncpy(line1, desc, 119); line1[119] = '\0';
            } else {
                strncpy(line1, desc, 90); line1[90] = '\0';
                // break at last space if possible
                for (int k = 89; k > 60; k--) {
                    if (line1[k] == ' ') { line1[k] = '\0'; break; }
                }
                const char* rest = desc + strlen(line1);
                while (*rest == ' ') rest++;
                strncpy(line2, rest, 119); line2[119] = '\0';
                if (strlen(line2) > 95) { line2[92] = '.'; line2[93] = '.'; line2[94] = '.'; line2[95] = '\0'; }
            }
        }
        draw_text_small_scaled(line1, left_x, desc_y + 28.0f,
                               COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
        if (line2[0]) {
            draw_text_small_scaled(line2, left_x, desc_y + 48.0f,
                                   COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);
        }
        (void)info_w;

    if (ls->error[0]) {
            draw_text_scaled(ls->error, 30.0f, (float)IMG_H - 28.0f,
                             COL_ERR_R, COL_ERR_G, COL_ERR_B, vwidth, vheight);
        }
        } // end detail_fetched content
    }

    // Logout confirm
    if (ls->logout_confirm) {
        draw_rect_img_a(0.0f, 0.0f, (float)IMG_W, (float)IMG_H, 0.12f, 0.16f, 0.04f, 0.42f);
        float panel_w = 380.0f, panel_h = 168.0f;
        float panel_x = ((float)IMG_W - panel_w) * 0.5f;
        float panel_y = ((float)IMG_H - panel_h) * 0.5f;
        draw_round_border_img(panel_x, panel_y, panel_w, panel_h, 12.0f, 2.0f,
                              COL_BG_R, COL_BG_G, COL_BG_B,
                              COL_LIME_R, COL_LIME_G, COL_LIME_B);
        // Lime accent bar on the left (same as Avatar editor)
        draw_rect_img(panel_x, panel_y + 10.0f, 4.0f, panel_h - 20.0f,
                      COL_LIME_R, COL_LIME_G, COL_LIME_B);
        const char* title = "Log out?";
        float tw = measure_text(title);
        draw_text_scaled(title, panel_x + (panel_w - tw) * 0.5f, panel_y + 30.0f,
                         COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        const char* sub = "You will need to sign in again.";
        float sw = measure_text_small(sub);
        draw_text_small_scaled(sub, panel_x + (panel_w - sw) * 0.5f, panel_y + 62.0f,
                               COL_MUTED_R, COL_MUTED_G, COL_MUTED_B, vwidth, vheight);

        float btn_w = 130.0f, btn_h = 40.0f, gap = 16.0f;
        float row_w = btn_w * 2.0f + gap;
        float cancel_x = panel_x + (panel_w - row_w) * 0.5f;
        float yes_x = cancel_x + btn_w + gap;
        float btn_y = panel_y + panel_h - 58.0f;

        draw_round_rect_img(cancel_x, btn_y, btn_w, btn_h, 8.0f,
                            COL_SOFT_R, COL_SOFT_G, COL_SOFT_B);
        {
            const char* lab = "Cancel";
            float lw = measure_text(lab);
            draw_text_scaled(lab, cancel_x + (btn_w - lw) * 0.5f, btn_y + 10.0f,
                             COL_TEXT_R, COL_TEXT_G, COL_TEXT_B, vwidth, vheight);
        }
        draw_round_rect_img(yes_x, btn_y, btn_w, btn_h, 8.0f,
                            COL_ERR_R, COL_ERR_G, COL_ERR_B);
        {
            const char* lab = "Log out";
            float lw = measure_text(lab);
            draw_text_scaled(lab, yes_x + (btn_w - lw) * 0.5f, btn_y + 10.0f,
                             1.0f, 1.0f, 1.0f, vwidth, vheight);
        }
    }

    glEnable(GL_DEPTH_TEST);
}

void login_screen_on_key(LoginScreen* ls, int keycode, bool shift, bool ctrl) {
    if (ls->update_required) {
        if (keycode == 13)
            login_open_downloads();
        return;
    }
    if (ls->logout_confirm) {
        if (keycode == 27) { // Escape key
            ls->logout_confirm = false;
            return;
        }
        if (keycode == 13) { // Enter confirms
            login_perform_logout(ls);
            return;
        }
        return;
    }
    if (ls->phase == 0) {
        if (keycode == 27 && ls->awaiting_2fa) {
            ls->awaiting_2fa = false;
            ls->totp_challenge[0] = '\0';
            ls->password[0] = '\0';
            ls->password_len = 0;
            ls->error[0] = '\0';
            ls->active_field = 1;
            return;
        }
        if (keycode == 13) {
            ls->submitted = true;
        } else if (keycode == 9) {
            if (!ls->awaiting_2fa)
                ls->active_field = (ls->active_field == 0) ? 1 : 0;
            else
                ls->active_field = 1;
            TextEdit* e = (ls->active_field == 0) ? &s_login_user_edit : &s_login_pass_edit;
            int nlen = (ls->active_field == 0) ? ls->username_len : ls->password_len;
            te_select_all(e, nlen);
        } else if (ls->active_field >= 0) {
            TextEdit* e = (ls->active_field == 0) ? &s_login_user_edit : &s_login_pass_edit;
            char* buf = (ls->active_field == 0) ? ls->username : ls->password;
            int* nlen = (ls->active_field == 0) ? &ls->username_len : &ls->password_len;
            int cap = 64;
            te_clamp(e, *nlen);
            if (ctrl && (keycode == 65 || keycode == 'a' || keycode == 'A')) {
                te_select_all(e, *nlen);
            } else if (keycode == 37) {
                te_move_left(e, buf, *nlen, shift, ctrl);
            } else if (keycode == 39) {
                te_move_right(e, buf, *nlen, shift, ctrl);
            } else if (keycode == 36) {
                te_move_home(e, buf, *nlen, shift, ctrl);
            } else if (keycode == 35) {
                te_move_end(e, buf, *nlen, shift, ctrl);
            } else if (keycode == 8) {
                te_backspace(buf, nlen, e, ctrl);
            } else if (keycode == 46) {
                te_delete_fwd(buf, nlen, e, ctrl);
            }
            (void)cap;
        }
    } else if (ls->phase == 2) {
        if (keycode == 13) {
            if (ls->detail_fetched && !ls->detail_loading)
            ls->ready_to_play = true;
        } else if (keycode == 27 || keycode == 8) {
            ls->phase = 1;
            ls->detail_fetched = false;
            ls->detail_loading = false;
            ls->error[0] = '\0';
        }
    } else if (ls->phase == 1) {
        int cols = games_grid_cols();
        if (keycode == 13) {
            if (ls->game_count > 0) {
                ls->phase = 2;
                ls->detail_fetched = false;
                ls->detail_loading = false;
            }
        } else if (keycode == 9) {
            login_set_games_tab(ls, ls->games_tab + 1);
        } else if (keycode == 37) {
            if (ls->selected_game > 0) ls->selected_game--;
            games_ensure_selection_visible(ls);
        } else if (keycode == 39) {
            if (ls->selected_game < ls->game_count - 1) ls->selected_game++;
            games_ensure_selection_visible(ls);
        } else if (keycode == 38) {
            if (ls->selected_game >= cols) ls->selected_game -= cols;
            games_ensure_selection_visible(ls);
        } else if (keycode == 40) {
            if (ls->selected_game + cols < ls->game_count) ls->selected_game += cols;
            games_ensure_selection_visible(ls);
        }
    }
}

void login_screen_on_mouseup(LoginScreen* ls) {
    ls->ignore_pointer_until_up = false;
    if (!ls->games_drag_active) return;

    const InputState* in = input_get_state();
    int ix = 0, iy = 0;
    screen_to_image((int)in->mouse_x, (int)in->mouse_y, &ix, &iy);

    if (ls->phase == 1 && !ls->games_drag_moved && ls->games_fetched && !ls->games_loading) {
        int hit = games_hit_card(ls, ls->games_drag_start_x, ls->games_drag_start_y);
        if (hit < 0) hit = games_hit_card(ls, (float)ix, (float)iy);
        if (hit >= 0) {
            ls->selected_game = hit;
            ls->phase = 2;
            ls->detail_fetched = false;
            ls->detail_loading = false;
            ls->error[0] = '\0';
        }
    }
    ls->games_drag_active = false;
    ls->games_drag_moved = false;
    ls->games_drag_mode = 0;
}

void login_screen_on_mousedown(LoginScreen* ls, int x, int y) {
    ls->ignore_pointer_until_up = false;

    int ix = (int)(((float)(x - g_hit_ox) * (float)IMG_W) / (float)g_hit_w);
    int iy = (int)(((float)(y - g_hit_oy) * (float)IMG_H) / (float)g_hit_h);
    x = ix;
    y = iy;

    if (ls->update_required) {
        login_open_downloads();
        return;
    }

    if (ls->logout_confirm) {
        float panel_x, panel_y, panel_w, panel_h, cancel_x, yes_x, btn_y, btn_w, btn_h;
        logout_confirm_layout(&panel_x, &panel_y, &panel_w, &panel_h,
                              &cancel_x, &yes_x, &btn_y, &btn_w, &btn_h);
        float fx = (float)x, fy = (float)y;
        if (fx >= yes_x && fx <= yes_x + btn_w && fy >= btn_y && fy <= btn_y + btn_h) {
            login_perform_logout(ls);
            return;
        }
        if (fx >= cancel_x && fx <= cancel_x + btn_w && fy >= btn_y && fy <= btn_y + btn_h) {
            ls->logout_confirm = false;
            return;
        }
        if (fx < panel_x || fx > panel_x + panel_w || fy < panel_y || fy > panel_y + panel_h) {
            ls->logout_confirm = false;
        }
        return;
    }

    {
        int chip = nav_chip_hit(ls, (float)x, (float)y);
        if (chip == NAV_CHIP_CATALOG) {
            ls->want_catalog_ui = true;
            return;
        }
        if (chip == NAV_CHIP_AVATAR) {
            ls->want_avatar_editor = true;
            return;
        }
        if (chip == NAV_CHIP_LOGOUT) {
            ls->logout_confirm = true;
            return;
        }
        if (chip == NAV_CHIP_THEME) {
            game_menu_set_dark_mode(!ui_theme_is_dark());
            return;
        }
    }

    if (login_banner_hit(ls, (float)x, (float)y)) {
        if (ls->banner_link[0])
            platform_open_url(ls->banner_link);
        return;
    }

    if (ls->phase == 0) {
        if ((float)x >= LOGIN_FORM_X && (float)x <= LOGIN_FORM_X + LOGIN_FORM_W &&
            (float)y >= login_ly(ls, LOGIN_USER_Y) && (float)y <= login_ly(ls, LOGIN_USER_Y) + LOGIN_USER_H) {
            if (!ls->awaiting_2fa) {
                ls->active_field = 0;
                const InputState* in = input_get_state();
                bool shift = in && in->key_shift;
                float local_x = (float)x - (LOGIN_FORM_X + 12.0f);
                int hit = te_hit_x(ls->username, 0, ls->username_len, local_x, TXT_BODY_H);
                te_mouse_down(&s_login_user_edit, ls->username, ls->username_len, hit, shift, (float)platform_get_time());
                s_login_field_drag = true;
            }
        } else if ((float)x >= LOGIN_FORM_X && (float)x <= LOGIN_FORM_X + LOGIN_FORM_W &&
                   (float)y >= login_ly(ls, LOGIN_PASS_Y) && (float)y <= login_ly(ls, LOGIN_PASS_Y) + LOGIN_PASS_H) {
            ls->active_field = 1;
            {
                const InputState* in = input_get_state();
                bool shift = in && in->key_shift;
                float local_x = (float)x - (LOGIN_FORM_X + 12.0f);
                int hit;
                if (ls->awaiting_2fa) {
                    hit = te_hit_x(ls->password, 0, ls->password_len, local_x, TXT_BODY_H);
                } else {
                    char stars[64];
                    int pl = ls->password_len > 63 ? 63 : ls->password_len;
                    memset(stars, '*', (size_t)pl);
                    stars[pl] = '\0';
                    hit = te_hit_x(stars, 0, pl, local_x, TXT_BODY_H);
                }
                te_mouse_down(&s_login_pass_edit, ls->password, ls->password_len, hit, shift, (float)platform_get_time());
                s_login_field_drag = true;
            }
        } else if ((float)x >= LOGIN_FORM_X && (float)x <= LOGIN_FORM_X + LOGIN_FORM_W &&
                   (float)y >= login_ly(ls, LOGIN_BTN_Y) && (float)y <= login_ly(ls, LOGIN_BTN_Y) + LOGIN_BTN_H) {
            ls->active_field = -1;
            ls->submitted = true;
        } else if ((float)x >= LOGIN_FORM_X && (float)x <= LOGIN_FORM_X + LOGIN_FORM_W &&
                   (float)y >= login_ly(ls, LOGIN_GUEST_Y) && (float)y <= login_ly(ls, LOGIN_GUEST_Y) + LOGIN_GUEST_H) {
            ls->active_field = -1;
            ls->play_as_guest = true;
            ls->password_len = 0;
            ls->username_len = 0;
            ls->username[0] = '\0';
            ls->password[0] = '\0';
            ls->error[0] = '\0';
            ls->awaiting_2fa = false;
            ls->totp_challenge[0] = '\0';
            ls->submitted = true;
        } else if ((float)y >= login_ly(ls, LOGIN_CHECK_Y) && (float)y <= login_ly(ls, LOGIN_CHECK_Y) + LOGIN_CHECK_SIZE + 8.0f &&
                   (float)x >= LOGIN_FORM_X &&
                   (float)x <= LOGIN_FORM_X + LOGIN_FORM_W) {
            ls->skip_benchmark = !ls->skip_benchmark;
            ls->skip_benchmark_dirty = true;
            ls->active_field = -1;
        } else {
            // Tap outside...
            ls->active_field = -1;
        }
    } else if (ls->phase == 2 && ls->selected_game >= 0 && ls->selected_game < ls->game_count) {
        int si = ls->selected_game;
        // Back
        if ((float)x >= 40.0f && (float)x <= 120.0f &&
            (float)y >= login_chrome_bottom(ls) + 10.0f &&
            (float)y <= login_chrome_bottom(ls) + 36.0f) {
            ls->phase = 1;
            ls->detail_fetched = false;
            ls->detail_loading = false;
            ls->error[0] = '\0';
            return;
        }
        if (ls->detail_loading || !ls->detail_fetched) return;
        float thumb_w = 360.0f;
        float left_x = 40.0f;
        float top_y = login_chrome_bottom(ls) + 48.0f;
        float info_x = left_x + thumb_w + 28.0f;
        float btn_y = top_y + 92.0f;
        float play_w = 160.0f, play_h = 42.0f;
        if ((float)x >= info_x && (float)x <= info_x + play_w &&
            (float)y >= btn_y && (float)y <= btn_y + play_h) {
            ls->ready_to_play = true;
            return;
        }
        float vote_x = info_x + play_w + 14.0f;
        float vote_w = 72.0f, vote_h = 42.0f;
        float dislike_x = vote_x + vote_w + 8.0f;
        if (!ls->games[si].local_path[0] &&
            (float)x >= vote_x && (float)x <= vote_x + vote_w &&
            (float)y >= btn_y && (float)y <= btn_y + vote_h) {
            if (!ls->session_token[0]) {
                snprintf(ls->error, sizeof(ls->error), "Log in to vote.");
                return;
            }
            int likes = ls->games[si].likes, dislikes = ls->games[si].dislikes, ur = 0;
            if (auth_rate_game(ls->session_token, ls->games[si].id, 1, &likes, &dislikes, &ur)) {
                ls->games[si].likes = likes;
                ls->games[si].dislikes = dislikes;
                ls->games[si].user_rating = ur;
                ls->error[0] = '\0';
            } else {
                snprintf(ls->error, sizeof(ls->error), "Could not submit vote.");
            }
            return;
        }
        if (!ls->games[si].local_path[0] &&
            (float)x >= dislike_x && (float)x <= dislike_x + vote_w &&
            (float)y >= btn_y && (float)y <= btn_y + vote_h) {
            if (!ls->session_token[0]) {
                snprintf(ls->error, sizeof(ls->error), "Log in to vote.");
                return;
            }
            int likes = ls->games[si].likes, dislikes = ls->games[si].dislikes, ur = 0;
            if (auth_rate_game(ls->session_token, ls->games[si].id, -1, &likes, &dislikes, &ur)) {
                ls->games[si].likes = likes;
                ls->games[si].dislikes = dislikes;
                ls->games[si].user_rating = ur;
                ls->error[0] = '\0';
            } else {
                snprintf(ls->error, sizeof(ls->error), "Could not submit vote.");
            }
            return;
        }
    } else if (ls->phase == 1) {
        bool home_user = login_is_home_user(ls);
        float scroll_off = home_user ? ls->games_scroll_y : 0.0f;

        if (home_user && ls->home_fetched) {
            // This is a very nice feature, but buggy!
            float fy = login_home_friends_y(ls) - scroll_off;
            float frow_y = fy + HOME_HEAD_H + HOME_HEAD_GAP;
            if (ls->friend_count > 0 &&
                (float)y >= frow_y - 4.0f && (float)y <= frow_y + FRIEND_AVATAR_D + 20.0f) {
                for (int i = 0; i < ls->friend_count && i < 16; i++) {
                    float ccx = GAMES_GRID_X + (float)i * FRIEND_CHIP_W;
                    if ((float)x >= ccx && (float)x <= ccx + FRIEND_CHIP_W) {
                        if (ls->friends[i].current_game_id > 0) {
                            memset(&ls->games[0], 0, sizeof(ls->games[0]));
                            ls->games[0].id = ls->friends[i].current_game_id;
                            if (ls->friends[i].playing_title[0]) {
                                strncpy(ls->games[0].title, ls->friends[i].playing_title,
                                       sizeof(ls->games[0].title) - 1);
                            } else {
                                strncpy(ls->games[0].title, "Game", sizeof(ls->games[0].title) - 1);
                            }
                            if (ls->game_count < 1) ls->game_count = 1;
                            ls->selected_game = 0;
                            ls->sel_draw_valid = false;
                            ls->game_id = ls->friends[i].current_game_id;
                            ls->phase = 2;
                            ls->detail_fetched = false;
                            ls->detail_loading = false;
                            ls->error[0] = '\0';
                        }
                        return;
                    }
                }
            }

            if (ls->continue_count > 0) {
                float cy0 = login_home_continue_y(ls) - scroll_off;
                float rail_y = cy0 + HOME_HEAD_H + HOME_HEAD_GAP;
                if ((float)y >= rail_y && (float)y <= rail_y + HOME_RAIL_THUMB_H) {
                    int show = ls->continue_count;
                    if (show > HOME_RAIL_MAX_VISIBLE) show = HOME_RAIL_MAX_VISIBLE;
                    for (int i = 0; i < show; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        if ((float)x >= rx && (float)x <= rx + HOME_RAIL_THUMB_W) {
                            login_open_game_ref(ls, ls->continue_games[i].id, ls->continue_games[i].title,
                                                ls->continue_games[i].creator, ls->continue_games[i].thumbnail);
                            return;
                        }
                    }
                }
            }

            if (ls->gotw_count > 0) {
                float gy0 = login_home_gotw_y(ls) - scroll_off;
                float rail_y = gy0 + HOME_HEAD_H + HOME_HEAD_GAP;
                if ((float)y >= rail_y && (float)y <= rail_y + HOME_RAIL_THUMB_H) {
                    int show = ls->gotw_count;
                    if (show > HOME_RAIL_MAX_VISIBLE) show = HOME_RAIL_MAX_VISIBLE;
                    for (int i = 0; i < show; i++) {
                        float rx = GAMES_GRID_X + (float)i * (HOME_RAIL_THUMB_W + HOME_RAIL_GAP_X);
                        if ((float)x >= rx && (float)x <= rx + HOME_RAIL_THUMB_W) {
                            login_open_game_ref(ls, ls->gotw_games[i].id, ls->gotw_games[i].title,
                                                ls->gotw_games[i].creator, ls->gotw_games[i].thumbnail);
                            return;
                        }
                    }
                }
            }
        }

        int ntabs = games_tab_count(ls);
        float ty = games_tab_y(ls) - scroll_off;
#ifdef __ANDROID__
        float tab_pad = 12.0f;
#else
        float tab_pad = 0.0f;
#endif
        for (int t = 0; t < ntabs; t++) {
            float tx, tw;
            games_tab_rect(ls, t, &tx, &tw);
            if ((float)x >= tx - tab_pad && (float)x <= tx + tw + tab_pad &&
                (float)y >= ty - tab_pad && (float)y <= ty + GAMES_TAB_H + tab_pad) {
                login_set_games_tab(ls, t);
                return;
            }
        }

        // Allow drag, even while loading.
        if (ls->games_loading && !login_is_home_user(ls)) return;

        if (games_point_in_grid(ls, (float)x, (float)y)) {
            ls->games_drag_active = true;
            ls->games_drag_moved = false;
            ls->games_drag_last_x = (float)x;
            ls->games_drag_last_y = (float)y;
            ls->games_drag_start_x = (float)x;
            ls->games_drag_start_y = (float)y;
            ls->games_drag_mode = (login_is_home_user(ls) && home_games_point_in_rail(ls, (float)x, (float)y)) ? 1 : 0;
        }
    }
}

bool login_screen_on_scroll(LoginScreen* ls, float delta) {
    if (!ls || ls->update_required || ls->phase != 1 || ls->games_loading) return false;
    // when over the games rail we do horizontal scrolling.
    if (login_is_home_user(ls) && ls->games_fetched) {
        const InputState* in = input_get_state();
        int ix = 0, iy = 0;
        screen_to_image((int)in->mouse_x, (int)in->mouse_y, &ix, &iy);
        if (home_games_point_in_rail(ls, (float)ix, (float)iy)) {
            ls->games_rail_scroll_target += delta * 56.0f;
            home_games_clamp_scroll_x(ls);
            return true;
        }
    }
    ls->games_scroll_target += delta * 56.0f;
    games_clamp_scroll(ls);
    return true;
}


void login_screen_on_char(LoginScreen* ls, unsigned int codepoint) {
    if (ls->update_required) return;
    if (codepoint < 32 || codepoint > 126) return;
    if (ls->awaiting_2fa)
        ls->active_field = 1;
    TextEdit* e = NULL;
    char* buf = NULL;
    int* nlen = NULL;
    int cap = 64;
    if (ls->active_field == 0) {
        e = &s_login_user_edit;
        buf = ls->username;
        nlen = &ls->username_len;
    } else if (ls->active_field == 1) {
        e = &s_login_pass_edit;
        buf = ls->password;
        nlen = &ls->password_len;
    }
    if (!e || !buf || !nlen) return;
    te_insert_cp(buf, nlen, cap, e, codepoint);
}

bool login_screen_copy(LoginScreen* ls) {
    if (!ls || ls->phase != 0 || ls->active_field < 0) return false;
    if (ls->active_field == 0) {
        te_copy(ls->username, &s_login_user_edit, ls->username_len, true);
        return true;
    }
    te_copy(ls->password, &s_login_pass_edit, ls->password_len, true);
    return true;
}

bool login_screen_cut(LoginScreen* ls) {
    if (!ls || ls->phase != 0 || ls->active_field < 0) return false;
    if (ls->active_field == 0) {
        te_cut(ls->username, &ls->username_len, &s_login_user_edit, false);
        return true;
    }
    te_cut(ls->password, &ls->password_len, &s_login_pass_edit, false);
    return true;
}

bool login_screen_paste(LoginScreen* ls) {
    if (!ls || ls->phase != 0 || ls->active_field < 0) return false;
    const char* clip = platform_clipboard_get();
    if (ls->active_field == 0) {
        te_paste(ls->username, &ls->username_len, 64, &s_login_user_edit, clip, false);
        return true;
    }
    te_paste(ls->password, &ls->password_len, 64, &s_login_pass_edit, clip, false);
    return true;
}

bool login_screen_select_all(LoginScreen* ls) {
    if (!ls || ls->phase != 0 || ls->active_field < 0) return false;
    if (ls->active_field == 0) {
        te_select_all(&s_login_user_edit, ls->username_len);
        return true;
    }
    te_select_all(&s_login_pass_edit, ls->password_len);
    return true;
}

#endif // !__EMSCRIPTEN__ (yeah f you emscripten. you suck!)
