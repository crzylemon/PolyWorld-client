/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: input.h                                                                             |
|   Purpose: kb / mouse / gamepad                                                             |
\*-------------------------------------------------------------------------------------------*/

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

typedef struct {
    bool key_w;
    bool key_a;
    bool key_s;
    bool key_d;
    bool key_space;
    bool key_space_held;
    bool key_shift;
    bool key_f;
    bool key_g;
    bool key_z;
    bool key_b_held;
    bool key_f3;
    bool key_f3_held;
    bool key_f1;
    bool key_f6;
    bool key_f7;
    bool key_f8;
    bool key_f9;
    bool key_f10;
    bool key_c;
    bool key_t;
    bool key_1;
    bool key_2;
    bool key_3;
    bool key_4;
    bool key_5;
    bool key_6;
    bool key_7;
    bool key_8;
    bool key_9;
    bool mouse_left;
    bool mouse_right;
    float mouse_dx;
    float mouse_dy;
    float mouse_x;
    float mouse_y;
    float scroll_delta;

    float move_x;
    float move_y;
} InputState;

void input_init(void);
void input_pre_frame(void);
void input_post_frame(void);
const InputState* input_get_state(void);
bool input_mouse_left_held(void);

void input_on_keydown(int keycode);
void input_on_keyup(int keycode);
void input_on_mousedown(int button);
void input_on_mouseup(int button);
void input_clear_mouse_delta(void);
void input_on_mousemove(float dx, float dy);
void input_on_scroll(float delta);
void input_clear_scroll(void);
void input_set_mouse_pos(float x, float y);
void input_set_move_axes(float x, float y);
void input_release_all(void);

bool input_key_held(int keycode);
bool input_mouse_button_held(int button);

#endif
