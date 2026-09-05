/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: catalog_ui.h                                                                        |
|   Purpose: in-game catalog overlay                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef CATALOG_UI_H
#define CATALOG_UI_H

#include <stdbool.h>
#include "accessory.h"
#include "renderer.h"

#define CATALOG_UI_MAX_ITEMS 96

typedef struct {
    int id;
    char name[48];
    char type[16];
    char creator[32];
    char image_path[192];
    char mesh_style[16];
    char description[256];
    int price;
    int stock;
    int resale;
    bool owned;
    bool limited;
    bool offsale;
    bool donate;
    unsigned int thumb_tex;
    bool thumb_loaded;
    bool thumb_loading;
} CatalogUiItem;

typedef struct {
    bool open;
    bool loading;
    bool load_deferred;
    bool thumbs_pending;
    bool buying;
    bool has_more;
    bool load_more;
    char error[128];
    char status[80];
    float skeleton_t;

    char session_token[128];
    char host[128];
    char skin_color[8];
    int base_shirt, base_pants, base_head, base_package;
    int base_accs[PW_MAX_EQUIPPED_ACCESSORIES];

    int bricks;
    int tab;
    int sort;
    int page;
    int view;
    int selected;
    int item_count;
    CatalogUiItem items[CATALOG_UI_MAX_ITEMS];
    CatalogUiItem detail;

    float scroll_y;
    float scroll_max;
    bool drag_active;
    bool drag_moved;
    bool press_armed;
    bool press_outside;
    float drag_last_y;
    float drag_start_x;
    float drag_start_y;

    float panel_x, panel_y, panel_w, panel_h;
    float grid_x, grid_y, grid_w, grid_h;
    float cell, gap, label_h;
    int cols;
    bool compact;
    float ui_s;
    float header_h;
    float buy_x, buy_y, buy_w, buy_h;
    float back_x, back_y, back_w, back_h;
    float close_x, close_y, close_w, close_h;
    float sort_x, sort_y, sort_w, sort_h;
    float tab_x[5];
    float tab_y[5];
    float tab_w[5];
    float tab_row_h;
    float preview_x, preview_y, preview_w, preview_h;
    float cam_yaw, cam_pitch, cam_dist;
    Vec3 cam_target;
    float front_yaw, front_pitch, front_dist;
    float back_yaw, back_pitch, back_dist;
    bool facing_front;
    bool flip_active;
    float flip_t, flip_dur;
    float flip_from_yaw, flip_to_yaw;
    float flip_from_pitch, flip_to_pitch;
    float flip_from_dist, flip_to_dist;
    double last_flip_at;
    bool auto_spin;
    bool preview_dragging;
    bool preview_orbited;
    float preview_drag_lx, preview_drag_ly;
    float preview_down_x, preview_down_y;
} CatalogUi;

void catalog_ui_init(CatalogUi* ui);
void catalog_ui_shutdown(CatalogUi* ui);
void catalog_ui_open(CatalogUi* ui, const char* session_token, const char* host,
                     const char* skin_color, int shirt, int pants, int head,
                     const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                     int package);
void catalog_ui_close(CatalogUi* ui);

void catalog_ui_render(CatalogUi* ui, Renderer* renderer, int screen_w, int screen_h, float dt);
bool catalog_ui_on_mousedown(CatalogUi* ui, float x, float y);
void catalog_ui_on_mouseup(CatalogUi* ui, float x, float y);
bool catalog_ui_on_scroll(CatalogUi* ui, float x, float y, float delta);
bool catalog_ui_on_key(CatalogUi* ui, int keycode);
bool catalog_ui_blocks_input(const CatalogUi* ui);

#endif
