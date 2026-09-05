/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar_editor.h                                                                     |
|   Purpose: in-game avatar editor overlay                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef AVATAR_EDITOR_H
#define AVATAR_EDITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "accessory.h"
#include "renderer.h"

#define AVATAR_EDITOR_MAX_ITEMS 128

typedef struct {
    int id;
    char name[48];
    char type[16];
    char image_path[192];
    char mesh_style[16];
    unsigned int thumb_tex;
    bool thumb_loaded;
    bool thumb_loading;
} AvatarEditorItem;

typedef struct {
    bool open;
    bool loading;
    bool load_deferred;
    bool need_profile_fetch;
    bool thumbs_pending;
    bool saving;
    bool dirty;
    char error[128];
    char status[64];
    float skeleton_t;

    char session_token[128];
    char host[128];

    char skin_color[8];
    int shirt, pants, head;
    int accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    int accessory;
    int package;

    char base_skin[8];
    int base_shirt, base_pants, base_head;
    int base_accessories[PW_MAX_EQUIPPED_ACCESSORIES];
    int base_accessory;

    AvatarEditorItem items[AVATAR_EDITOR_MAX_ITEMS];
    int item_count;
    int tab;
    int scroll;
    float scroll_y, scroll_max;
    bool press_armed;
    bool press_outside;
    bool list_drag_active;
    bool list_drag_moved;
    float list_drag_last_y, list_drag_start_x, list_drag_start_y;

    float panel_x, panel_y, panel_w, panel_h;
    float preview_x, preview_y, preview_w, preview_h;
    bool compact;
    float ui_s;
    float header_h;
    float close_x, close_y, close_w, close_h;
    float tab_x[4], tab_y[4], tab_w[4], tab_h;
    float grid_x, grid_y, grid_w, grid_h;
    float cell, gap;
    int cols;
    float save_x, save_y, save_w, save_h;
    float cancel_x, cancel_y, cancel_w, cancel_h;
    float spin_x, spin_y, spin_w, spin_h;
    float skin_x, skin_y, skin_cell, skin_gap;
    float btn_h;

    float cam_yaw, cam_pitch, cam_dist;
    bool auto_spin;
    bool preview_dragging;
    float drag_lx, drag_ly;
} AvatarEditor;

void avatar_editor_init(AvatarEditor* ed);
void avatar_editor_shutdown(AvatarEditor* ed);

void avatar_editor_open(AvatarEditor* ed, const char* session_token, const char* host,
                        const char* skin_color, int shirt, int pants, int head,
                        const int accessories[PW_MAX_EQUIPPED_ACCESSORIES]);
void avatar_editor_close(AvatarEditor* ed);

void avatar_editor_render(AvatarEditor* ed, Renderer* renderer,
                          int screen_w, int screen_h, float dt);
bool avatar_editor_on_mousedown(AvatarEditor* ed, float x, float y, int button);
void avatar_editor_on_mouseup(AvatarEditor* ed, float x, float y, int button);
bool avatar_editor_on_scroll(AvatarEditor* ed, float x, float y, float delta);
bool avatar_editor_on_key(AvatarEditor* ed, int keycode);
bool avatar_editor_blocks_input(const AvatarEditor* ed);
bool avatar_editor_consume_saved(AvatarEditor* ed);

const char* avatar_editor_skin(const AvatarEditor* ed);
int avatar_editor_shirt(const AvatarEditor* ed);
int avatar_editor_pants(const AvatarEditor* ed);
int avatar_editor_head(const AvatarEditor* ed);
int avatar_editor_accessory(const AvatarEditor* ed);
const int* avatar_editor_accessories(const AvatarEditor* ed);
int avatar_editor_package(const AvatarEditor* ed);

void avatar_preview_set_outfit(const char* host, const char* skin_hex,
                               int shirt, int pants, int head,
                               const int accessories[PW_MAX_EQUIPPED_ACCESSORIES],
                               int mesh_flags);
void avatar_preview_draw(Renderer* renderer,
                         float x, float y, float w, float h,
                         int screen_w, int screen_h, float dt,
                         float* cam_yaw, float* cam_pitch, float* cam_dist,
                         const Vec3* cam_target, float fov,
                         bool auto_spin, bool dragging, float* drag_lx, float* drag_ly);

#endif
