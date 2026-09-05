/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: touch_controls.h                                                                    |
|   Purpose: on-screen stick / jump / look / tap-to-fire                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef TOUCH_CONTROLS_H
#define TOUCH_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#define TC_FLAG_FORCE_LOOK  0x1u

void touch_controls_init(void);

void touch_controls_invalidate_gl(bool context_alive);
void touch_controls_set_enabled(bool enabled);
bool touch_controls_enabled(void);

bool touch_controls_pointer_down(int32_t id, float x, float y, int screen_w, int screen_h,
                                 uint32_t flags);
bool touch_controls_pointer_move(int32_t id, float x, float y);
bool touch_controls_pointer_up(int32_t id);

void touch_controls_update_pinch(uint32_t count, const int32_t* ids,
                                 const float* xs, const float* ys);
bool touch_controls_pinch_active(void);

bool touch_controls_owns_pointer(int32_t id);

void touch_controls_mark_ui_pointer(int32_t id);

bool touch_controls_is_ui_pointer(int32_t id);

bool touch_controls_clear_ui_pointer(int32_t id);

void touch_controls_render(int screen_w, int screen_h, float ui_scale);

bool touch_controls_consume_menu_press(void);

bool touch_controls_consume_shiftlock_toggle(void);

void touch_controls_set_shift_lock(bool locked);

bool touch_controls_consume_tap_fire(void);

void touch_controls_tap_fire_pos(float* out_x, float* out_y);

#endif
