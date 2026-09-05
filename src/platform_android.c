/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform_android.c                                                                  |
|   Purpose: GameActivity + EGL/GLES3                                                         |
\*-------------------------------------------------------------------------------------------*/

#ifdef __ANDROID__

#include "platform.h"
#include "platform_android.h"
#include "input.h"
#include "touch_controls.h"
#include "pw_gles.h"
#ifdef PW_QUEST
#include "vr_openxr.h"
#endif

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android/input.h>
#include <android/native_window.h>
#include <errno.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <game-activity/GameActivity.h>
#include <game-text-input/gametextinput.h>
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#define PW_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "PolyWorld", __VA_ARGS__)
#define PW_AERR(...) __android_log_print(ANDROID_LOG_ERROR, "PolyWorld", __VA_ARGS__)

void platform_http_pump(void);
static int pw_ahttp_start(const char* url, const char* post_body, int is_async,
                          file_load_callback cb, void* user);

static struct android_app* g_app = NULL;
static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLSurface g_surface = EGL_NO_SURFACE;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLConfig g_config = NULL;
static bool g_has_focus = false;
static bool g_paused = false;
static bool g_window_ready = false;
static bool g_resized_by_user = false;
static bool g_gl_restore_pending = false;
static bool g_fullscreen = true;
static int g_width = 0;
static int g_height = 0;
static int g_win_w = 0;
static int g_win_h = 0;
static double g_time_origin = 0.0;
static float g_last_touch_x = 0.0f;
static float g_last_touch_y = 0.0f;
static bool g_touch_down = false;
static bool g_mouse_rmb_down = false;
static int g_ime_inset_bottom = 0;
static bool g_ime_visible = false;
static char g_userdata_dir[512] = {0};
static JavaVM* g_vm = NULL;
static char g_http_last_error[256] = {0};
static platform_android_key_fn g_on_key = NULL;
static platform_android_char_fn g_on_char = NULL;
static platform_android_text_fn g_on_text = NULL;

static bool mkdir_p(const char* path) {
    if (!path || !path[0]) return false;
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) return false;

    size_t len = strlen(tmp);
    if (len == 0) return false;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0770) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0770) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

static void ensure_userdata_dir(void) {
    if (g_userdata_dir[0]) return;
    if (!g_app || !g_app->activity) return;

    const char* external = g_app->activity->externalDataPath;
    const char* internal = g_app->activity->internalDataPath;
    const char* chosen = NULL;

    if (external && external[0]) {
        chosen = external;
    } else if (internal && internal[0]) {
        chosen = internal;
    }

    PW_ALOG("paths: internal=%s external=%s",
            internal ? internal : "(null)",
            external ? external : "(null)");

    if (!chosen) {
        PW_AERR("no userdata directory available");
        return;
    }

    if (!mkdir_p(chosen)) {
        PW_AERR("mkdir_p failed for %s (errno=%d)", chosen, errno);

    }

    snprintf(g_userdata_dir, sizeof(g_userdata_dir), "%s", chosen);
    PW_ALOG("userdata dir: %s", g_userdata_dir);
}

void platform_android_attach(struct android_app* app) {
    g_app = app;
    g_paused = false;
    g_has_focus = false;
    g_window_ready = false;
    g_userdata_dir[0] = '\0';
    g_vm = (app && app->activity) ? app->activity->vm : NULL;
}

struct android_app* platform_android_app(void) { return g_app; }
EGLDisplay platform_android_egl_display(void) { return g_display; }
EGLConfig platform_android_egl_config(void) { return g_config; }
EGLContext platform_android_egl_context(void) { return g_context; }

void platform_android_set_text_handlers(platform_android_key_fn on_key,
                                       platform_android_char_fn on_char) {
    g_on_key = on_key;
    g_on_char = on_char;
}

void platform_android_set_text_input_handler(platform_android_text_fn on_text) {
    g_on_text = on_text;
}

void platform_android_set_ime_text(const char* utf8) {
    if (!g_app || !g_app->activity) return;
    GameTextInputState st;
    memset(&st, 0, sizeof(st));
    const char* t = utf8 ? utf8 : "";
    st.text_UTF8 = t;
    st.text_length = (int32_t)strlen(t);
    st.selection.start = st.text_length;
    st.selection.end = st.text_length;
    st.composingRegion.start = -1;
    st.composingRegion.end = -1;
    GameActivity_setTextInputState(g_app->activity, &st);
}

void platform_android_configure_ime(bool password, int action) {
    if (!g_app || !g_app->activity) return;

    unsigned int type = (unsigned int)TYPE_CLASS_TEXT | (unsigned int)TYPE_TEXT_FLAG_MULTI_LINE;
    if (password)
        type |= (unsigned int)TYPE_TEXT_VARIATION_PASSWORD;
    enum GameTextInputActionType act = IME_ACTION_DONE;
    if (action == 1) act = IME_ACTION_GO;
    else if (action == 2) act = IME_ACTION_SEND;
    GameActivity_setImeEditorInfo(g_app->activity,
                                  (enum GameTextInputType)type,
                                  act,
                                  (enum GameTextInputImeOptions)IME_FLAG_NO_FULLSCREEN);
}

static void text_input_state_cb(void* context, const struct GameTextInputState* state) {
    (void)context;
    if (!state || !g_on_text) return;
    const char* t = state->text_UTF8 ? state->text_UTF8 : "";
    int len = state->text_length;
    if (len < 0) len = (int)strlen(t);
    g_on_text(t, len);
}

static void process_soft_keyboard_text(void) {
    if (!g_app || !g_app->activity || !g_app->textInputState) return;
    GameActivity_getTextInputState(g_app->activity, text_input_state_cb, NULL);
    g_app->textInputState = 0;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void destroy_surface(void) {
    if (g_display == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_display, g_surface);
        g_surface = EGL_NO_SURFACE;
    }
}

static void destroy_context(void) {
    destroy_surface();
    if (g_display != EGL_NO_DISPLAY && g_context != EGL_NO_CONTEXT) {
        eglDestroyContext(g_display, g_context);
        g_context = EGL_NO_CONTEXT;
    }
    if (g_display != EGL_NO_DISPLAY) {
        eglTerminate(g_display);
        g_display = EGL_NO_DISPLAY;
    }
    g_config = NULL;
    g_window_ready = false;
}

static bool init_egl_display(void) {
    if (g_display != EGL_NO_DISPLAY && g_context != EGL_NO_CONTEXT) {
        return true;
    }

    if (g_display != EGL_NO_DISPLAY && g_context == EGL_NO_CONTEXT) {
        destroy_context();
    }

    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) {
        PW_AERR("eglGetDisplay failed");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_display, &major, &minor)) {
        PW_AERR("eglInitialize failed");
        g_display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLint num_configs = 0;
    if (!eglChooseConfig(g_display, attribs, &g_config, 1, &num_configs) || num_configs == 0) {
        PW_AERR("eglChooseConfig failed");
        destroy_context();
        return false;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    g_context = eglCreateContext(g_display, g_config, EGL_NO_CONTEXT, context_attribs);
    if (g_context == EGL_NO_CONTEXT) {
        PW_AERR("eglCreateContext failed");
        destroy_context();
        return false;
    }

    PW_ALOG("EGL initialized (ES3), version %d.%d", major, minor);
    return true;
}

static bool create_surface(ANativeWindow* window) {
    if (!window || g_display == EGL_NO_DISPLAY || g_context == EGL_NO_CONTEXT) {
        return false;
    }

    destroy_surface();

    g_win_w = ANativeWindow_getWidth(window);
    g_win_h = ANativeWindow_getHeight(window);
    if (g_win_w < 1) g_win_w = 1;
    if (g_win_h < 1) g_win_h = 1;
    ANativeWindow_setBuffersGeometry(window, 0, 0,  0);

    g_surface = eglCreateWindowSurface(g_display, g_config, window, NULL);
    if (g_surface == EGL_NO_SURFACE) {
        PW_AERR("eglCreateWindowSurface failed");
        return false;
    }

    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        PW_AERR("eglMakeCurrent failed -- recreating EGL context");
        destroy_surface();

        if (g_display != EGL_NO_DISPLAY && g_context != EGL_NO_CONTEXT) {
            eglDestroyContext(g_display, g_context);
            g_context = EGL_NO_CONTEXT;
        }
        g_gl_restore_pending = true;
        if (!init_egl_display()) return false;
        g_surface = eglCreateWindowSurface(g_display, g_config, window, NULL);
        if (g_surface == EGL_NO_SURFACE) {
            PW_AERR("eglCreateWindowSurface retry failed");
            return false;
        }
        if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
            PW_AERR("eglMakeCurrent retry failed");
            destroy_surface();
            return false;
        }
    }

    eglSwapInterval(g_display, 1);

    EGLint ew = 0, eh = 0;
    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &ew);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &eh);
    g_width = ew > 0 ? ew : g_win_w;
    g_height = eh > 0 ? eh : g_win_h;
    g_win_w = g_width;
    g_win_h = g_height;
    glViewport(0, 0, g_width, g_height);
    g_window_ready = true;
    g_resized_by_user = true;
    {
        extern void resize_canvas(int width, int height);
        resize_canvas(g_width, g_height);
    }
    PW_ALOG("EGL surface %dx%d", g_width, g_height);
    return true;
}

static void handle_cmd(struct android_app* app, int32_t cmd) {
    (void)app;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (g_app && g_app->window) {
                if (!init_egl_display()) {
                    break;
                }
                if (create_surface(g_app->window)) {
                    g_has_focus = true;
                    g_paused = false;
                    if (g_gl_restore_pending) {
                        extern void pw_game_restore_gl(void);
                        PW_ALOG("restoring GL resources after surface recreate");
                        pw_game_restore_gl();
                        g_gl_restore_pending = false;
                    }
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            destroy_surface();
            g_window_ready = false;

            g_gl_restore_pending = true;
            break;
        case APP_CMD_PAUSE:

            g_paused = true;
            g_has_focus = false;
            PW_ALOG("APP_CMD_PAUSE");
            break;
        case APP_CMD_RESUME:
            g_paused = false;
            PW_ALOG("APP_CMD_RESUME");
            break;
        case APP_CMD_GAINED_FOCUS:
            g_has_focus = true;
            g_paused = false;
            break;
        case APP_CMD_LOST_FOCUS:
            g_has_focus = false;
            break;
        case APP_CMD_LOW_MEMORY:
            PW_ALOG("LOW_MEMORY -- marking GL for restore on next surface");
            g_gl_restore_pending = true;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (g_app && g_app->window && g_surface != EGL_NO_SURFACE) {
                int w = ANativeWindow_getWidth(g_app->window);
                int h = ANativeWindow_getHeight(g_app->window);
#ifdef PW_QUEST

                if (vr_openxr_game_width() > 0) {
                    g_win_w = w > 0 ? w : g_win_w;
                    g_win_h = h > 0 ? h : g_win_h;
                    break;
                }
#endif
                if (w > 0 && h > 0 && (w != g_width || h != g_height)) {
                    g_width = w;
                    g_height = h;
                    g_win_w = w;
                    g_win_h = h;
                    glViewport(0, 0, g_width, g_height);
                    g_resized_by_user = true;
                    {
                        extern void resize_canvas(int width, int height);
                        resize_canvas(g_width, g_height);
                    }
                    PW_ALOG("resize %dx%d", g_width, g_height);
                }
            }
            break;
        case APP_CMD_SOFTWARE_KB_VIS_CHANGED:
            if (g_app) {
                g_ime_visible = g_app->softwareKeyboardVisible;
                PW_ALOG("ime visible=%d inset=%d", g_ime_visible ? 1 : 0, g_ime_inset_bottom);
            }
            break;
        case APP_CMD_WINDOW_INSETS_CHANGED:
            if (g_app && g_app->activity) {
                ARect insets;
                memset(&insets, 0, sizeof(insets));
                GameActivity_getWindowInsets(g_app->activity, GAMECOMMON_INSETS_TYPE_IME, &insets);

                g_ime_inset_bottom = insets.bottom;
                if (g_ime_inset_bottom < 0) g_ime_inset_bottom = 0;
                g_ime_visible = (g_ime_inset_bottom > 0) ||
                               (g_app->softwareKeyboardVisible != 0);
                PW_ALOG("ime insets bottom=%d visible=%d", g_ime_inset_bottom, g_ime_visible ? 1 : 0);
            }
            break;
        default:
            break;
    }
}

static float pointer_axis(const GameActivityMotionEvent* event, uint32_t index, int32_t axis) {
    return GameActivityPointerAxes_getAxisValue(&event->pointers[index], axis);
}

static void map_touch_to_buffer(float* x, float* y) {
    if (!x || !y) return;
    if (g_win_w > 0 && g_win_h > 0 && g_width > 0 && g_height > 0 &&
        (g_win_w != g_width || g_win_h != g_height)) {
        *x = *x * (float)g_width / (float)g_win_w;
        *y = *y * (float)g_height / (float)g_win_h;
    }
}

extern bool chat_handle_click(float x, float y);
extern bool chat_handle_mouseup(float x, float y);

static void handle_motion_events(void) {
    struct android_input_buffer* input_buffer = android_app_swap_input_buffers(g_app);
    if (!input_buffer) {
        return;
    }

    for (uint64_t i = 0; i < input_buffer->motionEventsCount; i++) {
        GameActivityMotionEvent* ev = &input_buffer->motionEvents[i];
        if (ev->pointerCount == 0) {
            continue;
        }

        int action = ev->action;
        int action_masked = action & AMOTION_EVENT_ACTION_MASK;
        uint32_t pointer_index = 0;
        if (action_masked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
            action_masked == AMOTION_EVENT_ACTION_POINTER_UP) {
            pointer_index = (uint32_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                       AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        }
        if (pointer_index >= ev->pointerCount) {
            pointer_index = 0;
        }

        float x = pointer_axis(ev, pointer_index, AMOTION_EVENT_AXIS_X);
        float y = pointer_axis(ev, pointer_index, AMOTION_EVENT_AXIS_Y);
        map_touch_to_buffer(&x, &y);
        int32_t pid = ev->pointers[pointer_index].id;
        int sw = g_width > 0 ? g_width : 1;
        int sh = g_height > 0 ? g_height : 1;

        uint32_t tc_flags = 0;
        int32_t tool = ev->pointers[pointer_index].toolType;
        bool is_mouse = (tool == AMOTION_EVENT_TOOL_TYPE_MOUSE) ||
                        ((ev->source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE);
        bool secondary = (ev->buttonState & AMOTION_EVENT_BUTTON_SECONDARY) != 0 ||
                         (ev->actionButton & AMOTION_EVENT_BUTTON_SECONDARY) != 0;
        if (is_mouse) {
            if (secondary && !g_mouse_rmb_down) {
                g_mouse_rmb_down = true;
                input_on_mousedown(2);
            } else if (!secondary && g_mouse_rmb_down &&
                       (action_masked == AMOTION_EVENT_ACTION_UP ||
                        action_masked == AMOTION_EVENT_ACTION_BUTTON_RELEASE ||
                        action_masked == AMOTION_EVENT_ACTION_CANCEL)) {
                g_mouse_rmb_down = false;
                input_on_mouseup(2);
            }
            if (secondary || g_mouse_rmb_down) {
                tc_flags |= TC_FLAG_FORCE_LOOK;
            }
        }

        if (action_masked == AMOTION_EVENT_ACTION_DOWN ||
            action_masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            if (chat_handle_click(x, y)) {
                input_set_mouse_pos(x, y);

                touch_controls_mark_ui_pointer(pid);
                continue;
            }
        }
        if (action_masked == AMOTION_EVENT_ACTION_UP ||
            action_masked == AMOTION_EVENT_ACTION_POINTER_UP ||
            action_masked == AMOTION_EVENT_ACTION_CANCEL ||
            action_masked == AMOTION_EVENT_ACTION_BUTTON_RELEASE) {
            if (touch_controls_clear_ui_pointer(pid)) {
                input_set_mouse_pos(x, y);

                chat_handle_mouseup(x, y);
                continue;
            }
        }
        if (action_masked == AMOTION_EVENT_ACTION_MOVE &&
            touch_controls_is_ui_pointer(pid)) {

            input_set_mouse_pos(x, y);
            continue;
        }

        if (touch_controls_enabled()) {
            bool consumed = false;
            switch (action_masked) {
                case AMOTION_EVENT_ACTION_DOWN:
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                case AMOTION_EVENT_ACTION_BUTTON_PRESS:
                    consumed = touch_controls_pointer_down(pid, x, y, sw, sh, tc_flags);

                    {
                        int32_t ids[8];
                        float xs[8], ys[8];
                        uint32_t n = 0;
                        for (uint32_t p = 0; p < ev->pointerCount && n < 8; p++) {
                            ids[n] = ev->pointers[p].id;
                            xs[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_X);
                            ys[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_Y);
                            map_touch_to_buffer(&xs[n], &ys[n]);
                            n++;
                        }
                        touch_controls_update_pinch(n, ids, xs, ys);
                    }
                    break;
                case AMOTION_EVENT_ACTION_MOVE: {

                    int32_t ids[8];
                    float xs[8], ys[8];
                    uint32_t n = 0;
                    for (uint32_t p = 0; p < ev->pointerCount && n < 8; p++) {
                        ids[n] = ev->pointers[p].id;
                        xs[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_X);
                        ys[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_Y);
                        map_touch_to_buffer(&xs[n], &ys[n]);
                        n++;
                    }
                    touch_controls_update_pinch(n, ids, xs, ys);
                    for (uint32_t p = 0; p < n; p++) {
                        if (touch_controls_pointer_move(ids[p], xs[p], ys[p]))
                            consumed = true;
                    }
                    break;
                }
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_POINTER_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
                    consumed = touch_controls_pointer_up(pid);

                    int32_t ids[8];
                    float xs[8], ys[8];
                    uint32_t n = 0;
                    for (uint32_t p = 0; p < ev->pointerCount && n < 8; p++) {
                        if (p == pointer_index) continue;
                        ids[n] = ev->pointers[p].id;
                        xs[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_X);
                        ys[n] = pointer_axis(ev, p, AMOTION_EVENT_AXIS_Y);
                        map_touch_to_buffer(&xs[n], &ys[n]);
                        n++;
                    }
                    touch_controls_update_pinch(n, ids, xs, ys);
                    break;
                }
                default:
                    break;
            }
            if (consumed) {
                continue;
            }

            if (!is_mouse) {
                continue;
            }
        }

        switch (action_masked) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                g_touch_down = true;
                g_last_touch_x = x;
                g_last_touch_y = y;
                input_set_mouse_pos(x, y);
                input_on_mousedown(0);
                break;
            case AMOTION_EVENT_ACTION_MOVE: {
                float px = pointer_axis(ev, 0, AMOTION_EVENT_AXIS_X);
                float py = pointer_axis(ev, 0, AMOTION_EVENT_AXIS_Y);
                map_touch_to_buffer(&px, &py);
                float dx = px - g_last_touch_x;
                float dy = py - g_last_touch_y;
                g_last_touch_x = px;
                g_last_touch_y = py;
                input_set_mouse_pos(px, py);
                if (g_touch_down) {
                    input_on_mousemove(dx, dy);
                }
                break;
            }
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                input_set_mouse_pos(x, y);
                if (action_masked != AMOTION_EVENT_ACTION_POINTER_UP || pointer_index == 0) {
                    input_on_mouseup(0);
                    g_touch_down = false;
                }
                break;
            default:
                break;
        }
    }

    android_app_clear_motion_events(input_buffer);

    for (uint64_t i = 0; i < input_buffer->keyEventsCount; i++) {
        GameActivityKeyEvent* kev = &input_buffer->keyEvents[i];

        bool is_down = (kev->action == AKEY_EVENT_ACTION_DOWN);
        bool is_up = (kev->action == AKEY_EVENT_ACTION_UP);
        if (!is_down && !is_up) continue;

        int js = -1;
        switch (kev->keyCode) {
            case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: js = 13; break;
            case AKEYCODE_TAB: js = 9; break;
            case AKEYCODE_DEL: js = 8; break;
            case AKEYCODE_ESCAPE: js = 27; break;
            case AKEYCODE_DPAD_LEFT: js = 37; break;
            case AKEYCODE_DPAD_UP: js = 38; break;
            case AKEYCODE_DPAD_RIGHT: js = 39; break;
            case AKEYCODE_DPAD_DOWN: js = 40; break;
            default: break;
        }

        if (js == 13 || js == 9 || js == 27) {
            if (is_down && g_on_key) g_on_key(js);
            continue;
        }
        if (!is_down) continue;

        if (js >= 0 && g_on_key) {
            g_on_key(js);
        }

        if (kev->unicodeChar > 0 && kev->unicodeChar < 0x10000 && g_on_char) {

            if (kev->unicodeChar >= 32) {
                g_on_char((unsigned int)kev->unicodeChar);
            }
        } else if (js < 0 && g_on_char) {

            int kc = kev->keyCode;
            if (kc >= AKEYCODE_A && kc <= AKEYCODE_Z) {
                bool shift = (kev->metaState & AMETA_SHIFT_ON) != 0;
                unsigned int ch = (unsigned int)('a' + (kc - AKEYCODE_A));
                if (shift) ch = (unsigned int)('A' + (kc - AKEYCODE_A));
                g_on_char(ch);
            } else if (kc >= AKEYCODE_0 && kc <= AKEYCODE_9) {
                g_on_char((unsigned int)('0' + (kc - AKEYCODE_0)));
            }
        }
    }
    if (input_buffer->keyEventsCount) {
        android_app_clear_key_events(input_buffer);
    }
}

static void poll_events(int timeout_ms) {
    if (!g_app) {
        return;
    }

    int events = 0;
    struct android_poll_source* source = NULL;
    while (ALooper_pollOnce(timeout_ms, NULL, &events, (void**)&source) >= 0) {
        if (source) {
            source->process(g_app, source);
        }
        if (g_app->destroyRequested) {
            break;
        }
        timeout_ms = 0;
    }

    handle_motion_events();
}

bool platform_init(int width, int height, const char* title) {
    (void)width;
    (void)height;
    (void)title;

    if (!g_app) {
        PW_AERR("platform_android_attach() was not called");
        return false;
    }

    g_time_origin = monotonic_seconds();
    g_app->userData = NULL;
    g_app->onAppCmd = handle_cmd;
    android_app_set_key_event_filter(g_app, NULL);
    android_app_set_motion_event_filter(g_app, NULL);
    ensure_userdata_dir();

    while (!g_window_ready && !g_app->destroyRequested) {
        if (g_app->window && g_display == EGL_NO_DISPLAY) {
            if (init_egl_display()) {
                create_surface(g_app->window);
            }
        }
        poll_events(-1);
    }

    return g_window_ready;
}

void platform_shutdown(void) {
    destroy_context();
    g_app = NULL;
}

static bool can_draw_frame(void) {
    return !g_paused && g_window_ready &&
           g_display != EGL_NO_DISPLAY &&
           g_surface != EGL_NO_SURFACE &&
           g_context != EGL_NO_CONTEXT;
}

void platform_run_loop(frame_callback_fn callback) {
    if (!g_app || !callback) {
        return;
    }

    double prev = platform_get_time();
    while (!g_app->destroyRequested) {

        int timeout_ms = -1;
        if (can_draw_frame()) {
            timeout_ms = g_has_focus ? 0 : 16;
        }
        poll_events(timeout_ms);
        platform_http_pump();

        if (g_app->destroyRequested) {
            break;
        }

        if (!can_draw_frame()) {
            continue;
        }

        process_soft_keyboard_text();

#ifdef PW_QUEST
        vr_openxr_begin_frame();
#endif

        double frame_start = platform_get_time();
        double dt = frame_start - prev;
        prev = frame_start;
        if (dt < 0.0) {
            dt = 0.0;
        }
        if (dt > 0.25) {
            dt = 0.25;
        }

        callback(dt);

        if (!can_draw_frame()) {
            continue;
        }

#ifdef PW_QUEST
        if (vr_openxr_submit_frame()) {

        } else
#endif
        if (!eglSwapBuffers(g_display, g_surface)) {
            EGLint err = eglGetError();
            PW_AERR("eglSwapBuffers failed (0x%x) -- will restore GL on next surface", err);
            destroy_surface();
            if (g_display != EGL_NO_DISPLAY && g_context != EGL_NO_CONTEXT) {
                eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(g_display, g_context);
                g_context = EGL_NO_CONTEXT;
            }
            g_window_ready = false;
            g_gl_restore_pending = true;
        }

        {
            int lim = platform_get_fps_limit();
            if (lim > 0 && lim < 240) {
                double target = 1.0 / (double)lim;
                double elapsed = platform_get_time() - frame_start;
                if (elapsed < target) {
                    double sleep_s = target - elapsed;
                    struct timespec ts;
                    ts.tv_sec = (time_t)sleep_s;
                    ts.tv_nsec = (long)((sleep_s - (double)ts.tv_sec) * 1e9);
                    if (ts.tv_nsec < 0) ts.tv_nsec = 0;
                    nanosleep(&ts, NULL);
                }
            }
        }
    }

    platform_shutdown();
}

double platform_get_time(void) {
    return monotonic_seconds() - g_time_origin;
}

void platform_flush_frame(void) {
    platform_http_pump();
    poll_events(0);
    if (g_display != EGL_NO_DISPLAY && g_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(g_display, g_surface);
    }
}

void platform_set_busy_redraw(void (*fn)(void)) { (void)fn; }

static int g_ui_event_block = 0;
void platform_block_ui_events(bool block) {
    if (block) g_ui_event_block++;
    else if (g_ui_event_block > 0) g_ui_event_block--;
}
bool platform_ui_events_blocked(void) { return g_ui_event_block > 0; }

void platform_set_window_size(int width, int height) {
    (void)width;
    (void)height;

}

bool platform_was_resized_by_user(void) {
    bool resized = g_resized_by_user;
    g_resized_by_user = false;
    return resized;
}

int platform_get_fps_limit(void) {
    extern int g_fps_limit_setting;
    int limit = g_fps_limit_setting;
    if (limit <= 0) return 0;
    return limit;
}

void platform_load_file(const char* path, file_load_callback cb, void* user) {
    if (!cb) {
        return;
    }
    if (!path || !path[0]) {
        cb(path, NULL, 0, user);
        return;
    }

    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
        if (pw_ahttp_start(path, NULL, 1, cb, user) < 0) {
            PW_AERR("platform_load_file: HTTP queue full: %s", path);
            cb(path, NULL, 0, user);
        }
        return;
    }

    char ud_path[512];
    if (platform_userdata_path(path, ud_path, sizeof(ud_path))) {
        FILE* f = fopen(ud_path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long size = ftell(f);
                if (size >= 0 && fseek(f, 0, SEEK_SET) == 0) {
                    uint8_t* data = (uint8_t*)malloc((size_t)size);
                    if (data) {
                        size_t n = fread(data, 1, (size_t)size, f);
                        fclose(f);
                        cb(path, data, n, user);
                        free(data);
                        return;
                    }
                }
            }
            fclose(f);
        }
    }

    AAssetManager* mgr = NULL;
    if (g_app && g_app->activity) {
        mgr = g_app->activity->assetManager;
    }
    if (!mgr) {
        PW_AERR("platform_load_file: no AssetManager for %s", path);
        cb(path, NULL, 0, user);
        return;
    }

    const char* candidates[4];
    int n_cand = 0;
    candidates[n_cand++] = path;
    if (strncmp(path, "assets/", 7) == 0) {
        candidates[n_cand++] = path + 7;
    }
    if (strncmp(path, "../assets/", 10) == 0) {
        candidates[n_cand++] = path + 10;
    }

    for (int i = 0; i < n_cand; i++) {
        AAsset* asset = AAssetManager_open(mgr, candidates[i], AASSET_MODE_BUFFER);
        if (!asset) {
            continue;
        }
        off_t size = AAsset_getLength(asset);
        if (size < 0) {
            AAsset_close(asset);
            continue;
        }
        uint8_t* data = (uint8_t*)malloc((size_t)size);
        if (!data) {
            AAsset_close(asset);
            cb(path, NULL, 0, user);
            return;
        }
        int read_n = AAsset_read(asset, data, (size_t)size);
        AAsset_close(asset);
        if (read_n < 0) {
            free(data);
            cb(path, NULL, 0, user);
            return;
        }
        PW_ALOG("loaded asset %s (%d bytes)", candidates[i], read_n);
        cb(path, data, (size_t)read_n, user);
        free(data);
        return;
    }

    PW_AERR("platform_load_file: not found: %s", path);
    cb(path, NULL, 0, user);
}

char* platform_read_text_file(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!path || !path[0]) return NULL;

    char ud_path[512];
    if (platform_userdata_path(path, ud_path, sizeof(ud_path))) {
        FILE* f = fopen(ud_path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long size = ftell(f);
                if (size >= 0 && fseek(f, 0, SEEK_SET) == 0) {
                    char* data = (char*)malloc((size_t)size + 1);
                    if (data) {
                        size_t n = fread(data, 1, (size_t)size, f);
                        fclose(f);
                        data[n] = '\0';
                        if (out_len) *out_len = n;
                        return data;
                    }
                }
            }
            fclose(f);
        }
    }

    AAssetManager* mgr = NULL;
    if (g_app && g_app->activity) mgr = g_app->activity->assetManager;
    if (!mgr) return NULL;

    const char* candidates[4];
    int n_cand = 0;
    candidates[n_cand++] = path;
    if (strncmp(path, "assets/", 7) == 0) candidates[n_cand++] = path + 7;
    if (strncmp(path, "../assets/", 10) == 0) candidates[n_cand++] = path + 10;

    for (int i = 0; i < n_cand; i++) {
        AAsset* asset = AAssetManager_open(mgr, candidates[i], AASSET_MODE_BUFFER);
        if (!asset) continue;
        off_t size = AAsset_getLength(asset);
        if (size < 0) { AAsset_close(asset); continue; }
        char* data = (char*)malloc((size_t)size + 1);
        if (!data) { AAsset_close(asset); return NULL; }
        int read_n = AAsset_read(asset, data, (size_t)size);
        AAsset_close(asset);
        if (read_n < 0) { free(data); return NULL; }
        data[read_n] = '\0';
        if (out_len) *out_len = (size_t)read_n;
        return data;
    }
    return NULL;
}

static JNIEnv* android_jni_env(void) {
    if (!g_vm) return NULL;
    JNIEnv* env = NULL;
    jint rs = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    if (rs == JNI_OK) return env;
    if (rs == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0) return NULL;
        return env;
    }
    return NULL;
}

static jclass android_find_pw_http(JNIEnv* env) {
    if (!env || !g_app || !g_app->activity || !g_app->activity->javaGameActivity) {
        return NULL;
    }
    jobject activity = g_app->activity->javaGameActivity;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return NULL;

    jmethodID get_cl = (*env)->GetMethodID(env, activity_cls, "getClassLoader",
                                           "()Ljava/lang/ClassLoader;");
    (*env)->DeleteLocalRef(env, activity_cls);
    if (!get_cl) return NULL;

    jobject loader = (*env)->CallObjectMethod(env, activity, get_cl);
    if (!loader) return NULL;

    jclass loader_cls = (*env)->GetObjectClass(env, loader);
    jmethodID load_class = (*env)->GetMethodID(env, loader_cls, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
    (*env)->DeleteLocalRef(env, loader_cls);
    if (!load_class) {
        (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }

    jstring name = (*env)->NewStringUTF(env, "games.polyworld.app.PwHttp");
    jclass http_cls = (jclass)(*env)->CallObjectMethod(env, loader, load_class, name);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, loader);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return http_cls;
}

static uint8_t* android_http_call(const char* method, const char* url, const char* body,
                                  size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!url || !url[0]) return NULL;

    JNIEnv* env = android_jni_env();
    if (!env) {
        PW_AERR("http: no JNIEnv");
        return NULL;
    }

    jclass http_cls = android_find_pw_http(env);
    if (!http_cls) {
        PW_AERR("http: PwHttp class not found");
        return NULL;
    }

    jmethodID mid;
    if (body) {
        mid = (*env)->GetStaticMethodID(env, http_cls, method, "(Ljava/lang/String;Ljava/lang/String;)[B");
    } else {
        mid = (*env)->GetStaticMethodID(env, http_cls, method, "(Ljava/lang/String;)[B");
    }
    if (!mid) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, http_cls);
        PW_AERR("http: method %s missing", method);
        return NULL;
    }

    jstring jurl = (*env)->NewStringUTF(env, url);
    jbyteArray jbytes = NULL;
    if (body) {
        jstring jbody = (*env)->NewStringUTF(env, body);
        jbytes = (jbyteArray)(*env)->CallStaticObjectMethod(env, http_cls, mid, jurl, jbody);
        (*env)->DeleteLocalRef(env, jbody);
    } else {
        jbytes = (jbyteArray)(*env)->CallStaticObjectMethod(env, http_cls, mid, jurl);
    }
    (*env)->DeleteLocalRef(env, jurl);

    if ((*env)->ExceptionCheck(env)) {
        jthrowable exc = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        g_http_last_error[0] = '\0';
        if (exc) {
            jclass throwable_cls = (*env)->GetObjectClass(env, exc);
            jmethodID get_msg = (*env)->GetMethodID(env, throwable_cls, "getMessage",
                                                    "()Ljava/lang/String;");
            jmethodID get_name = NULL;
            jclass class_cls = (*env)->FindClass(env, "java/lang/Class");
            if (class_cls) {
                get_name = (*env)->GetMethodID(env, class_cls, "getName", "()Ljava/lang/String;");
                (*env)->DeleteLocalRef(env, class_cls);
            }
            const char* cls_name = NULL;
            const char* msg = NULL;
            jstring jcls = NULL;
            jstring jmsg = NULL;
            if (get_name) {
                jcls = (jstring)(*env)->CallObjectMethod(env, throwable_cls, get_name);
                if (jcls) cls_name = (*env)->GetStringUTFChars(env, jcls, NULL);
            }
            if (get_msg) {
                jmsg = (jstring)(*env)->CallObjectMethod(env, exc, get_msg);
                if (jmsg) msg = (*env)->GetStringUTFChars(env, jmsg, NULL);
            }
            if (cls_name && msg && msg[0]) {
                snprintf(g_http_last_error, sizeof(g_http_last_error), "%s: %s", cls_name, msg);
            } else if (cls_name) {
                snprintf(g_http_last_error, sizeof(g_http_last_error), "%s", cls_name);
            } else if (msg && msg[0]) {
                snprintf(g_http_last_error, sizeof(g_http_last_error), "%s", msg);
            } else {
                snprintf(g_http_last_error, sizeof(g_http_last_error), "JNI exception");
            }
            if (cls_name && jcls) (*env)->ReleaseStringUTFChars(env, jcls, cls_name);
            if (msg && jmsg) (*env)->ReleaseStringUTFChars(env, jmsg, msg);
            if (jcls) (*env)->DeleteLocalRef(env, jcls);
            if (jmsg) (*env)->DeleteLocalRef(env, jmsg);
            (*env)->DeleteLocalRef(env, throwable_cls);
            (*env)->DeleteLocalRef(env, exc);
        } else {
            snprintf(g_http_last_error, sizeof(g_http_last_error), "JNI exception");
        }
        PW_AERR("http %s JNI: %s (%s)", method, url, g_http_last_error);
        (*env)->DeleteLocalRef(env, http_cls);
        return NULL;
    }
    if (!jbytes) {
        g_http_last_error[0] = '\0';
        jfieldID fid = (*env)->GetStaticFieldID(env, http_cls, "lastError", "Ljava/lang/String;");
        if (fid) {
            jstring jerr = (jstring)(*env)->GetStaticObjectField(env, http_cls, fid);
            if (jerr) {
                const char* err = (*env)->GetStringUTFChars(env, jerr, NULL);
                if (err) {
                    snprintf(g_http_last_error, sizeof(g_http_last_error), "%s", err);
                    (*env)->ReleaseStringUTFChars(env, jerr, err);
                }
                (*env)->DeleteLocalRef(env, jerr);
            }
        } else {
            (*env)->ExceptionClear(env);
            snprintf(g_http_last_error, sizeof(g_http_last_error), "request failed");
        }
        if (!g_http_last_error[0]) {
            snprintf(g_http_last_error, sizeof(g_http_last_error), "request failed (null)");
        }
        PW_AERR("http %s failed: %s (%s)", method, url, g_http_last_error);
        (*env)->DeleteLocalRef(env, http_cls);
        return NULL;
    }

    jsize n = (*env)->GetArrayLength(env, jbytes);
    uint8_t* data = (uint8_t*)malloc((size_t)n + 1);
    if (!data) {
        (*env)->DeleteLocalRef(env, jbytes);
        (*env)->DeleteLocalRef(env, http_cls);
        return NULL;
    }
    (*env)->GetByteArrayRegion(env, jbytes, 0, n, (jbyte*)data);
    data[n] = '\0';
    (*env)->DeleteLocalRef(env, jbytes);
    (*env)->DeleteLocalRef(env, http_cls);
    if (out_len) *out_len = (size_t)n;
    g_http_last_error[0] = '\0';
    PW_ALOG("http %s %s -> %d bytes", method, url, (int)n);
    return data;
}

#define PW_AHTTP_SLOTS 16
typedef struct {
    volatile int used;
    volatile int done;
    int is_async;
    char url[768];
    char* post_body;
    int is_post;
    file_load_callback cb;
    void* user;
    uint8_t* data;
    size_t len;
} PwAHttpSlot;
static PwAHttpSlot g_ahttp[PW_AHTTP_SLOTS];
static pthread_mutex_t g_ahttp_mu = PTHREAD_MUTEX_INITIALIZER;

static void* pw_ahttp_worker(void* arg) {
    int slot = (int)(intptr_t)arg;
    PwAHttpSlot* s = &g_ahttp[slot];
    size_t n = 0;
    uint8_t* data = s->is_post
        ? android_http_call("httpPost", s->url, s->post_body ? s->post_body : "", &n)
        : android_http_call("httpGet", s->url, NULL, &n);
    free(s->post_body);
    s->post_body = NULL;
    s->data = data;
    s->len = n;
    pthread_mutex_lock(&g_ahttp_mu);
    s->done = 1;
    pthread_mutex_unlock(&g_ahttp_mu);
    return NULL;
}

static int pw_ahttp_start(const char* url, const char* post_body, int is_async,
                          file_load_callback cb, void* user) {
    pthread_mutex_lock(&g_ahttp_mu);
    int slot = -1;
    for (int i = 0; i < PW_AHTTP_SLOTS; i++) {
        if (!g_ahttp[i].used) { slot = i; break; }
    }
    if (slot < 0) { pthread_mutex_unlock(&g_ahttp_mu); return -1; }
    memset(&g_ahttp[slot], 0, sizeof(g_ahttp[slot]));
    g_ahttp[slot].used = 1;
    pthread_mutex_unlock(&g_ahttp_mu);
    snprintf(g_ahttp[slot].url, sizeof(g_ahttp[slot].url), "%s", url);
    g_ahttp[slot].is_post = post_body != NULL;
    g_ahttp[slot].post_body = post_body ? strdup(post_body) : NULL;
    g_ahttp[slot].is_async = is_async;
    g_ahttp[slot].cb = cb;
    g_ahttp[slot].user = user;
    pthread_t thr;
    if (pthread_create(&thr, NULL, pw_ahttp_worker, (void*)(intptr_t)slot) != 0) {
        free(g_ahttp[slot].post_body);
        pthread_mutex_lock(&g_ahttp_mu);
        memset(&g_ahttp[slot], 0, sizeof(g_ahttp[slot]));
        pthread_mutex_unlock(&g_ahttp_mu);
        return -1;
    }
    pthread_detach(thr);
    return slot;
}

void platform_http_pump(void) {
    for (int i = 0; i < PW_AHTTP_SLOTS; i++) {
        file_load_callback cb = NULL;
        void* user = NULL;
        char url[768];
        uint8_t* data = NULL;
        size_t len = 0;
        int deliver = 0;
        pthread_mutex_lock(&g_ahttp_mu);
        if (g_ahttp[i].used && g_ahttp[i].done && g_ahttp[i].is_async) {
            deliver = 1;
            cb = g_ahttp[i].cb;
            user = g_ahttp[i].user;
            snprintf(url, sizeof(url), "%s", g_ahttp[i].url);
            data = g_ahttp[i].data;
            len = g_ahttp[i].len;
            g_ahttp[i].data = NULL;
            free(g_ahttp[i].post_body);
            memset(&g_ahttp[i], 0, sizeof(g_ahttp[i]));
        }
        pthread_mutex_unlock(&g_ahttp_mu);
        if (!deliver) continue;
        if (cb) cb(url, data, len, user);
        free(data);
    }
}

static uint8_t* pw_ahttp_sync(const char* url, const char* post_body, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!url || !url[0]) return NULL;
    int slot = pw_ahttp_start(url, post_body, 0, NULL, NULL);
    if (slot < 0) {
        return post_body
            ? android_http_call("httpPost", url, post_body, out_len)
            : android_http_call("httpGet", url, NULL, out_len);
    }
    while (1) {
        pthread_mutex_lock(&g_ahttp_mu);
        int done = g_ahttp[slot].done;
        pthread_mutex_unlock(&g_ahttp_mu);
        if (done) break;
        poll_events(0);
        usleep(1000);
    }
    pthread_mutex_lock(&g_ahttp_mu);
    uint8_t* data = g_ahttp[slot].data;
    size_t len = g_ahttp[slot].len;
    g_ahttp[slot].data = NULL;
    memset(&g_ahttp[slot], 0, sizeof(g_ahttp[slot]));
    pthread_mutex_unlock(&g_ahttp_mu);
    if (out_len) *out_len = len;
    return data;
}

uint8_t* platform_http_get(const char* url, size_t* out_len) {
    return pw_ahttp_sync(url, NULL, out_len);
}

uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len) {
    return pw_ahttp_sync(url, body ? body : "", out_len);
}

void platform_get_window_size(int* out_w, int* out_h) {
    if (out_w) *out_w = g_width > 0 ? g_width : 1280;
    if (out_h) *out_h = g_height > 0 ? g_height : 720;
}

bool platform_userdata_path(const char* filename, char* out, size_t out_sz) {
    if (!filename || !filename[0] || !out || out_sz < 2) {
        return false;
    }

    if (filename[0] == '/') {
        if (snprintf(out, out_sz, "%s", filename) >= (int)out_sz) return false;
        return true;
    }

    ensure_userdata_dir();
    if (!g_userdata_dir[0]) {
        PW_AERR("platform_userdata_path: userdata dir unavailable");
        return false;
    }

    if (strchr(filename, '/')) {
        char parent[512];
        if (snprintf(parent, sizeof(parent), "%s/%s", g_userdata_dir, filename) < (int)sizeof(parent)) {
            char* slash = strrchr(parent, '/');
            if (slash) {
                *slash = '\0';
                mkdir_p(parent);
            }
        }
    }

    if (snprintf(out, out_sz, "%s/%s", g_userdata_dir, filename) >= (int)out_sz) {
        return false;
    }
    return true;
}

bool platform_open_url(const char* url) {
    if (!url || !url[0]) return false;

    JNIEnv* env = android_jni_env();
    if (!env || !g_app || !g_app->activity || !g_app->activity->javaGameActivity) {
        PW_AERR("open_url: no JNI/activity");
        return false;
    }

    jobject activity = g_app->activity->javaGameActivity;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return false;

    jmethodID get_cl = (*env)->GetMethodID(env, activity_cls, "getClassLoader",
                                           "()Ljava/lang/ClassLoader;");
    (*env)->DeleteLocalRef(env, activity_cls);
    if (!get_cl) return false;

    jobject loader = (*env)->CallObjectMethod(env, activity, get_cl);
    if (!loader) return false;

    jclass loader_cls = (*env)->GetObjectClass(env, loader);
    jmethodID load_class = (*env)->GetMethodID(env, loader_cls, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
    (*env)->DeleteLocalRef(env, loader_cls);
    if (!load_class) {
        (*env)->DeleteLocalRef(env, loader);
        return false;
    }

    jstring name = (*env)->NewStringUTF(env, "games.polyworld.app.PwBrowser");
    jclass browser_cls = (jclass)(*env)->CallObjectMethod(env, loader, load_class, name);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, loader);
    if ((*env)->ExceptionCheck(env) || !browser_cls) {
        (*env)->ExceptionClear(env);
        PW_AERR("open_url: PwBrowser not found");
        return false;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, browser_cls, "openUrl",
        "(Landroid/content/Context;Ljava/lang/String;)Z");
    if (!mid) {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, browser_cls);
        PW_AERR("open_url: openUrl missing");
        return false;
    }

    jstring jurl = (*env)->NewStringUTF(env, url);
    jboolean ok = (*env)->CallStaticBooleanMethod(env, browser_cls, mid, activity, jurl);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jurl);
    (*env)->DeleteLocalRef(env, browser_cls);
    PW_ALOG("open_url %s -> %s", url, ok ? "ok" : "fail");
    return ok == JNI_TRUE;
}

void platform_clipboard_set(const char* utf8) { (void)utf8; }
const char* platform_clipboard_get(void) { return ""; }

void platform_set_cursor_captured(bool captured) {
    (void)captured;
}

void platform_set_cursor_pointer(bool pointer) {
    (void)pointer;
}

void platform_set_cursor_grab(int grab) {
    (void)grab;
}

void platform_set_fullscreen(bool fullscreen) {
    g_fullscreen = fullscreen;
}

bool platform_is_fullscreen(void) {
    return g_fullscreen;
}

void platform_show_soft_keyboard(bool show) {
    if (!g_app || !g_app->activity) return;
    if (show) {
        GameActivity_showSoftInput(g_app->activity, GAMEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
    } else {
        GameActivity_hideSoftInput(g_app->activity, GAMEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
        g_ime_visible = false;
        g_ime_inset_bottom = 0;
    }
}

int platform_get_ime_bottom_inset(void) {
    if (g_ime_inset_bottom > 0) return g_ime_inset_bottom;

    if (g_ime_visible && g_height > 0) return g_height * 2 / 5;
    return 0;
}

bool platform_ime_visible(void) {
    return g_ime_visible || g_ime_inset_bottom > 0;
}

bool platform_has_hardware_keyboard(void) {
    JNIEnv* env = android_jni_env();
    if (!env || !g_app || !g_app->activity || !g_app->activity->javaGameActivity) {
        return false;
    }
    jobject activity = g_app->activity->javaGameActivity;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return false;
    jmethodID get_resources = (*env)->GetMethodID(env, activity_cls, "getResources",
                                                  "()Landroid/content/res/Resources;");
    (*env)->DeleteLocalRef(env, activity_cls);
    if (!get_resources) return false;
    jobject resources = (*env)->CallObjectMethod(env, activity, get_resources);
    if (!resources) return false;
    jclass res_cls = (*env)->GetObjectClass(env, resources);
    jmethodID get_config = (*env)->GetMethodID(env, res_cls, "getConfiguration",
                                               "()Landroid/content/res/Configuration;");
    (*env)->DeleteLocalRef(env, res_cls);
    if (!get_config) {
        (*env)->DeleteLocalRef(env, resources);
        return false;
    }
    jobject config = (*env)->CallObjectMethod(env, resources, get_config);
    (*env)->DeleteLocalRef(env, resources);
    if (!config) return false;
    jclass cfg_cls = (*env)->GetObjectClass(env, config);
    jfieldID keyboard_fid = (*env)->GetFieldID(env, cfg_cls, "keyboard", "I");
    (*env)->DeleteLocalRef(env, cfg_cls);
    if (!keyboard_fid) {
        (*env)->DeleteLocalRef(env, config);
        return false;
    }
    jint keyboard = (*env)->GetIntField(env, config, keyboard_fid);
    (*env)->DeleteLocalRef(env, config);

    return keyboard == 2 || keyboard == 3;
}

bool platform_is_chromebook(void) {
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    cached = 0;
    JNIEnv* env = android_jni_env();
    if (!env || !g_app || !g_app->activity || !g_app->activity->javaGameActivity)
        return false;
    jobject activity = g_app->activity->javaGameActivity;
    jclass activity_cls = (*env)->GetObjectClass(env, activity);
    if (!activity_cls) return false;
    jmethodID get_pm = (*env)->GetMethodID(env, activity_cls, "getPackageManager",
                                           "()Landroid/content/pm/PackageManager;");
    (*env)->DeleteLocalRef(env, activity_cls);
    if (!get_pm) return false;
    jobject pm = (*env)->CallObjectMethod(env, activity, get_pm);
    if (!pm) return false;
    jclass pm_cls = (*env)->GetObjectClass(env, pm);
    jmethodID has_feat = (*env)->GetMethodID(env, pm_cls, "hasSystemFeature",
                                            "(Ljava/lang/String;)Z");
    (*env)->DeleteLocalRef(env, pm_cls);
    if (!has_feat) {
        (*env)->DeleteLocalRef(env, pm);
        return false;
    }
    const char* feats[] = {
        "org.chromium.arc",
        "org.chromium.arc.device_management",
        "android.hardware.type.pc"
    };
    for (int i = 0; i < 3; i++) {
        jstring js = (*env)->NewStringUTF(env, feats[i]);
        if (!js) continue;
        jboolean yes = (*env)->CallBooleanMethod(env, pm, has_feat, js);
        (*env)->DeleteLocalRef(env, js);
        if (yes) {
            cached = 1;
            break;
        }
    }
    (*env)->DeleteLocalRef(env, pm);
    return cached == 1;
}

bool platform_prefers_touch_controls(void) {
    if (platform_is_chromebook())
        return !platform_has_hardware_keyboard();
    return true;
}

const char* platform_http_last_error(void) {
    return g_http_last_error;
}

void platform_request_close(void) {
    if (g_app && g_app->activity) {
        GameActivity_finish(g_app->activity);
    }
}

#endif
