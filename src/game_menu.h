/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: game_menu.h                                                                         |
|   Purpose: pause menu                                                                       |
\*-------------------------------------------------------------------------------------------*/

#ifndef GAME_MENU_H
#define GAME_MENU_H

#include <stdbool.h>

typedef enum {
    GFX_QUALITY_POTATO = 0,
    GFX_QUALITY_LOW = 1,
    GFX_QUALITY_MEDIUM = 2,
    GFX_QUALITY_HIGH = 3,
    GFX_QUALITY_ULTRA = 4,
    GFX_QUALITY_SUPER = 5,
    GFX_QUALITY_AUTO = 6,
    GFX_QUALITY_MANUAL = 7
} GfxQuality;

typedef enum {
    GFX_GLOW_LEAK_NONE = 0,
    GFX_GLOW_LEAK_DISTANCE = 1
} GfxGlowLeakMode;

typedef enum {
    GFX_LIGHTING_SHADOWMAP = 0,
    GFX_LIGHTING_VOXEL = 1
} GfxLightingMode;

typedef enum {
    MENU_PAGE_MAIN = 0,
    MENU_PAGE_SETTINGS_HUB = 1,
    MENU_PAGE_SETTINGS = 2,
    MENU_PAGE_SETTINGS_AUDIO = 3,
    MENU_PAGE_SETTINGS_VR = 4
} MenuPage;

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_RESUME,
    MENU_ACTION_RESPAWN,
    MENU_ACTION_LEAVE_GAME,
    MENU_ACTION_AVATAR_EDITOR,
    MENU_ACTION_CATALOG,
    MENU_ACTION_BENCHMARK
} MenuAction;

typedef struct {
    bool open;
    MenuPage page;
    GfxQuality quality;
    GfxLightingMode lighting_tech;
    int hovered_item;
    float ui_scale;
    int fps_limit;
    bool reset_enabled;
    bool fullscreen;

    bool manual_fog;
    float manual_render_scale;
    GfxGlowLeakMode manual_glow_leak;

    bool force_mobile_controls;

    int vr_turn;
    int vr_vignette;

    bool dark_mode;

    float vol_master;
    float vol_music;
    float vol_sfx;

    float menu_scroll_y;
    float menu_scroll_max;

    bool menu_drag_active;
    bool menu_drag_moved;
    float menu_drag_last_y;
    float menu_drag_start_x;
    float menu_drag_start_y;

    bool menu_click_pending;
    float menu_click_x, menu_click_y;

    bool benchmark_running;
    float benchmark_timer;
    int benchmark_frames;
    float benchmark_dt_sum;
    char benchmark_label[48];
    bool benchmark_first_run;

    bool skip_startup_benchmark;
    bool startup_benchmark_done;
    bool first_run_active;

    unsigned int nineslice_tex;
    unsigned int menu_tex;

    bool studio_playtest;
} GameMenu;

void game_menu_init(GameMenu* m, float ui_scale);
void game_menu_set_shaders(unsigned int quad_prog, int qp, int qt, int qa, int qtint,
                           unsigned int text_prog, int tp, int tt, int tc,
                           unsigned int font_tex, unsigned int vao, unsigned int vbo);
void game_menu_load_settings(GameMenu* m);
void game_menu_save_settings(GameMenu* m);
void game_menu_set_dark_mode(bool dark);
void game_menu_toggle(GameMenu* m);
MenuAction game_menu_on_click(GameMenu* m, float x, float y, int screen_width, int screen_height);
void game_menu_on_mousedown(GameMenu* m, float x, float y, int screen_width, int screen_height);
MenuAction game_menu_on_mouseup(GameMenu* m, float x, float y, int screen_width, int screen_height);
void game_menu_on_mouse_move(GameMenu* m, float x, float y, int screen_width, int screen_height);
bool game_menu_on_scroll(GameMenu* m, float delta, int screen_height);
void game_menu_update(GameMenu* m, float dt);
MenuAction game_menu_poll_pending_click(GameMenu* m, int screen_width, int screen_height);
void game_menu_render(GameMenu* m, int screen_width, int screen_height);
GfxQuality game_menu_get_effective_quality(GameMenu* m);

bool game_menu_needs_first_run(const GameMenu* m);
void game_menu_begin_first_run(GameMenu* m);
bool game_menu_first_run_active(const GameMenu* m);
void game_menu_start_benchmark(GameMenu* m, bool first_run);

#endif
