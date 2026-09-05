/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: touch_controls.c                                                                    |
|   Purpose: on-screen stick / jump / look / tap-to-fire                                      |
\*-------------------------------------------------------------------------------------------*/

#include "touch_controls.h"
#include "input.h"
#include "platform.h"
#include "shader.h"
#include "texture.h"
#include "pw_gles.h"

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TC_MAX_RADIUS_BASE 110.0f
#define TC_DEADZONE        0.10f
#define TC_LOOK_SENS       0.55f
#define TC_STICK_DRAG_PX   28.0f
#define TC_TAP_MAX_PX      22.0f

static bool g_enabled = false;

static bool g_stick_active = false;
static int32_t g_stick_id = -1;
static float g_origin_x = 0.0f, g_origin_y = 0.0f;
static float g_knob_x = 0.0f, g_knob_y = 0.0f;
static float g_max_radius = TC_MAX_RADIUS_BASE;

static bool g_pending_bl = false;
static int32_t g_pending_id = -1;
static float g_pending_x = 0.0f, g_pending_y = 0.0f;
static float g_pending_start_x = 0.0f, g_pending_start_y = 0.0f;

static bool g_look_active = false;
static int32_t g_look_id = -1;
static float g_look_last_x = 0.0f, g_look_last_y = 0.0f;
static float g_look_start_x = 0.0f, g_look_start_y = 0.0f;
static float g_look_travel = 0.0f;

static bool g_jump_held = false;
static int32_t g_jump_id = -1;

static bool g_shiftlock_held = false;
static int32_t g_shiftlock_id = -1;
static bool g_shiftlock_toggle = false;
static bool g_shiftlock_on = false;

static bool g_tap_fire = false;
static float g_tap_fire_x = 0.0f, g_tap_fire_y = 0.0f;

static bool g_pinch_active = false;
static float g_pinch_last_dist = 0.0f;
static int32_t g_pinch_id0 = -1, g_pinch_id1 = -1;

#define TC_MAX_UI_PTRS 8
static int32_t g_ui_ptrs[TC_MAX_UI_PTRS];
static int g_ui_ptr_n = 0;

static int g_sw = 1, g_sh = 1;
static float g_uis = 1.5f;
static float g_ts = 1.5f;

static unsigned int g_prog = 0;
static int g_u_proj = -1, g_u_tex = -1, g_u_alpha = -1, g_u_tint = -1, g_u_use_tex = -1;
static unsigned int g_vao = 0, g_vbo = 0;

static unsigned int g_jump_unpressed = 0;
static unsigned int g_jump_pressed = 0;
static unsigned int g_shiftlock_on_tex = 0;
static unsigned int g_shiftlock_off_tex = 0;
static unsigned int g_stick_base_tex = 0;
static unsigned int g_stick_point_tex = 0;
static unsigned int g_white_tex = 0;
static bool g_assets_requested = false;

static float touch_scale(float ui_scale) {
    float s = ui_scale > 0.1f ? ui_scale : 1.5f;
    if (s < 1.6f) s = 1.6f;
    if (s > 2.4f) s = 2.4f;
    return s;
}

static float drag_thresh(void) {
    return TC_STICK_DRAG_PX * (g_ts > 0.1f ? g_ts : 1.5f) * 0.65f;
}

static float tap_thresh(void) {
    return TC_TAP_MAX_PX * (g_ts > 0.1f ? g_ts : 1.5f) * 0.65f;
}

static void make_white_tex(void) {
    if (g_white_tex) return;
    uint8_t px[4] = {255, 255, 255, 255};
    g_white_tex = texture_load_from_memory(px, 1, 1, 4);
}

static void on_jump_tex(const char* path, const uint8_t* data, size_t len, void* user) {
    unsigned int* slot = (unsigned int*)user;
    (void)path;
    if (!slot || !data || len == 0) return;
    extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
    extern void stbi_image_free(void*);
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &c, 4);
    if (!px) return;
    *slot = texture_load_from_memory(px, w, h, 4);
    stbi_image_free(px);
}

static void ensure_assets(void) {
    if (g_assets_requested) return;
    g_assets_requested = true;
    make_white_tex();
    platform_load_file("assets/jump_unpressed.png", on_jump_tex, &g_jump_unpressed);
    platform_load_file("assets/jump_pressed.png", on_jump_tex, &g_jump_pressed);
    platform_load_file("assets/shiftlock_on.png", on_jump_tex, &g_shiftlock_on_tex);
    platform_load_file("assets/shiftlock_off.png", on_jump_tex, &g_shiftlock_off_tex);
    platform_load_file("assets/joystick_base.png", on_jump_tex, &g_stick_base_tex);
    platform_load_file("assets/joystick_point.png", on_jump_tex, &g_stick_point_tex);
}

static void ensure_gl(void) {
    if (g_prog) return;
    g_prog = shader_load_program("ui_touch");
    g_u_proj = glGetUniformLocation(g_prog, "u_proj");
    g_u_tex = glGetUniformLocation(g_prog, "u_tex");
    g_u_alpha = glGetUniformLocation(g_prog, "u_alpha");
    g_u_tint = glGetUniformLocation(g_prog, "u_tint");
    g_u_use_tex = glGetUniformLocation(g_prog, "u_use_tex");
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
}

static void jump_rect(float* out_x, float* out_y, float* out_s) {
    float s = 96.0f * g_ts;
    *out_s = s;
    *out_x = (float)g_sw - 16.0f * g_ts - s;
    *out_y = (float)g_sh - 16.0f * g_ts - s;
}

static void shiftlock_rect(float* out_x, float* out_y, float* out_s) {
    float jx, jy, js;
    jump_rect(&jx, &jy, &js);
    float s = js * 0.78f;
    *out_s = s;
    *out_x = jx + (js - s) * 0.5f;
    *out_y = jy - 14.0f * g_ts - s;
}

static void stick_home(float* out_x, float* out_y, float* out_r) {
    float r = TC_MAX_RADIUS_BASE * g_ts * 0.85f;
    *out_r = r;
    *out_x = 24.0f * g_ts + r;
    *out_y = (float)g_sh - 24.0f * g_ts - r;
}

static bool in_bottom_left_quadrant(float x, float y) {
    return x < (float)g_sw * 0.5f && y > (float)g_sh * 0.5f;
}

static bool in_circle(float x, float y, float cx, float cy, float r) {
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static void request_tap_fire(void) {
    g_tap_fire = true;

    g_tap_fire_x = g_look_start_x;
    g_tap_fire_y = g_look_start_y;
}

static bool ui_has_ptr(int32_t id) {
    for (int i = 0; i < g_ui_ptr_n; i++) {
        if (g_ui_ptrs[i] == id) return true;
    }
    return false;
}

static void ui_clear_ptr(int32_t id) {
    for (int i = 0; i < g_ui_ptr_n; i++) {
        if (g_ui_ptrs[i] == id) {
            g_ui_ptrs[i] = g_ui_ptrs[g_ui_ptr_n - 1];
            g_ui_ptr_n--;
            return;
        }
    }
}

void touch_controls_mark_ui_pointer(int32_t id) {
    if (ui_has_ptr(id)) return;
    if (g_ui_ptr_n >= TC_MAX_UI_PTRS) return;
    g_ui_ptrs[g_ui_ptr_n++] = id;
}

bool touch_controls_is_ui_pointer(int32_t id) {
    return ui_has_ptr(id);
}

bool touch_controls_clear_ui_pointer(int32_t id) {
    if (!ui_has_ptr(id)) return false;
    ui_clear_ptr(id);
    return true;
}

static bool near_jump_pad(float x, float y) {
    float jx, jy, js;
    jump_rect(&jx, &jy, &js);
    float jcx = jx + js * 0.5f, jcy = jy + js * 0.5f;

    return in_circle(x, y, jcx, jcy, js * 0.72f);
}

static bool near_shiftlock_pad(float x, float y) {
    float sx, sy, ss;
    shiftlock_rect(&sx, &sy, &ss);
    float cx = sx + ss * 0.5f, cy = sy + ss * 0.5f;
    return in_circle(x, y, cx, cy, ss * 0.72f);
}

static void apply_stick_axes(void) {
    if (!g_stick_active) {
        input_set_move_axes(0.0f, 0.0f);
        return;
    }
    float dx = g_knob_x - g_origin_x;
    float dy = g_knob_y - g_origin_y;
    float dist = sqrtf(dx * dx + dy * dy);
    float mx = 0.0f, my = 0.0f;
    if (dist > 0.001f && g_max_radius > 0.001f) {
        float t = dist / g_max_radius;
        if (t > 1.0f) t = 1.0f;
        if (t >= TC_DEADZONE) {
            float u = (t - TC_DEADZONE) / (1.0f - TC_DEADZONE);
            mx = (dx / dist) * u;
            my = (dy / dist) * u;
        }
    }
    input_set_move_axes(mx, my);
}

static void release_stick(void) {
    g_stick_active = false;
    g_stick_id = -1;
    input_set_move_axes(0.0f, 0.0f);
}

static void clear_pending_bl(void) {
    g_pending_bl = false;
    g_pending_id = -1;
}

static void activate_stick(int32_t id, float x, float y) {
    clear_pending_bl();
    g_stick_active = true;
    g_stick_id = id;
    float hx, hy, hr;
    stick_home(&hx, &hy, &hr);
    g_origin_x = hx;
    g_origin_y = hy;
    g_max_radius = hr;
    g_knob_x = x;
    g_knob_y = y;
    {
        float dx = g_knob_x - g_origin_x;
        float dy = g_knob_y - g_origin_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > g_max_radius && dist > 0.001f) {
            g_knob_x = g_origin_x + dx / dist * g_max_radius;
            g_knob_y = g_origin_y + dy / dist * g_max_radius;
        }
    }
    apply_stick_axes();
}

static void begin_look(int32_t id, float x, float y) {
    if (g_look_active) return;
    g_look_active = true;
    g_look_id = id;
    g_look_last_x = x;
    g_look_last_y = y;
    g_look_start_x = x;
    g_look_start_y = y;
    g_look_travel = 0.0f;
    input_on_mousedown(2);
}

static void release_look(void) {
    if (g_look_active) {
        input_on_mouseup(2);
    }
    g_look_active = false;
    g_look_id = -1;
    g_look_travel = 0.0f;
}

static void release_jump(void) {
    if (g_jump_held) {
        input_on_keyup(32);
    }
    g_jump_held = false;
    g_jump_id = -1;
}

static void release_shiftlock(void) {
    g_shiftlock_held = false;
    g_shiftlock_id = -1;
}

static void end_pinch(void) {
    g_pinch_active = false;
    g_pinch_last_dist = 0.0f;
    g_pinch_id0 = -1;
    g_pinch_id1 = -1;
}

static bool pointer_blocks_pinch(int32_t id) {
    if (ui_has_ptr(id)) return true;
    if (g_jump_held && g_jump_id == id) return true;
    if (g_shiftlock_held && g_shiftlock_id == id) return true;
    if (g_stick_active && g_stick_id == id) return true;
    if (g_pending_bl && g_pending_id == id) return true;
    return false;
}

void touch_controls_update_pinch(uint32_t count, const int32_t* ids,
                                 const float* xs, const float* ys) {
    if (!g_enabled || !ids || !xs || !ys || count < 2) {
        if (g_pinch_active && g_look_active) {

            g_look_travel = tap_thresh() + 1.0f;
        }
        end_pinch();
        return;
    }

    int32_t free_ids[8];
    float free_x[8], free_y[8];
    int free_n = 0;
    for (uint32_t i = 0; i < count && free_n < 8; i++) {
        if (pointer_blocks_pinch(ids[i])) continue;
        free_ids[free_n] = ids[i];
        free_x[free_n] = xs[i];
        free_y[free_n] = ys[i];
        free_n++;
    }

    if (free_n < 2) {
        if (g_pinch_active && g_look_active)
            g_look_travel = tap_thresh() + 1.0f;
        end_pinch();
        return;
    }

    float dx = free_x[1] - free_x[0];
    float dy = free_y[1] - free_y[0];
    float dist = sqrtf(dx * dx + dy * dy);

    if (!g_pinch_active) {
        g_pinch_active = true;
        g_pinch_id0 = free_ids[0];
        g_pinch_id1 = free_ids[1];
        g_pinch_last_dist = dist;

        if (g_look_active) {
            for (int i = 0; i < free_n; i++) {
                if (free_ids[i] == g_look_id) {
                    g_look_last_x = free_x[i];
                    g_look_last_y = free_y[i];
                    break;
                }
            }
            g_look_travel = tap_thresh() + 1.0f;
        }
        return;
    }

    if (g_pinch_last_dist > 8.0f && dist > 8.0f) {

        float delta = dist - g_pinch_last_dist;
        input_on_scroll(-delta * 0.04f);
    }
    g_pinch_last_dist = dist;
    g_pinch_id0 = free_ids[0];
    g_pinch_id1 = free_ids[1];
}

bool touch_controls_pinch_active(void) {
    return g_pinch_active;
}

void touch_controls_init(void) {
    g_enabled = false;
    release_stick();
    release_look();
    release_jump();
    release_shiftlock();
    clear_pending_bl();
    end_pinch();
    g_tap_fire = false;
    g_shiftlock_toggle = false;
    g_ui_ptr_n = 0;
    g_assets_requested = false;
    ensure_assets();
}

void touch_controls_invalidate_gl(bool context_alive) {
    if (context_alive) {
        if (g_prog) glDeleteProgram(g_prog);
        if (g_vao) glDeleteVertexArrays(1, &g_vao);
        if (g_vbo) glDeleteBuffers(1, &g_vbo);
        if (g_jump_unpressed) glDeleteTextures(1, &g_jump_unpressed);
        if (g_jump_pressed) glDeleteTextures(1, &g_jump_pressed);
        if (g_shiftlock_on_tex) glDeleteTextures(1, &g_shiftlock_on_tex);
        if (g_shiftlock_off_tex) glDeleteTextures(1, &g_shiftlock_off_tex);
        if (g_white_tex) glDeleteTextures(1, &g_white_tex);
    }
    g_prog = 0;
    g_u_proj = g_u_tex = g_u_alpha = g_u_tint = g_u_use_tex = -1;
    g_vao = 0;
    g_vbo = 0;
    g_jump_unpressed = 0;
    g_jump_pressed = 0;
    g_shiftlock_on_tex = 0;
    g_shiftlock_off_tex = 0;
    g_white_tex = 0;
    g_assets_requested = false;
    ensure_assets();
    ensure_gl();
}

void touch_controls_set_enabled(bool enabled) {
    if (g_enabled == enabled) return;
    g_enabled = enabled;
    if (!enabled) {
        release_stick();
        release_look();
        release_jump();
        release_shiftlock();
        clear_pending_bl();
        end_pinch();
        g_tap_fire = false;
        g_shiftlock_toggle = false;
        g_ui_ptr_n = 0;
    }
}

bool touch_controls_enabled(void) {
    return g_enabled;
}

bool touch_controls_owns_pointer(int32_t id) {
    return (g_stick_active && g_stick_id == id) ||
           (g_look_active && g_look_id == id) ||
           (g_jump_held && g_jump_id == id) ||
           (g_shiftlock_held && g_shiftlock_id == id) ||
           (g_pending_bl && g_pending_id == id) ||
           ui_has_ptr(id);
}

bool touch_controls_consume_menu_press(void) {
    return false;
}

bool touch_controls_consume_shiftlock_toggle(void) {
    if (!g_shiftlock_toggle) return false;
    g_shiftlock_toggle = false;
    return true;
}

void touch_controls_set_shift_lock(bool locked) {
    g_shiftlock_on = locked;
}

bool touch_controls_consume_tap_fire(void) {
    if (!g_tap_fire) return false;
    g_tap_fire = false;
    return true;
}

void touch_controls_tap_fire_pos(float* out_x, float* out_y) {
    if (out_x) *out_x = g_tap_fire_x;
    if (out_y) *out_y = g_tap_fire_y;
}

bool touch_controls_pointer_down(int32_t id, float x, float y, int screen_w, int screen_h,
                                 uint32_t flags) {
    if (!g_enabled) return false;
    g_sw = screen_w > 0 ? screen_w : 1;
    g_sh = screen_h > 0 ? screen_h : 1;

    if (ui_has_ptr(id)) return true;

    float jx, jy, js;
    jump_rect(&jx, &jy, &js);
    float jcx = jx + js * 0.5f, jcy = jy + js * 0.5f;
    if (in_circle(x, y, jcx, jcy, js * 0.62f)) {
        g_jump_held = true;
        g_jump_id = id;
        input_on_keydown(32);
        return true;
    }

    {
        float sx, sy, ss;
        shiftlock_rect(&sx, &sy, &ss);
        float scx = sx + ss * 0.5f, scy = sy + ss * 0.5f;
        if (in_circle(x, y, scx, scy, ss * 0.62f)) {
            g_shiftlock_held = true;
            g_shiftlock_id = id;
            return true;
        }
    }

    if (flags & TC_FLAG_FORCE_LOOK) {
        begin_look(id, x, y);
        return true;
    }

    if (in_bottom_left_quadrant(x, y)) {
        if (g_stick_active || g_pending_bl) return true;
        g_pending_bl = true;
        g_pending_id = id;
        g_pending_start_x = x;
        g_pending_start_y = y;
        g_pending_x = x;
        g_pending_y = y;
        return true;
    }

    if (g_look_active) return true;
    begin_look(id, x, y);
    return true;
}

bool touch_controls_pointer_move(int32_t id, float x, float y) {
    if (!g_enabled) return false;
    if (ui_has_ptr(id)) return true;

    if (g_pending_bl && id == g_pending_id) {
        g_pending_x = x;
        g_pending_y = y;
        float dx = x - g_pending_start_x;
        float dy = y - g_pending_start_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist >= drag_thresh()) {
            activate_stick(id, x, y);
        }
        return true;
    }

    if (g_stick_active && id == g_stick_id) {
        float dx = x - g_origin_x;
        float dy = y - g_origin_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > g_max_radius && dist > 0.001f) {
            g_knob_x = g_origin_x + dx / dist * g_max_radius;
            g_knob_y = g_origin_y + dy / dist * g_max_radius;
        } else {
            g_knob_x = x;
            g_knob_y = y;
        }
        apply_stick_axes();
        return true;
    }

    if (g_look_active && id == g_look_id) {
        float dx = x - g_look_last_x;
        float dy = y - g_look_last_y;
        g_look_last_x = x;
        g_look_last_y = y;
        g_look_travel += sqrtf(dx * dx + dy * dy);

        if (!g_pinch_active) {
            input_on_mousemove(dx * TC_LOOK_SENS, dy * TC_LOOK_SENS);
        }
        return true;
    }

    if (g_jump_held && id == g_jump_id) return true;
    if (g_shiftlock_held && id == g_shiftlock_id) return true;
    return false;
}

bool touch_controls_pointer_up(int32_t id) {
    if (!g_enabled) {
        ui_clear_ptr(id);
        return false;
    }
    bool owned = false;

    if (ui_has_ptr(id)) {
        ui_clear_ptr(id);
        return true;
    }

    if (g_pending_bl && id == g_pending_id) {

        clear_pending_bl();
        owned = true;
    }

    if (g_stick_active && id == g_stick_id) {
        release_stick();
        owned = true;
    }

    if (g_look_active && id == g_look_id) {

        if (!g_pinch_active && g_look_travel < tap_thresh() &&
            !near_jump_pad(g_look_start_x, g_look_start_y) &&
            !near_shiftlock_pad(g_look_start_x, g_look_start_y)) {
            request_tap_fire();
        }
        release_look();
        owned = true;
    }

    if (g_jump_id == id) {
        release_jump();
        owned = true;
    }

    if (g_shiftlock_id == id) {
        g_shiftlock_toggle = true;
        release_shiftlock();
        owned = true;
    }

    return owned;
}

static void set_proj(void) {
    float proj[16];
    memset(proj, 0, sizeof(proj));
    proj[0] = 2.0f / (float)g_sw;
    proj[5] = -2.0f / (float)g_sh;
    proj[10] = 1.0f;
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[15] = 1.0f;
    glUniformMatrix4fv(g_u_proj, 1, GL_FALSE, proj);
}

static void draw_quad(unsigned int tex, float x, float y, float w, float h,
                      float r, float g, float b, float a, int use_tex, int flip_v) {
    float v0 = flip_v ? 0.0f : 1.0f;
    float v1 = flip_v ? 1.0f : 0.0f;
    float verts[] = {
        x,     y,     0.0f, v0,
        x + w, y,     1.0f, v0,
        x + w, y + h, 1.0f, v1,
        x,     y,     0.0f, v0,
        x + w, y + h, 1.0f, v1,
        x,     y + h, 0.0f, v1,
    };
    glUniform1f(g_u_alpha, a);
    glUniform4f(g_u_tint, r, g, b, 1.0f);
    glUniform1i(g_u_use_tex, use_tex ? 1 : 0);
    if (use_tex && tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(g_u_tex, 0);
    } else if (g_white_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_white_tex);
        glUniform1i(g_u_tex, 0);
        glUniform1i(g_u_use_tex, 1);
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void draw_circle(float cx, float cy, float radius,
                        float cr, float cg, float cb, float ca) {
    const int segs = 28;
    float verts[(28 + 2) * 4];
    int n = 0;
    verts[n++] = cx; verts[n++] = cy; verts[n++] = 0.5f; verts[n++] = 0.5f;
    for (int i = 0; i <= segs; i++) {
        float a = (float)i / (float)segs * 6.2831853f;
        verts[n++] = cx + cosf(a) * radius;
        verts[n++] = cy + sinf(a) * radius;
        verts[n++] = 0.5f;
        verts[n++] = 0.5f;
    }
    glUniform1f(g_u_alpha, ca);
    glUniform4f(g_u_tint, cr, cg, cb, 1.0f);
    if (g_white_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_white_tex);
        glUniform1i(g_u_tex, 0);
        glUniform1i(g_u_use_tex, 1);
    } else {
        glUniform1i(g_u_use_tex, 0);
    }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(float)), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 4);
}

void touch_controls_render(int screen_w, int screen_h, float ui_scale) {
    if (!g_enabled) return;
    g_sw = screen_w > 0 ? screen_w : 1;
    g_sh = screen_h > 0 ? screen_h : 1;
    g_uis = ui_scale > 0.1f ? ui_scale : 1.5f;
    g_ts = touch_scale(g_uis);
    g_max_radius = TC_MAX_RADIUS_BASE * g_ts * 0.85f;

    ensure_assets();
    ensure_gl();
    make_white_tex();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_sw, g_sh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_prog);
    set_proj();
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    {
        float hx, hy, hr;
        stick_home(&hx, &hy, &hr);
        float ox = g_stick_active ? g_origin_x : hx;
        float oy = g_stick_active ? g_origin_y : hy;
        float orad = g_stick_active ? g_max_radius : hr;
        float base_s = orad * 2.0f;
        float kx = g_stick_active ? g_knob_x : ox;
        float ky = g_stick_active ? g_knob_y : oy;
        float point_s = orad * 0.92f;
        float alpha = g_stick_active ? 1.0f : 0.88f;

        if (g_stick_base_tex) {
            draw_quad(g_stick_base_tex, ox - orad, oy - orad, base_s, base_s,
                      1.0f, 1.0f, 1.0f, alpha, 1, 1);
        } else {
            draw_circle(ox, oy, orad, 0.0f, 0.0f, 0.0f, 0.28f);
            draw_circle(ox, oy, orad * 0.94f, 1.0f, 1.0f, 1.0f, 0.10f);
            draw_circle(ox, oy, orad * 0.72f, 0.0f, 0.0f, 0.0f, 0.18f);
        }

        if (g_stick_point_tex) {
            draw_quad(g_stick_point_tex, kx - point_s * 0.5f, ky - point_s * 0.5f,
                      point_s, point_s, 1.0f, 1.0f, 1.0f, alpha, 1, 1);
        } else {
            float kr = orad * 0.38f;
            draw_circle(kx, ky, kr, 0.0f, 0.0f, 0.0f, 0.35f);
            draw_circle(kx, ky, kr * 0.86f, 1.0f, 1.0f, 1.0f, g_stick_active ? 0.55f : 0.28f);
        }
    }

    {
        float jx, jy, js;
        jump_rect(&jx, &jy, &js);
        unsigned int tex = g_jump_held
            ? (g_jump_pressed ? g_jump_pressed : g_jump_unpressed)
            : (g_jump_unpressed ? g_jump_unpressed : g_jump_pressed);
        if (tex) {
            draw_quad(tex, jx, jy, js, js, 1.0f, 1.0f, 1.0f, 1.0f, 1, 1);
        } else {
            float jcx = jx + js * 0.5f, jcy = jy + js * 0.5f;
            draw_circle(jcx, jcy, js * 0.5f, 1.0f, 1.0f, 1.0f, g_jump_held ? 0.7f : 0.45f);
        }
    }

    {
        float sx, sy, ss;
        shiftlock_rect(&sx, &sy, &ss);
        unsigned int tex = g_shiftlock_on
            ? (g_shiftlock_on_tex ? g_shiftlock_on_tex : g_shiftlock_off_tex)
            : (g_shiftlock_off_tex ? g_shiftlock_off_tex : g_shiftlock_on_tex);
        float alpha = g_shiftlock_held ? 1.0f : 0.92f;
        if (tex) {
            draw_quad(tex, sx, sy, ss, ss, 1.0f, 1.0f, 1.0f, alpha, 1, 1);
        } else {
            float cx = sx + ss * 0.5f, cy = sy + ss * 0.5f;
            float lit = g_shiftlock_on ? 0.75f : 0.4f;
            if (g_shiftlock_held) lit = 0.9f;
            draw_circle(cx, cy, ss * 0.5f, 1.0f, 1.0f, 1.0f, lit);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}
