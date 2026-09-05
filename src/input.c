/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: input.c                                                                             |
|   Purpose: kb / mouse / gamepad                                                             |
\*-------------------------------------------------------------------------------------------*/

#include "input.h"
#include <string.h>

#define KEY_W     87
#define KEY_A     65
#define KEY_S     83
#define KEY_D     68
#define KEY_F     70
#define KEY_G     71
#define KEY_Z     90
#define KEY_B     66
#define KEY_SPACE 32
#define KEY_SHIFT 16
#define KEY_F3    114
#define KEY_F1    112
#define KEY_F6    117
#define KEY_F7    118
#define KEY_F8    119
#define KEY_F9    120
#define KEY_F10   121
#define KEY_C     67
#define KEY_T     84
#define KEY_1     49
#define KEY_2     50
#define KEY_3     51
#define KEY_4     52
#define KEY_5     53
#define KEY_6     54
#define KEY_7     55
#define KEY_8     56
#define KEY_9     57

#define MOUSE_RIGHT 2

static InputState state;
static bool space_held;
static bool f_held;
static bool g_held;
static bool z_held;
static bool b_held;
static bool f3_held;
static bool f1_held;
static bool f6_held;
static bool f7_held;
static bool f8_held;
static bool f9_held;
static bool f10_held;
static bool c_held;
static bool t_held;
static bool key1_held;
static bool key2_held;
static bool key3_held;
static bool key4_held;
static bool key5_held;
static bool key6_held;
static bool key7_held;
static bool key8_held;
static bool key9_held;
static bool mleft_held;
static bool mmiddle_held;
static bool keys_down[256];

void input_init(void) {
    memset(&state, 0, sizeof(state));
    memset(keys_down, 0, sizeof(keys_down));
    mmiddle_held = false;
    space_held = false;
    f_held = false;
    g_held = false;
    z_held = false;
    b_held = false;
    f3_held = false;
    f1_held = false;
    f6_held = false;
    f7_held = false;
    f8_held = false;
    f9_held = false;
    f10_held = false;
    c_held = false;
    t_held = false;
    key1_held = false;
    key2_held = false;
    key3_held = false;
    key4_held = false;
    key5_held = false;
    key6_held = false;
    key7_held = false;
    key8_held = false;
    key9_held = false;
    mleft_held = false;
}

void input_release_all(void) {
    for (int i = 0; i < 256; i++) {
        if (keys_down[i])
            input_on_keyup(i);
    }
    if (mleft_held)
        input_on_mouseup(0);
    if (mmiddle_held)
        input_on_mouseup(1);
    if (state.mouse_right)
        input_on_mouseup(2);
    input_clear_mouse_delta();
}

void input_pre_frame(void) {
    state.mouse_dx = 0.0f;
    state.mouse_dy = 0.0f;
    state.scroll_delta = 0.0f;
}

void input_post_frame(void) {
    state.key_space = false;
    state.key_f = false;
    state.key_g = false;
    state.key_z = false;
    state.key_f3 = false;
    state.key_f1 = false;
    state.key_f6 = false;
    state.key_f7 = false;
    state.key_f8 = false;
    state.key_f9 = false;
    state.key_f10 = false;
    state.key_c = false;
    state.key_t = false;
    state.key_1 = false;
    state.key_2 = false;
    state.key_3 = false;
    state.key_4 = false;
    state.key_5 = false;
    state.key_6 = false;
    state.key_7 = false;
    state.key_8 = false;
    state.key_9 = false;
    state.mouse_left = false;
}

const InputState* input_get_state(void) {
    return &state;
}

void input_on_keydown(int keycode) {
    if (keycode >= 0 && keycode < 256)
        keys_down[keycode] = true;
    switch (keycode) {
        case KEY_W: state.key_w = true; break;
        case KEY_A: state.key_a = true; break;
        case KEY_S: state.key_s = true; break;
        case KEY_D: state.key_d = true; break;
        case KEY_SHIFT: state.key_shift = true; break;
        case KEY_F:
            if (!f_held) {
                state.key_f = true;
                f_held = true;
            }
            break;
        case KEY_G:
            if (!g_held) {
                state.key_g = true;
                g_held = true;
            }
            break;
        case KEY_Z:
            if (!z_held) {
                state.key_z = true;
                z_held = true;
            }
            break;
        case KEY_B:
            b_held = true;
            state.key_b_held = true;
            break;
        case KEY_SPACE:
            if (!space_held) {
                state.key_space = true;
                space_held = true;
            }
            state.key_space_held = true;
            break;
        case KEY_F3:
            if (!f3_held) {
                state.key_f3 = true;
                f3_held = true;
                state.key_f3_held = true;
            }
            break;
        case KEY_F1:
            if (!f1_held) { state.key_f1 = true; f1_held = true; }
            break;
        case KEY_F6:
            if (!f6_held) { state.key_f6 = true; f6_held = true; }
            break;
        case KEY_F7:
            if (!f7_held) { state.key_f7 = true; f7_held = true; }
            break;
        case KEY_F8:
            if (!f8_held) { state.key_f8 = true; f8_held = true; }
            break;
        case KEY_F9:
            if (!f9_held) { state.key_f9 = true; f9_held = true; }
            break;
        case KEY_F10:
            if (!f10_held) { state.key_f10 = true; f10_held = true; }
            break;
        case KEY_C:
            if (!c_held) {
                state.key_c = true;
                c_held = true;
            }
            break;
        case KEY_T:
            if (!t_held) {
                state.key_t = true;
                t_held = true;
            }
            break;
        case KEY_1:
            if (!key1_held) {
                state.key_1 = true;
                key1_held = true;
            }
            break;
        case KEY_2:
            if (!key2_held) {
                state.key_2 = true;
                key2_held = true;
            }
            break;
        case KEY_3:
            if (!key3_held) {
                state.key_3 = true;
                key3_held = true;
            }
           break;
        case KEY_4:
            if (!key4_held) {
                state.key_4 = true;
                key4_held = true;
            }
            break;
        case KEY_5:
            if (!key5_held) {
                state.key_5 = true;
                key5_held = true;
            }
            break;
        case KEY_6:
            if (!key6_held) {
                state.key_6 = true;
                key6_held = true;
            }
            break;
        case KEY_7:
            if (!key7_held) {
                state.key_7 = true;
                key7_held = true;
            }
            break;
        case KEY_8:
            if (!key8_held) {
                state.key_8 = true;
                key8_held = true;
            }
            break;
        case KEY_9:
            if (!key9_held) {
                state.key_9 = true;
                key9_held = true;
            }
            break;
    }
}

void input_on_keyup(int keycode) {
    if (keycode >= 0 && keycode < 256)
        keys_down[keycode] = false;
    switch (keycode) {
        case KEY_W: state.key_w = false; break;
        case KEY_A: state.key_a = false; break;
        case KEY_S: state.key_s = false; break;
        case KEY_D: state.key_d = false; break;
        case KEY_SHIFT: state.key_shift = false; break;
        case KEY_F: f_held = false; break;
        case KEY_G: g_held = false; break;
        case KEY_Z: z_held = false; break;
        case KEY_B:
            b_held = false;
            state.key_b_held = false;
            break;
        case KEY_F3:
            f3_held = false;
            state.key_f3_held = false;
            break;
        case KEY_F1: f1_held = false; break;
        case KEY_F6: f6_held = false; break;
        case KEY_F7: f7_held = false; break;
        case KEY_F8: f8_held = false; break;
        case KEY_F9: f9_held = false; break;
        case KEY_F10: f10_held = false; break;
        case KEY_C: c_held = false; break;
        case KEY_T: t_held = false; break;
        case KEY_1: key1_held = false; break;
        case KEY_SPACE:
            space_held = false;
            state.key_space_held = false;
            break;
        case KEY_2: key2_held = false; break;
        case KEY_3: key3_held = false; break;
        case KEY_4: key4_held = false; break;
        case KEY_5: key5_held = false; break;
        case KEY_6: key6_held = false; break;
        case KEY_7: key7_held = false; break;
        case KEY_8: key8_held = false; break;
        case KEY_9: key9_held = false; break;

    }
}

void input_clear_mouse_delta(void) {
    state.mouse_dx = 0.0f;
    state.mouse_dy = 0.0f;
}

void input_on_mousedown(int button) {
    if (button == MOUSE_RIGHT) {
        state.mouse_right = true;
        input_clear_mouse_delta();
    }
    if (button == 0) {
        state.mouse_left = true;
        mleft_held = true;
    }
    if (button == 1)
        mmiddle_held = true;
}

void input_on_mouseup(int button) {
    if (button == MOUSE_RIGHT) {
        state.mouse_right = false;
    }
    if (button == 0) {
        mleft_held = false;
    }
    if (button == 1)
        mmiddle_held = false;
}

bool input_mouse_left_held(void) {
    return mleft_held;
}

bool input_key_held(int keycode) {
    if (keycode < 0 || keycode >= 256) return false;
    return keys_down[keycode];
}

bool input_mouse_button_held(int button) {
    if (button == 0) return mleft_held;
    if (button == 1) return state.mouse_right;
    if (button == 2) return mmiddle_held;
    return false;
}

void input_on_mousemove(float dx, float dy) {
    state.mouse_dx += dx;
    state.mouse_dy += dy;
}

void input_set_mouse_pos(float x, float y) {
    state.mouse_x = x;
    state.mouse_y = y;
}

void input_set_move_axes(float x, float y) {
    state.move_x = x;
    state.move_y = y;
}

void input_on_scroll(float delta) {
    state.scroll_delta += delta;
}

void input_clear_scroll(void) {
    state.scroll_delta = 0.0f;
}
