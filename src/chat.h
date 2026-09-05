/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: chat.h                                                                              |
|   Purpose: in-game chat                                                                     |
\*-------------------------------------------------------------------------------------------*/

#ifndef CHAT_H
#define CHAT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "text_edit.h"

#define CHAT_MAX_INPUT 128
#define CHAT_MAX_MESSAGES 20
#define CHAT_MSG_LEN 160
#define CHAT_PL_MAX 48
#define CHAT_PL_STAT_MAX 8
#define CHAT_PL_TEAM_MAX 16

typedef struct {
    const char* name;
    uint8_t badges;
    uint32_t player_id;
    int team_idx;
    float stats[CHAT_PL_STAT_MAX];
    int stat_count;
    float name_r, name_g, name_b;
    bool has_name_color;
} ChatPlayerListEntry;
#define CHAT_MAX_NAME_COLORS 32

typedef struct {
    char messages[CHAT_MAX_MESSAGES][CHAT_MSG_LEN];
    uint32_t msg_sender[CHAT_MAX_MESSAGES];
    bool msg_system[CHAT_MAX_MESSAGES];
    float msg_age[CHAT_MAX_MESSAGES];
    float msg_fade[CHAT_MAX_MESSAGES];
    int msg_count;
    int msg_start;

    char input_buf[CHAT_MAX_INPUT];
    int input_len;
    TextEdit edit;
    bool open;
    bool focused;
    bool just_focused;
    bool opened_by_slash;

    unsigned int font_texture;
    unsigned int text_vao;
    unsigned int text_vbo;
    unsigned int text_shader;
    int u_projection;
    int u_tex;
    int u_color;

    unsigned int quad_shader;
    int quad_u_projection;
    int quad_u_tex;
    int quad_u_alpha;
    int quad_u_tint;

    bool initialized;

    float fade_timer;

    unsigned int bubble_texture;
    unsigned int bubble_bottom_tex;
    unsigned int nineslice_tex;
    unsigned int chat_closed_tex;
    unsigned int chat_open_tex;
    unsigned int chat_unread_tex;
    unsigned int menu_tex;

    unsigned int badge_creator;
    unsigned int badge_verified;
    unsigned int badge_shield;
    unsigned int badge_tester;

    unsigned int music_icon_tex;
    unsigned int white_tex;
    unsigned int circle_tex;

    float ui_scale;
    bool unread;
    int scroll_offset;

    char local_username[32];

    struct {
        char name[32];
        float r, g, b;
        bool active;
    } name_colors[CHAT_MAX_NAME_COLORS];

    float panel_anim;

    float pl_anim_h;
    float pl_anim_w;
    float pl_entry_fade[CHAT_PL_MAX];
    char pl_entry_names[CHAT_PL_MAX][32];
    bool pl_entry_header[CHAT_PL_MAX];
    int pl_entry_count;

    float pl_hit_x, pl_hit_y, pl_hit_w, pl_hit_h;
    float pl_hit_pad, pl_hit_header_h, pl_hit_line_h;
    int pl_hit_count;

    char nt_names[CHAT_PL_MAX][32];
    float nt_x0[CHAT_PL_MAX], nt_y0[CHAT_PL_MAX], nt_x1[CHAT_PL_MAX], nt_y1[CHAT_PL_MAX];
    int nt_hit_count;

    int emoji_sel;
    float emoji_hit_x, emoji_hit_y, emoji_hit_w, emoji_hit_row_h;
    int emoji_hit_count;

    float input_hit_x, input_hit_y, input_hit_w, input_hit_h;
    float input_hit_pad, input_hit_line_h, input_hit_px, input_hit_max_w;
    bool input_hit_valid;

    float panel_hit_x, panel_hit_y, panel_hit_w, panel_hit_h;
    bool panel_hit_valid;
} Chat;

void chat_init(Chat* c);
void chat_shutdown(Chat* c);
void chat_recreate_gl(Chat* c, bool context_alive);

void chat_set_local_username(Chat* c, const char* name);

void chat_name_color(const char* name, float* r, float* g, float* b);
void chat_resolve_name_color(Chat* c, const char* name, float* r, float* g, float* b);
void chat_set_name_color_override(Chat* c, const char* name, bool enabled, float r, float g, float b);
void chat_clear_name_color_overrides(Chat* c);

void chat_add_message(Chat* c, const char* msg);

void chat_add_message_from(Chat* c, const char* msg, uint32_t sender_id);

void chat_add_system_message(Chat* c, const char* msg);

bool chat_on_key(Chat* c, int keycode, bool shift, bool ctrl);

bool chat_on_char(Chat* c, unsigned int codepoint);

void chat_set_input_text(Chat* c, const char* utf8);

bool chat_copy_input(Chat* c);
bool chat_cut_input(Chat* c);
bool chat_select_all_input(Chat* c);
bool chat_paste_text(Chat* c, const char* utf8);

bool chat_on_click(Chat* c, float x, float y, int screen_width, int screen_height);

bool chat_on_scroll(Chat* c, float x, float y, float delta,
                    int screen_width, int screen_height);

void chat_blur(Chat* c);

void chat_update(Chat* c, float dt);

void chat_render(Chat* c, int screen_width, int screen_height);

void chat_draw_billboard(Chat* c, const char* text, uint8_t badges,
                         float world_x, float world_y, float world_z,
                         const float* view, const float* projection,
                         int screen_width, int screen_height);
void chat_draw_billboard_colored(Chat* c, const char* text, uint8_t badges,
                                 float world_x, float world_y, float world_z,
                                 const float* view, const float* projection,
                                 int screen_width, int screen_height,
                                 float nr, float ng, float nb);

bool chat_billboard_hit_test(Chat* c, const char* text, uint8_t badges,
                             float world_x, float world_y, float world_z,
                             const float* view, const float* projection,
                             int screen_width, int screen_height,
                             float mx, float my,
                             float* out_screen_x, float* out_screen_y);
void chat_nametag_hits_clear(Chat* c);
int chat_nametag_hit_test(Chat* c, float mx, float my, char* out_name, size_t name_cap);

typedef struct {
    const char* text;
    uint32_t sender_id;
    float age;
    float fade;
} ChatBubbleMsg;

#define CHAT_BUBBLE_MAX_AGE 5.0f

int chat_collect_recent_bubbles(Chat* c, float max_age, ChatBubbleMsg* out, int max_out);

const char* chat_get_latest_bubble(Chat* c, uint32_t* sender_id, float* age);

float chat_draw_bubble(Chat* c, const char* text, float world_x, float world_y, float world_z,
                       const float* view, const float* projection,
                       int screen_width, int screen_height,
                       bool show_pointer, float screen_lift,
                       float appear_fade, float age);

float chat_render_player_list(Chat* c, const char** names, const uint8_t* badges, int count,
                              int screen_width, int screen_height);

float chat_render_player_list_ex(Chat* c, const ChatPlayerListEntry* entries, int count,
                                 const char* const* stat_names, int stat_name_count,
                                 int screen_width, int screen_height);

int chat_player_list_hit_test(Chat* c, float x, float y,
                              float* out_card_x, float* out_card_y);

typedef struct {
    const char* name;
    unsigned int icon_tex;
    int icon_w, icon_h;
    bool selected;
} ToolSlotInfo;
void chat_render_toolbar(Chat* c, const ToolSlotInfo* tools, int count,
                         float bottom_lift,
                         int screen_width, int screen_height);

void chat_render_tool_cooldown(Chat* c, float ready_frac, int tool_count,
                               float bottom_lift,
                               int screen_width, int screen_height);

int chat_toolbar_hit_test(Chat* c, float x, float y, int tool_count,
                          float bottom_lift,
                          int screen_width, int screen_height);

void chat_render_health_bar(Chat* c, int health, int max_health,
                            float list_left_x, float list_top_y,
                            int screen_width, int screen_height);

void chat_render_hud_text(Chat* c, const char* text, float x, float y, float scale,
                          int screen_width, int screen_height);

void chat_render_banner(Chat* c, const char* text, float cx, float cy, float scale,
                        float alpha, float y_offset,
                        int screen_width, int screen_height);

float chat_music_credit_stack_height(Chat* c, float size_scale);
void chat_render_music_credit(Chat* c, const char* title, const char* author,
                              float alpha, float y_offset, float size_scale,
                              const float* wave_levels, int wave_count,
                              int screen_width, int screen_height);

const char* chat_get_pending_send(Chat* c);

void chat_clear_pending(Chat* c);

void chat_draw_tex_uv(Chat* c, unsigned int tex,
                      float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a,
                      int screen_width, int screen_height);

void chat_draw_tex_wedge(Chat* c, unsigned int tex,
                         float cx, float cy, float radius,
                         float ang0, float ang1,
                         float r, float g, float b, float a,
                         int screen_width, int screen_height);

#endif
