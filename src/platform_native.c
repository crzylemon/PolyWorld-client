/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform_native.c                                                                   |
|   Purpose: desktop: GLFW + GL 3.3                                                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef __EMSCRIPTEN__

#include "platform.h"
#include "studio_embed.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

#include <GLFW/glfw3.h>
#ifndef _WIN32
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#else
#include <process.h>
#include <sys/stat.h>
#endif

extern unsigned char* stbi_load_from_memory(unsigned char const* buffer, int len,
                                           int* x, int* y, int* channels_in_file, int desired_channels);
extern void stbi_image_free(void* retval_from_stbi_load);

extern void input_on_keydown(int keycode);
extern void input_on_keyup(int keycode);
extern void input_on_mousedown(int button);
extern void input_on_mouseup(int button);
extern void input_on_mousemove(float dx, float dy);
extern void input_clear_mouse_delta(void);
extern void input_on_scroll(float delta);
extern void resize_canvas(int width, int height);
extern void input_set_mouse_pos(float x, float y);

extern bool chat_handle_key(int keycode, bool shift, bool ctrl);
extern bool chat_handle_char(unsigned int codepoint);
extern bool chat_handle_click(float x, float y);

extern bool login_handle_key(int keycode);
extern bool login_handle_char(unsigned int codepoint);

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <time.h>

static void take_screenshot(GLFWwindow* window) {
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    unsigned char* pixels = (unsigned char*)malloc(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    unsigned char* flipped = (unsigned char*)malloc(w * h * 3);
    for (int y = 0; y < h; y++) {
        memcpy(flipped + y * w * 3, pixels + (h - 1 - y) * w * 3, w * 3);
    }

    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char filename[128];
    snprintf(filename, sizeof(filename), "screenshot_%04d%02d%02d_%02d%02d%02d.png",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    stbi_write_png(filename, w, h, 3, flipped, w * 3);
    PW_LOG("Saved %s (%dx%d)\n", filename, w, h);

    free(pixels);
    free(flipped);
}

static GLFWwindow* g_window = NULL;
static bool g_host_mode = false;
static int g_host_canvas_w = 0;
static int g_host_canvas_h = 0;
static char g_userdata_dir[1024];
static GLFWcursor* g_pointer_cursor = NULL;
static GLFWcursor* g_grabbing_cursor = NULL;
static int g_want_pointer = 0;
static int g_want_grab = 0;
static int g_applied_style = -1;
static double g_start_time = 0.0;
static frame_callback_fn g_frame_callback = NULL;
static double g_last_cursor_x = 0.0;
static double g_last_cursor_y = 0.0;
static bool g_cursor_initialized = false;

static int g_skip_cursor_deltas = 0;
#ifndef _WIN32
static bool g_is_wayland = false;
#endif

static bool g_user_resized = false;
static bool g_programmatic_resize = false;
static bool g_force_fb_sync = false;
static int g_fb_w = 0;
static int g_fb_h = 0;
static int g_pending_fb_w = 0;
static int g_pending_fb_h = 0;
static int g_pending_fb_frames = 0;
static double g_pending_fb_since = 0.0;
static int g_last_maximized = -1;
static int g_last_fullscreen = -1;
static bool g_borderless_fullscreen = false;
static int g_saved_decorated = 1;
static bool g_fs_transition = false;

#ifndef _WIN32

static bool platform_detect_wayland(void) {
    const char* session_type = getenv("XDG_SESSION_TYPE");
    if (session_type && strcmp(session_type, "wayland") == 0) {
        return true;
    }

    if (getenv("WAYLAND_DISPLAY") != NULL) {
        return true;
    }

    return false;
}
#endif

static int glfw_to_js_keycode(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return key;
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return key;
    switch (key) {
        case GLFW_KEY_SPACE: return 32;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT: return 16;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return 17;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT: return 18;
        case GLFW_KEY_ESCAPE: return 27;
        case GLFW_KEY_TAB: return 9;
        case GLFW_KEY_ENTER: return 13;
        case GLFW_KEY_BACKSPACE: return 8;
        case GLFW_KEY_LEFT: return 37;
        case GLFW_KEY_UP: return 38;
        case GLFW_KEY_RIGHT: return 39;
        case GLFW_KEY_DOWN: return 40;
        case GLFW_KEY_F1: return 112;
        case GLFW_KEY_F2: return 113;
        case GLFW_KEY_F3: return 114;
        case GLFW_KEY_F4: return 115;
        case GLFW_KEY_F5: return 116;
        case GLFW_KEY_F6: return 117;
        case GLFW_KEY_F7: return 118;
        case GLFW_KEY_F8: return 119;
        case GLFW_KEY_F9: return 120;
        case GLFW_KEY_F10: return 121;
        case GLFW_KEY_F11: return 122;
        case GLFW_KEY_F12: return 123;
        default: return -1;
    }
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    if (key == GLFW_KEY_F2 && action == GLFW_PRESS) {
        take_screenshot(window);
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (mods & GLFW_MOD_CONTROL) {
            if (key == GLFW_KEY_C) {
                extern bool chat_handle_copy(void);
                if (chat_handle_copy()) return;
            } else if (key == GLFW_KEY_V) {
                extern bool chat_handle_paste(void);
                if (chat_handle_paste()) return;
            } else if (key == GLFW_KEY_X) {
                extern bool chat_handle_cut(void);
                if (chat_handle_cut()) return;
            } else if (key == GLFW_KEY_A) {
                extern bool chat_handle_select_all(void);
                if (chat_handle_select_all()) return;
            }
        }

        int login_key = 0;
        if (key == GLFW_KEY_ENTER) login_key = 13;
        else if (key == GLFW_KEY_TAB) login_key = 9;
        else if (key == GLFW_KEY_BACKSPACE) login_key = 8;
        else if (key == GLFW_KEY_LEFT) login_key = 37;
        else if (key == GLFW_KEY_RIGHT) login_key = 39;
        else if (key == GLFW_KEY_UP) login_key = 38;
        else if (key == GLFW_KEY_DOWN) login_key = 40;
        else if (key == GLFW_KEY_ESCAPE) login_key = 27;
        if (login_handle_key(login_key)) return;

        int chat_key = 0;
        switch (key) {
            case GLFW_KEY_ENTER: chat_key = 13; break;
            case GLFW_KEY_ESCAPE: chat_key = 27; break;
            case GLFW_KEY_BACKSPACE: chat_key = 8; break;
            case GLFW_KEY_TAB: chat_key = 9; break;
            case GLFW_KEY_UP: chat_key = 38; break;
            case GLFW_KEY_DOWN: chat_key = 40; break;
            case GLFW_KEY_LEFT: chat_key = 37; break;
            case GLFW_KEY_RIGHT: chat_key = 39; break;
            case GLFW_KEY_PAGE_UP: chat_key = 33; break;
            case GLFW_KEY_PAGE_DOWN: chat_key = 34; break;
            case GLFW_KEY_SLASH: chat_key = 191; break;
            default: break;
        }
        if (chat_key && chat_handle_key(chat_key, shift, ctrl)) return;
    }

    int js_key = glfw_to_js_keycode(key);
    if (js_key < 0) return;

    if (action == GLFW_PRESS) {

        if (chat_handle_key(js_key, shift, ctrl)) return;
        input_on_keydown(js_key);
    } else if (action == GLFW_RELEASE) {
        input_on_keyup(js_key);
    }
}

static void char_callback(GLFWwindow* window, unsigned int codepoint) {
    (void)window;
    if (login_handle_char(codepoint)) return;
    chat_handle_char(codepoint);
}

static void cursor_to_framebuffer(GLFWwindow* window, double* x, double* y) {
    int ww = 0, wh = 0;
    glfwGetWindowSize(window, &ww, &wh);
    int fw = g_fb_w, fh = g_fb_h;
    if (fw <= 0 || fh <= 0)
        glfwGetFramebufferSize(window, &fw, &fh);
    if (ww > 0 && wh > 0 && (fw != ww || fh != wh)) {
        *x *= (double)fw / (double)ww;
        *y *= (double)fh / (double)wh;
    }
}

static void sync_cursor_after_mode_change(GLFWwindow* window) {
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    g_last_cursor_x = mx;
    g_last_cursor_y = my;
    g_cursor_initialized = true;
    double fx = mx, fy = my;
    cursor_to_framebuffer(window, &fx, &fy);
    input_set_mouse_pos((float)fx, (float)fy);
    input_clear_mouse_delta();
    g_skip_cursor_deltas = 1;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;

    int js_button = -1;
    if (button == GLFW_MOUSE_BUTTON_LEFT) js_button = 0;
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) js_button = 1;
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) js_button = 2;
    if (js_button < 0) return;

    if (action == GLFW_PRESS) {

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        g_last_cursor_x = mx;
        g_last_cursor_y = my;
        g_cursor_initialized = true;
        double fx = mx, fy = my;
        cursor_to_framebuffer(window, &fx, &fy);
        input_set_mouse_pos((float)fx, (float)fy);

        {
            extern bool avatar_editor_mouse_bridge(int x, int y, int button, int pressed);
            if (avatar_editor_mouse_bridge((int)fx, (int)fy, js_button, 1))
                return;
        }

        if (js_button == 0) {
            if (chat_handle_click((float)fx, (float)fy)) return;
        }

        if (js_button == 0 || js_button == 2) {
            extern bool touch_controls_enabled(void);
            extern bool touch_controls_pointer_down(int32_t id, float x, float y,
                                                   int screen_w, int screen_h, uint32_t flags);
            if (touch_controls_enabled()) {
                int sw = 0, sh = 0;
                glfwGetFramebufferSize(window, &sw, &sh);
                uint32_t flags = (js_button == 2) ? 0x1u  : 0u;
                if (touch_controls_pointer_down(js_button == 2 ? 2 : 1,
                                               (float)fx, (float)fy, sw, sh, flags))
                    return;
            }
        }

        input_on_mousedown(js_button);
        if (js_button == 2) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            sync_cursor_after_mode_change(window);
        }
        if (js_button == 0) {
            extern bool login_handle_mouse(int x, int y);
            login_handle_mouse((int)fx, (int)fy);
        }
    } else {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        double fx = mx, fy = my;
        cursor_to_framebuffer(window, &fx, &fy);
        {
            extern bool avatar_editor_mouse_bridge(int x, int y, int button, int pressed);
            if (avatar_editor_mouse_bridge((int)fx, (int)fy, js_button, 0))
                return;
        }
        input_on_mouseup(js_button);
        if (js_button == 0) {
            extern bool chat_handle_mouseup(float x, float y);
            chat_handle_mouseup((float)fx, (float)fy);
            extern bool login_handle_mouseup(void);
            login_handle_mouseup();
            extern void avatar_editor_mouseup_bridge(void);
            avatar_editor_mouseup_bridge();
        }
        if (js_button == 0 || js_button == 2) {
            extern bool touch_controls_enabled(void);
            extern bool touch_controls_pointer_up(int32_t id);
            if (touch_controls_enabled())
                touch_controls_pointer_up(js_button == 2 ? 2 : 1);
        }
        if (js_button == 2) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            sync_cursor_after_mode_change(window);
        }
    }
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!g_cursor_initialized) {
        g_last_cursor_x = xpos;
        g_last_cursor_y = ypos;
        g_cursor_initialized = true;
        return;
    }
    if (g_skip_cursor_deltas > 0) {
        g_skip_cursor_deltas--;
        g_last_cursor_x = xpos;
        g_last_cursor_y = ypos;
        double fx = xpos, fy = ypos;
        cursor_to_framebuffer(window, &fx, &fy);
        input_set_mouse_pos((float)fx, (float)fy);
        input_clear_mouse_delta();
        return;
    }

    float dx = (float)(xpos - g_last_cursor_x);
    float dy = (float)(ypos - g_last_cursor_y);
    g_last_cursor_x = xpos;
    g_last_cursor_y = ypos;
    input_on_mousemove(dx, dy);
    double fx = xpos, fy = ypos;
    cursor_to_framebuffer(window, &fx, &fy);
    input_set_mouse_pos((float)fx, (float)fy);
    {
        extern bool touch_controls_enabled(void);
        extern bool touch_controls_pointer_move(int32_t id, float x, float y);
        extern bool touch_controls_owns_pointer(int32_t id);
        if (touch_controls_enabled()) {
            if (touch_controls_owns_pointer(1))
                touch_controls_pointer_move(1, (float)fx, (float)fy);
            if (touch_controls_owns_pointer(2))
                touch_controls_pointer_move(2, (float)fx, (float)fy);
        }
    }
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)xoffset;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    double fx = mx, fy = my;
    cursor_to_framebuffer(window, &fx, &fy);
    {
        extern bool avatar_editor_scroll_bridge(float x, float y, float delta);

        if (avatar_editor_scroll_bridge((float)fx, (float)fy, -(float)yoffset))
            return;
    }
    {
        extern bool login_screen_scroll_bridge(float delta);
        if (login_screen_scroll_bridge(-(float)yoffset))
            return;
    }
    input_on_scroll(-(float)yoffset);
}

static void query_drawable_size(int* out_w, int* out_h) {
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(g_window, &fw, &fh);
    if (fw > 0 && fh > 0) {
        if (out_w) *out_w = fw;
        if (out_h) *out_h = fh;
        return;
    }
    int ww = 0, wh = 0;
    glfwGetWindowSize(g_window, &ww, &wh);
    float csx = 1.0f, csy = 1.0f;
    glfwGetWindowContentScale(g_window, &csx, &csy);
    if (csx < 0.05f) csx = 1.0f;
    if (csy < 0.05f) csy = 1.0f;
    int alt_w = (int)((float)ww * csx + 0.5f);
    int alt_h = (int)((float)wh * csy + 0.5f);
    if (alt_w < 1) alt_w = 1;
    if (alt_h < 1) alt_h = 1;
    if (out_w) *out_w = alt_w;
    if (out_h) *out_h = alt_h;
}

static void apply_framebuffer_size(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (abs(w - g_fb_w) <= 1 && abs(h - g_fb_h) <= 1 && g_fb_w > 0) {
        g_pending_fb_w = 0;
        g_pending_fb_h = 0;
        g_pending_fb_frames = 0;
        g_pending_fb_since = 0.0;
        g_force_fb_sync = false;
        g_fs_transition = false;
        return;
    }
    g_fb_w = w;
    g_fb_h = h;
    g_pending_fb_w = 0;
    g_pending_fb_h = 0;
    g_pending_fb_frames = 0;
    g_pending_fb_since = 0.0;
    g_force_fb_sync = false;
    g_fs_transition = false;
    resize_canvas(w, h);
}

static void queue_framebuffer_size(int w, int h, bool prefer_immediate) {
    if (w <= 0 || h <= 0) return;
    if (abs(w - g_fb_w) <= 1 && abs(h - g_fb_h) <= 1 && g_fb_w > 0) {
        g_pending_fb_w = 0;
        g_pending_fb_h = 0;
        g_pending_fb_frames = 0;
        g_pending_fb_since = 0.0;
        g_force_fb_sync = false;
        g_fs_transition = false;
        return;
    }
    double now = glfwGetTime();
    if (w != g_pending_fb_w || h != g_pending_fb_h) {
        g_pending_fb_w = w;
        g_pending_fb_h = h;
        g_pending_fb_frames = 1;
        g_pending_fb_since = now;

        if (prefer_immediate && g_fb_w == 0) {
            apply_framebuffer_size(w, h);
        }
        return;
    }
    g_pending_fb_frames++;
    bool stable = g_pending_fb_frames >= 3;
    bool timed_out = (g_pending_fb_since > 0.0) && ((now - g_pending_fb_since) >= 0.12);
    if (!stable && !timed_out) return;
    apply_framebuffer_size(w, h);
}

static void sync_framebuffer_size(void) {
    if (!g_window) return;

    int maximized = glfwGetWindowAttrib(g_window, GLFW_MAXIMIZED) == GLFW_TRUE ? 1 : 0;
    int fullscreen = (g_borderless_fullscreen || glfwGetWindowMonitor(g_window) != NULL) ? 1 : 0;
    if (g_last_maximized < 0) g_last_maximized = maximized;
    if (g_last_fullscreen < 0) g_last_fullscreen = fullscreen;
    if (maximized != g_last_maximized || fullscreen != g_last_fullscreen) {
        g_last_maximized = maximized;
        g_last_fullscreen = fullscreen;
        g_force_fb_sync = true;

        g_pending_fb_w = 0;
        g_pending_fb_h = 0;
        g_pending_fb_frames = 0;
        g_pending_fb_since = 0.0;
    }

    int w = 0, h = 0;
    query_drawable_size(&w, &h);
    if (w <= 0 || h <= 0) return;

    bool immediate = g_programmatic_resize || g_fb_w == 0;
    if (immediate) {
        apply_framebuffer_size(w, h);
        return;
    }

    (void)g_force_fb_sync;
    (void)g_fs_transition;
    queue_framebuffer_size(w, h, false);
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    if (!g_programmatic_resize) g_user_resized = true;

    if (width > 0 && height > 0) {
        if (g_programmatic_resize || g_fb_w == 0)
            apply_framebuffer_size(width, height);
        else
            queue_framebuffer_size(width, height, false);
    }
}

static void window_maximize_callback(GLFWwindow* window, int maximized) {
    (void)window;
    (void)maximized;
    if (!g_programmatic_resize) g_user_resized = true;
    g_force_fb_sync = true;
}

static GLFWwindow* pw_try_create_window(int width, int height, const char* title,
                                        int major, int minor, int profile, int stencil_bits) {
    glfwDefaultWindowHints();
#ifndef _WIN32
    if (g_is_wayland)
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, profile);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, stencil_bits);
    if (pw_embed_client_active()) {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "polyworld");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "polyworld");
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "polyworld");
#endif
    return glfwCreateWindow(width, height, title, NULL, NULL);
}

bool platform_init(int width, int height, const char* title) {
#ifndef _WIN32
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    if (!glfwInit()) {
        PW_ERR(ERR_GENERIC, "Failed to init GLFW\n");
        return false;
    }

#ifndef _WIN32
    g_is_wayland = platform_detect_wayland();
    if (g_is_wayland) {
        PW_LOG("Wayland!!!!\n");
    }
#endif

    struct { int major, minor, profile, stencil; const char* label; } attempts[] = {
        { 3, 3, GLFW_OPENGL_CORE_PROFILE, 8, "3.3 core+stencil" },
        { 3, 3, GLFW_OPENGL_CORE_PROFILE, 0, "3.3 core" },
        { 3, 3, GLFW_OPENGL_COMPAT_PROFILE, 8, "3.3 compat+stencil" },
        { 3, 3, GLFW_OPENGL_COMPAT_PROFILE, 0, "3.3 compat" },
        { 3, 2, GLFW_OPENGL_CORE_PROFILE, 0, "3.2 core" },
    };
    const char* last_err = NULL;
    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        g_window = pw_try_create_window(width, height, title,
                                        attempts[i].major, attempts[i].minor,
                                        attempts[i].profile, attempts[i].stencil);
        if (g_window) {
            if (i > 0)
                PW_LOG("OpenGL context: %s (fallback)\n", attempts[i].label);
            break;
        }
        glfwGetError(&last_err);
        PW_LOG("Window create failed (%s): %s\n",
               attempts[i].label, last_err ? last_err : "unknown");
    }
    if (!g_window) {
        PW_ERR(ERR_GENERIC, "Failed to create window (need OpenGL 3.3+)\n");
        if (last_err) PW_ERR(ERR_GENERIC, "GLFW: %s\n", last_err);
#ifdef _WIN32
        MessageBoxA(NULL,
            "PolyWorld could not create a graphics window.\n\n"
            "This usually means the GPU/driver does not support OpenGL 3.3.\n\n"
            "On a Windows VM:\n"
            "  - QEMU: use virtio-gpu with 3D (e.g. -device virtio-vga-gl)\n"
            "    or GPU passthrough - default VGA/QXL has no OpenGL\n"
            "  - VirtualBox/VMware: enable 3D + Guest Additions / Tools\n"
            "  - Hyper-V Basic Display usually has no usable OpenGL\n"
            "  - Last resort: put Mesa opengl32.dll (+ deps) next to\n"
            "    polyworld.exe for software rendering (slow)",
            "PolyWorld - graphics init failed",
            MB_OK | MB_ICONERROR);
        if (getenv("WAYLAND_DISPLAY")) {
            PW_ERR(ERR_GENERIC, "Tip (Wine/Wayland): WAYLAND_DISPLAY= wine polyworld.exe\n");
        }
#endif
        glfwTerminate();
        return false;
    }

    {
        char path_a[576] = {0}, path_b[576] = {0};
        const char* candidates[5];
        int n_cand = 0;
        candidates[n_cand++] = "assets/polyworld.png";
        candidates[n_cand++] = "assets/polyworld_32.png";
#ifndef _WIN32
        {
            char exe[512] = {0};
            ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n > 0) {
                exe[n] = '\0';
                char* slash = strrchr(exe, '/');
                if (slash) {
                    *slash = '\0';
                    snprintf(path_a, sizeof(path_a), "%s/../assets/polyworld.png", exe);
                    snprintf(path_b, sizeof(path_b), "%s/assets/polyworld.png", exe);
                    candidates[n_cand++] = path_a;
                    candidates[n_cand++] = path_b;
                }
            }
        }
#endif
        candidates[n_cand] = NULL;

        GLFWimage icons[2];
        int icon_count = 0;
        unsigned char* pixels[2] = {0};
        for (int i = 0; candidates[i] && icon_count < 2; i++) {
            FILE* f = fopen(candidates[i], "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); continue; }
            unsigned char* filebuf = (unsigned char*)malloc((size_t)sz);
            if (!filebuf) { fclose(f); continue; }
            if (fread(filebuf, 1, (size_t)sz, f) != (size_t)sz) { free(filebuf); fclose(f); continue; }
            fclose(f);
            int w = 0, h = 0, ch = 0;
            unsigned char* rgba = stbi_load_from_memory(filebuf, (int)sz, &w, &h, &ch, 4);
            free(filebuf);
            if (!rgba || w <= 0 || h <= 0) { if (rgba) stbi_image_free(rgba); continue; }
            pixels[icon_count] = rgba;
            icons[icon_count].width = w;
            icons[icon_count].height = h;
            icons[icon_count].pixels = rgba;
            icon_count++;
        }
        if (icon_count > 0) {
            glfwSetWindowIcon(g_window, icon_count, icons);
            for (int i = 0; i < icon_count; i++) stbi_image_free(pixels[i]);
        }
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(0);

    glfwSetKeyCallback(g_window, key_callback);
    glfwSetCharCallback(g_window, char_callback);
    glfwSetMouseButtonCallback(g_window, mouse_button_callback);
    glfwSetCursorPosCallback(g_window, cursor_pos_callback);
    glfwSetScrollCallback(g_window, scroll_callback);
    glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);
    glfwSetWindowMaximizeCallback(g_window, window_maximize_callback);

    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    g_pointer_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    g_grabbing_cursor = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    g_applied_style = -1;
    g_want_pointer = 0;
    g_want_grab = 0;

    g_start_time = glfwGetTime();

    query_drawable_size(&g_fb_w, &g_fb_h);

    if (pw_embed_client_active())
        pw_embed_client_bind(g_window);

    return true;
}

int platform_get_fps_limit(void) {
    extern int g_fps_limit_override;
    if (g_fps_limit_override > 0) return g_fps_limit_override;
    if (g_fps_limit_override < 0) return 0;

    int monitor_hz = 60;
    GLFWmonitor* mon = g_window ? glfwGetWindowMonitor(g_window) : NULL;
    if (!mon) mon = glfwGetPrimaryMonitor();
    if (mon) {
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        if (mode && mode->refreshRate > 0) monitor_hz = mode->refreshRate;
    }

    extern int g_fps_limit_setting;
    int limit = g_fps_limit_setting;
    if (limit <= 0) return monitor_hz;
    return limit;
}

static void platform_apply_swap_interval(int fps_limit_setting) {
    if (!g_window) return;
    (void)fps_limit_setting;

    glfwSwapInterval(0);
}

void platform_set_window_size(int width, int height) {
    if (g_host_mode) return;
    if (pw_embed_client_active()) return;
    if (g_window) {
        g_programmatic_resize = true;
        glfwSetWindowSize(g_window, width, height);

        g_fb_w = 0;
        g_fb_h = 0;
        g_pending_fb_w = 0;
        g_pending_fb_h = 0;
        g_pending_fb_frames = 0;
        g_pending_fb_since = 0.0;
        g_force_fb_sync = true;
        sync_framebuffer_size();
        g_programmatic_resize = false;
    }
}

bool platform_was_resized_by_user(void) {
    return g_user_resized;
}

void platform_set_cursor_captured(bool captured) {
    if (!g_window) return;
    glfwSetInputMode(g_window, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    sync_cursor_after_mode_change(g_window);
    g_applied_style = -1;
}

static void platform_apply_cursor(void) {
    if (!g_window) return;
    if (glfwGetInputMode(g_window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL) {
        g_applied_style = -1;
        return;
    }
    int style = 0;
    if (g_want_grab == 2) style = 2;
    else if (g_want_grab == 1 || g_want_pointer) style = 1;
    if (style == g_applied_style) return;
    g_applied_style = style;
    GLFWcursor* c = NULL;
    if (style == 2)
        c = g_grabbing_cursor ? g_grabbing_cursor : g_pointer_cursor;
    else if (style == 1)
        c = g_pointer_cursor;
    glfwSetCursor(g_window, c);
}

void platform_set_cursor_pointer(bool pointer) {
    if (!g_window) return;
    g_want_pointer = pointer ? 1 : 0;
    platform_apply_cursor();
}

void platform_set_cursor_grab(int grab) {
    if (!g_window) return;
    if (grab < 0) grab = 0;
    if (grab > 2) grab = 2;
    g_want_grab = grab;
    platform_apply_cursor();
}

void platform_set_fullscreen(bool fullscreen) {
    if (g_host_mode) return;
    if (pw_embed_client_active()) return;
    if (!g_window) return;
    static int win_x = 100, win_y = 100, win_w = 1280, win_h = 720;

    if (fullscreen) {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        if (!mon) return;
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        if (!mode) return;

        if (!g_borderless_fullscreen && !glfwGetWindowMonitor(g_window)) {
            glfwGetWindowPos(g_window, &win_x, &win_y);
            glfwGetWindowSize(g_window, &win_w, &win_h);
            g_saved_decorated = glfwGetWindowAttrib(g_window, GLFW_DECORATED);
        }
        if (g_borderless_fullscreen) {
            glfwSetWindowAttrib(g_window, GLFW_DECORATED,
                                g_saved_decorated ? GLFW_TRUE : GLFW_FALSE);
            g_borderless_fullscreen = false;
        }

        g_fs_transition = true;
        g_force_fb_sync = true;
        g_fb_w = 0;
        g_fb_h = 0;
        glfwSetWindowMonitor(g_window, mon, 0, 0, mode->width, mode->height, 0);
    } else {
        g_fs_transition = true;
        g_force_fb_sync = true;
        g_fb_w = 0;
        g_fb_h = 0;
        if (glfwGetWindowMonitor(g_window)) {
            glfwSetWindowMonitor(g_window, NULL, win_x, win_y, win_w, win_h, 0);
        } else if (g_borderless_fullscreen) {
            glfwSetWindowPos(g_window, win_x, win_y);
            glfwSetWindowSize(g_window, win_w, win_h);
        }
        g_borderless_fullscreen = false;
        glfwSetWindowAttrib(g_window, GLFW_DECORATED,
                            g_saved_decorated ? GLFW_TRUE : GLFW_FALSE);
    }
    glfwSwapInterval(0);
}

bool platform_is_fullscreen(void) {
    if (!g_window) return false;
    if (g_borderless_fullscreen) return true;
    return glfwGetWindowMonitor(g_window) != NULL;
}

void platform_request_close(void) {
    if (g_host_mode) {
        extern bool pw_studio_host_intercept_close(void);
        if (pw_studio_host_intercept_close()) return;
    }
    if (g_window) glfwSetWindowShouldClose(g_window, GLFW_TRUE);
}

static void pw_http_shutdown_workers(void);
void platform_http_pump(void);

static int g_ui_event_block = 0;
static void (*g_busy_redraw)(void) = NULL;

void platform_block_ui_events(bool block) {
    if (block) g_ui_event_block++;
    else if (g_ui_event_block > 0) g_ui_event_block--;
}
bool platform_ui_events_blocked(void) { return g_ui_event_block > 0; }

void platform_set_busy_redraw(void (*fn)(void)) {
    g_busy_redraw = fn;
}

void platform_flush_frame(void) {
    platform_http_pump();
    if (g_host_mode) return;
    if (g_window) {
        if (pw_embed_client_active())
            pw_embed_client_pump(g_window);
        sync_framebuffer_size();
        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }
}

void platform_shutdown(void) {
    pw_http_shutdown_workers();
    if (g_host_mode) {
        g_window = NULL;
        g_host_mode = false;
        g_host_canvas_w = 0;
        g_host_canvas_h = 0;
        return;
    }
    if (g_window) {
        glfwSetCursor(g_window, NULL);
        glfwDestroyWindow(g_window);
        g_window = NULL;
    }
    if (g_pointer_cursor) {
        glfwDestroyCursor(g_pointer_cursor);
        g_pointer_cursor = NULL;
    }
    if (g_grabbing_cursor) {
        glfwDestroyCursor(g_grabbing_cursor);
        g_grabbing_cursor = NULL;
    }
    g_applied_style = -1;
    g_want_pointer = 0;
    g_want_grab = 0;
#ifndef _WIN32
    curl_global_cleanup();
#endif
    glfwTerminate();
}

void platform_run_loop(frame_callback_fn callback) {
    g_frame_callback = callback;
    double last_time = glfwGetTime();
    int applied_fps_setting = -999;

    while (!glfwWindowShouldClose(g_window)) {
        extern int g_fps_limit_setting;
        if (g_fps_limit_setting != applied_fps_setting) {
            platform_apply_swap_interval(g_fps_limit_setting);
            applied_fps_setting = g_fps_limit_setting;
        }

        double frame_start = glfwGetTime();
        double dt = frame_start - last_time;
        last_time = frame_start;

        if (dt > 0.1) dt = 0.1;

        glfwPollEvents();
        platform_http_pump();
        if (pw_embed_client_active())
            pw_embed_client_pump(g_window);
        sync_framebuffer_size();

        if (platform_is_fullscreen())
            glfwSwapInterval(0);
        g_frame_callback(dt);
        glfwSwapBuffers(g_window);

        int target_fps = platform_get_fps_limit();
        if (g_window && glfwGetWindowAttrib(g_window, GLFW_ICONIFIED)) {
            target_fps = 5;
        }
        if (target_fps > 0) {
            double target_time = 1.0 / (double)target_fps;
            for (;;) {
                double elapsed = glfwGetTime() - frame_start;
                if (elapsed >= target_time) break;
                double remain = target_time - elapsed;
                if (remain > 0.002) {
#ifdef _WIN32
                    Sleep((DWORD)((remain - 0.001) * 1000.0));
#else
                    struct timespec ts = {0, (long)((remain - 0.001) * 1e9)};
                    nanosleep(&ts, NULL);
#endif
                } else {
                    while ((glfwGetTime() - frame_start) < target_time) {
                    }
                    break;
                }
            }
        }
    }

    platform_shutdown();
}

double platform_get_time(void) {
    return glfwGetTime() - g_start_time;
}

#define PW_HTTP_SLOTS 48

typedef struct {
    volatile int used;
    volatile int done;
    int is_async;
    char url[768];
    char* post_body;
    file_load_callback cb;
    void* user;
    uint8_t* data;
    size_t len;
} PwHttpSlot;

static PwHttpSlot g_http_slots[PW_HTTP_SLOTS];
#ifdef _WIN32
static CRITICAL_SECTION g_http_cs;
static int g_http_cs_ready = 0;
static void pw_http_lock(void) {
    if (!g_http_cs_ready) { InitializeCriticalSection(&g_http_cs); g_http_cs_ready = 1; }
    EnterCriticalSection(&g_http_cs);
}
static void pw_http_unlock(void) { LeaveCriticalSection(&g_http_cs); }
static void pw_http_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
static pthread_mutex_t g_http_mu = PTHREAD_MUTEX_INITIALIZER;
static void pw_http_lock(void) { pthread_mutex_lock(&g_http_mu); }
static void pw_http_unlock(void) { pthread_mutex_unlock(&g_http_mu); }
static void pw_http_sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

#ifdef _WIN32

static wchar_t* pw_utf8_to_wide(const char* s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static int pw_parse_http_url(const char* url, char* host, size_t host_sz,
                             char* path, size_t path_sz, int* port, int* is_https) {
    if (!url || !host || !path || !port || !is_https) return 0;
    host[0] = path[0] = '\0';
    *port = 443;
    *is_https = 0;
    if (strncmp(url, "https://", 8) == 0) {
        *is_https = 1;
        url += 8;
        *port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (strncmp(url, "http://", 7) == 0) {
        url += 7;
        *port = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return 0;
    }
    const char* slash = strchr(url, '/');
    const char* colon = strchr(url, ':');
    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - url);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, url, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        size_t hlen = slash ? (size_t)(slash - url) : strlen(url);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, url, hlen);
        host[hlen] = '\0';
    }
    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }
    return host[0] != '\0';
}

static uint8_t* pw_http_download_win(const char* url, const char* post_body, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!url) return NULL;

    char host[256], path[1024];
    int port = 443, is_https = 0;
    if (!pw_parse_http_url(url, host, sizeof(host), path, sizeof(path), &port, &is_https))
        return NULL;

    HINTERNET session = WinHttpOpen(L"PolyWorld/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return NULL;
    WinHttpSetTimeouts(session, 10000, 10000, 10000, post_body ? 15000 : 30000);

    wchar_t* whost = pw_utf8_to_wide(host);
    HINTERNET conn = WinHttpConnect(session, whost, (INTERNET_PORT)port, 0);
    free(whost);
    if (!conn) { WinHttpCloseHandle(session); return NULL; }

    wchar_t* wpath = pw_utf8_to_wide(path);
    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, post_body ? L"POST" : L"GET", wpath,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    free(wpath);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return NULL;
    }
    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
    }

    BOOL ok;
    if (post_body) {
        DWORD body_len = (DWORD)strlen(post_body);
        const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
        ok = WinHttpSendRequest(req, headers, (DWORD)-1L,
                                (LPVOID)post_body, body_len, body_len, 0);
    } else {
        ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }
    if (!ok || !WinHttpReceiveResponse(req, NULL)) {
        PW_LOG("[Platform] HTTP %s failed: %s (err=%lu)\n",
               post_body ? "POST" : "GET", url, (unsigned long)GetLastError());
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return NULL;
    }

    if (!post_body) {
        DWORD status = 0, status_sz = sizeof(status);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz,
                                WINHTTP_NO_HEADER_INDEX) &&
            (status < 200 || status >= 300)) {
            PW_LOG("[Platform] HTTP GET status %lu: %s\n", (unsigned long)status, url);
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            WinHttpCloseHandle(session);
            return NULL;
        }
    }

    uint8_t* data = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { free(data); data = NULL; len = 0; break; }
        if (avail == 0) break;
        if (len + avail + 1 > cap) {
            size_t ncap = len + avail + 4096;
            uint8_t* nd = (uint8_t*)realloc(data, ncap);
            if (!nd) { free(data); data = NULL; len = 0; break; }
            data = nd;
            cap = ncap;
        }
        DWORD read = 0;
        if (!WinHttpReadData(req, data + len, avail, &read) || read == 0) {
            free(data); data = NULL; len = 0;
            break;
        }
        len += read;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!data || len == 0) {
        free(data);
        return NULL;
    }
    data[len] = '\0';
    if (out_len) *out_len = len;
    return data;
}
#else
typedef struct { uint8_t* data; size_t len; } PlatformDlBuf;
static size_t platform_dl_write(void* ptr, size_t size, size_t nmemb, void* ud) {
    PlatformDlBuf* b = (PlatformDlBuf*)ud;
    size_t total = size * nmemb;

    if (b->len + total > 8u * 1024u * 1024u) return 0;
    uint8_t* nd = (uint8_t*)realloc(b->data, b->len + total + 1);
    if (!nd) return 0;
    b->data = nd;
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}
static uint8_t* pw_http_download_curl(const char* url, const char* post_body, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!url) return NULL;
    PlatformDlBuf buf = {NULL, 0};
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, platform_dl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, post_body ? 5L : 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, post_body ? 8L : 20L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PolyWorld/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    if (post_body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || !buf.data) {
        PW_LOG("[Platform] HTTP %s failed: %s -> %s\n",
               post_body ? "POST" : "GET", url, curl_easy_strerror(res));
        free(buf.data);
        return NULL;
    }
    uint8_t* data = (uint8_t*)realloc(buf.data, buf.len + 1);
    if (!data) { free(buf.data); return NULL; }
    data[buf.len] = '\0';
    if (out_len) *out_len = buf.len;
    return data;
}
#endif

static uint8_t* pw_http_download(const char* url, const char* post_body, size_t* out_len) {
#ifdef _WIN32
    return pw_http_download_win(url, post_body, out_len);
#else
    return pw_http_download_curl(url, post_body, out_len);
#endif
}

#ifdef _WIN32
static unsigned __stdcall pw_http_worker(void* arg) {
#else
static void* pw_http_worker(void* arg) {
#endif
    int slot = (int)(intptr_t)arg;
    PwHttpSlot* s = &g_http_slots[slot];
    size_t n = 0;
    uint8_t* data = pw_http_download(s->url, s->post_body, &n);
    free(s->post_body);
    s->post_body = NULL;
    s->data = data;
    s->len = n;
    pw_http_lock();
    s->done = 1;
    pw_http_unlock();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int pw_http_alloc_slot(void) {
    pw_http_lock();
    for (int i = 0; i < PW_HTTP_SLOTS; i++) {
        if (!g_http_slots[i].used) {
            memset(&g_http_slots[i], 0, sizeof(g_http_slots[i]));
            g_http_slots[i].used = 1;
            pw_http_unlock();
            return i;
        }
    }
    pw_http_unlock();
    return -1;
}

static void pw_http_free_slot(int slot) {
    if (slot < 0 || slot >= PW_HTTP_SLOTS) return;
    pw_http_lock();
    free(g_http_slots[slot].post_body);
    free(g_http_slots[slot].data);
    memset(&g_http_slots[slot], 0, sizeof(g_http_slots[slot]));
    pw_http_unlock();
}

static int pw_http_start(const char* url, const char* post_body, int is_async,
                         file_load_callback cb, void* user) {
    if (!url || !url[0]) return -1;
    int slot = pw_http_alloc_slot();
    if (slot < 0) {
        PW_LOG("[Platform] HTTP queue full\n");
        return -1;
    }
    PwHttpSlot* s = &g_http_slots[slot];
    snprintf(s->url, sizeof(s->url), "%s", url);
    s->post_body = post_body ? strdup(post_body) : NULL;
    s->is_async = is_async;
    s->cb = cb;
    s->user = user;
    s->done = 0;
#ifdef _WIN32
    uintptr_t thr = _beginthreadex(NULL, 0, pw_http_worker, (void*)(intptr_t)slot, 0, NULL);
    if (!thr) {
        pw_http_free_slot(slot);
        return -1;
    }
    CloseHandle((HANDLE)thr);
#else
    pthread_t thr;
    if (pthread_create(&thr, NULL, pw_http_worker, (void*)(intptr_t)slot) != 0) {
        pw_http_free_slot(slot);
        return -1;
    }
    pthread_detach(thr);
#endif
    return slot;
}

static void pw_http_wait_slot(int slot) {

    platform_block_ui_events(true);
    while (1) {
        pw_http_lock();
        int done = g_http_slots[slot].done;
        pw_http_unlock();
        if (done) break;
        if (g_window) {
            glfwPollEvents();
            platform_http_pump();
            if (g_busy_redraw) g_busy_redraw();
            if (glfwWindowShouldClose(g_window)) break;
        }
        pw_http_sleep_ms(1);
    }

    g_ui_event_block = 0;
}

static void pw_http_shutdown_workers(void) {

    for (int spin = 0; spin < 200; spin++) {
        int busy = 0;
        pw_http_lock();
        for (int i = 0; i < PW_HTTP_SLOTS; i++) {
            if (g_http_slots[i].used && !g_http_slots[i].done) busy = 1;
        }
        pw_http_unlock();
        if (!busy) break;
        pw_http_sleep_ms(10);
    }
    for (int i = 0; i < PW_HTTP_SLOTS; i++)
        pw_http_free_slot(i);
}

void platform_http_pump(void) {
    for (int i = 0; i < PW_HTTP_SLOTS; i++) {
        file_load_callback cb = NULL;
        void* user = NULL;
        char url[768];
        uint8_t* data = NULL;
        size_t len = 0;
        int deliver = 0;

        pw_http_lock();
        if (g_http_slots[i].used && g_http_slots[i].done && g_http_slots[i].is_async) {
            deliver = 1;
            cb = g_http_slots[i].cb;
            user = g_http_slots[i].user;
            snprintf(url, sizeof(url), "%s", g_http_slots[i].url);
            data = g_http_slots[i].data;
            len = g_http_slots[i].len;
            g_http_slots[i].data = NULL;
            free(g_http_slots[i].post_body);
            g_http_slots[i].post_body = NULL;
            memset(&g_http_slots[i], 0, sizeof(g_http_slots[i]));
        }
        pw_http_unlock();

        if (!deliver) continue;
        if (cb) cb(url, data, len, user);
        free(data);
    }
}

static uint8_t* pw_http_sync(const char* url, const char* post_body, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0))
        return NULL;
    int slot = pw_http_start(url, post_body, 0, NULL, NULL);
    if (slot < 0) {

        return pw_http_download(url, post_body, out_len);
    }
    pw_http_wait_slot(slot);
    pw_http_lock();
    uint8_t* data = g_http_slots[slot].data;
    size_t len = g_http_slots[slot].len;
    g_http_slots[slot].data = NULL;
    memset(&g_http_slots[slot], 0, sizeof(g_http_slots[slot]));
    pw_http_unlock();
    if (out_len) *out_len = len;
    return data;
}

static void pw_load_local_file(const char* path, file_load_callback cb, void* user) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        char alt_path[512];
        snprintf(alt_path, sizeof(alt_path), "../%s", path);
        f = fopen(alt_path, "rb");
    }
    if (!f) {
        PW_LOG("[Platform] File not found: %s\n", path);
        if (cb) cb(path, NULL, 0, user);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); if (cb) cb(path, NULL, 0, user); return; }
    uint8_t* data = (uint8_t*)malloc((size_t)size);
    if (!data) { fclose(f); if (cb) cb(path, NULL, 0, user); return; }
    fread(data, 1, (size_t)size, f);
    fclose(f);
    if (cb) cb(path, data, (size_t)size, user);
    free(data);
}

void platform_load_file(const char* path, file_load_callback cb, void* user) {
    if (!cb) return;
    if (!path || !path[0]) { cb(path, NULL, 0, user); return; }
    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {

        int slot = -1;
        for (int attempt = 0; attempt < 50; attempt++) {
            slot = pw_http_start(path, NULL, 1, cb, user);
            if (slot >= 0) return;
            pw_http_sleep_ms(10);
        }
        PW_LOG("[Platform] HTTP queue full, drop: %s\n", path);
        cb(path, NULL, 0, user);
        return;
    }
    pw_load_local_file(path, cb, user);
}

char* platform_read_text_file(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!path || !path[0]) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) {
        char alt_path[512];
        snprintf(alt_path, sizeof(alt_path), "../%s", path);
        f = fopen(alt_path, "rb");
    }
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char* data = (char*)malloc((size_t)size + 1);
    if (!data) { fclose(f); return NULL; }
    size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[n] = '\0';
    if (out_len) *out_len = n;
    return data;
}

uint8_t* platform_http_get(const char* url, size_t* out_len) {
    return pw_http_sync(url, NULL, out_len);
}

uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len) {
    if (!body) { if (out_len) *out_len = 0; return NULL; }
    return pw_http_sync(url, body, out_len);
}

bool platform_open_url(const char* url) {
    if (!url || !url[0]) return false;
#ifdef _WIN32
    HINSTANCE r = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
#else
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    return system(cmd) == 0;
#endif
}

void platform_clipboard_set(const char* utf8) {
    if (!g_window) return;
    glfwSetClipboardString(g_window, utf8 ? utf8 : "");
}

const char* platform_clipboard_get(void) {
    if (!g_window) return "";
    const char* s = glfwGetClipboardString(g_window);
    return s ? s : "";
}

void platform_get_window_size(int* out_w, int* out_h) {
    if (g_host_mode && g_host_canvas_w > 0 && g_host_canvas_h > 0) {
        if (out_w) *out_w = g_host_canvas_w;
        if (out_h) *out_h = g_host_canvas_h;
        return;
    }
    if (!g_window) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    query_drawable_size(out_w, out_h);
}

void platform_host_attach(void* glfw_window) {
    g_window = (GLFWwindow*)glfw_window;
    g_host_mode = true;
}

void platform_host_set_canvas(int w, int h) {
    if (w > 0) g_host_canvas_w = w;
    if (h > 0) g_host_canvas_h = h;
}

bool platform_host_active(void) {
    return g_host_mode;
}

int platform_glfw_key_to_js(int glfw_key) {
    return glfw_to_js_keycode(glfw_key);
}

static void pw_trim_inplace(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '"')) {
        s[--n] = '\0';
    }
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void pw_strip_trailing_slash(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 1 && (s[n - 1] == '/' || s[n - 1] == '\\'))
        s[--n] = '\0';
}

static int pw_path_is_dir(const char* path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int pw_path_is_file(const char* path) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void pw_join_userdata(char* out, size_t n, const char* dir, const char* file) {
#ifdef _WIN32
    snprintf(out, n, "%s\\%s", dir, file);
#else
    snprintf(out, n, "%s/%s", dir, file);
#endif
}

static int pw_copy_file(const char* src, const char* dst) {
    if (!src || !dst || !src[0] || !dst[0]) return 0;
    if (strcmp(src, dst) == 0) return 1;
#ifdef _WIN32
    return CopyFileA(src, dst, FALSE) ? 1 : 0;
#else
    FILE* in = fopen(src, "rb");
    if (!in) return 0;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return 0; }
    char buf[8192];
    size_t r;
    int ok = 1;
    while ((r = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, r, out) != r) { ok = 0; break; }
    }
    fclose(in);
    fclose(out);
    return ok;
#endif
}

static int pw_desktop_field(const char* text, const char* key, char* out, size_t out_n) {
    if (!text || !key || !out || out_n < 2) return 0;
    out[0] = '\0';
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "\n%s=", key);
    const char* p = strstr(text, prefix);
    if (!p) {
        snprintf(prefix, sizeof(prefix), "%s=", key);
        if (strncmp(text, prefix, strlen(prefix)) == 0) p = text;
        else return 0;
    } else {
        p += 1;
    }
    p = strchr(p, '=');
    if (!p) return 0;
    p++;
    size_t i = 0;
    while (p[i] && p[i] != '\n' && p[i] != '\r' && i + 1 < out_n) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    pw_trim_inplace(out);

    if (strcmp(key, "Exec") == 0) {
        if (out[0] == '"') {
            char* q = strchr(out + 1, '"');
            if (q) *q = '\0';
            memmove(out, out + 1, strlen(out + 1) + 1);
        } else {
            char* sp = strchr(out, ' ');
            if (sp) *sp = '\0';
        }
    }
    return out[0] != '\0';
}

static bool pw_datadir_from_exe_path(const char* exe, char* out, size_t out_sz) {
    if (!exe || !exe[0] || !out || out_sz < 2) return false;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", exe);
    pw_strip_trailing_slash(dir);
    char* slash = strrchr(dir, '/');
#ifdef _WIN32
    char* bslash = strrchr(dir, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = '\0';
    pw_strip_trailing_slash(dir);
    char assets[1080];
    pw_join_userdata(assets, sizeof(assets), dir, "assets");
    if (pw_path_is_dir(assets)) {
        snprintf(out, out_sz, "%s", dir);
        return true;
    }
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", dir);
    slash = strrchr(parent, '/');
#ifdef _WIN32
    bslash = strrchr(parent, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) {
        *slash = '\0';
        pw_join_userdata(assets, sizeof(assets), parent, "assets");
        if (pw_path_is_dir(assets)) {
            snprintf(out, out_sz, "%s", parent);
            return true;
        }
    }
    if (pw_path_is_dir(dir)) {
        snprintf(out, out_sz, "%s", dir);
        return true;
    }
    return false;
}

void platform_set_userdata_dir(const char* dir) {
    if (!dir || !dir[0]) {
        g_userdata_dir[0] = '\0';
        return;
    }
    snprintf(g_userdata_dir, sizeof(g_userdata_dir), "%s", dir);
    pw_strip_trailing_slash(g_userdata_dir);
}

void platform_migrate_userdata_files(const char* from_dir, const char* to_dir) {
    if (!from_dir || !to_dir || !from_dir[0] || !to_dir[0]) return;
    char from[1024], to[1024];
    snprintf(from, sizeof(from), "%s", from_dir);
    snprintf(to, sizeof(to), "%s", to_dir);
    pw_strip_trailing_slash(from);
    pw_strip_trailing_slash(to);
    if (strcmp(from, to) == 0) return;
    if (!pw_path_is_dir(from) || !pw_path_is_dir(to)) return;

    static const char* names[] = {
        "options.cfg",
        "polyworld_session.dat",
        "polyworld_session_vid1.dat",
        "polyworld_session_vid2.dat",
        "polyworld_session_vid3.dat",
        "polyworld_session_vid4.dat",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        char src[1080], dst[1080];
        pw_join_userdata(src, sizeof(src), from, names[i]);
        pw_join_userdata(dst, sizeof(dst), to, names[i]);
        if (!pw_path_is_file(src)) continue;
        if (pw_copy_file(src, dst))
            PW_LOG("Migrated %s -> %s\n", names[i], to);
    }
}

bool platform_find_registered_client_datadir(char* out, size_t out_sz) {
    if (!out || out_sz < 2) return false;
    out[0] = '\0';
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Classes\\polyworld\\shell\\open\\command",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    char existing[MAX_PATH + 64];
    DWORD existing_sz = sizeof(existing);
    DWORD type = 0;
    LONG ok = RegQueryValueExA(hKey, NULL, NULL, &type, (LPBYTE)existing, &existing_sz);
    RegCloseKey(hKey);
    if (ok != ERROR_SUCCESS || type != REG_SZ) return false;
    char exe[MAX_PATH];
    exe[0] = '\0';
    if (existing[0] == '"') {
        const char* q = strchr(existing + 1, '"');
        size_t n = q ? (size_t)(q - (existing + 1)) : strlen(existing + 1);
        if (n >= sizeof(exe)) n = sizeof(exe) - 1;
        memcpy(exe, existing + 1, n);
        exe[n] = '\0';
    } else {
        snprintf(exe, sizeof(exe), "%s", existing);
        char* sp = strchr(exe, ' ');
        if (sp) *sp = '\0';
    }
    return pw_datadir_from_exe_path(exe, out, out_sz);
#else
    const char* home = getenv("HOME");
    if (!home || !home[0]) return false;
    char desktop_path[512];
    snprintf(desktop_path, sizeof(desktop_path),
             "%s/.local/share/applications/polyworld.desktop", home);
    FILE* df = fopen(desktop_path, "r");
    if (!df) return false;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, df);
    buf[n] = '\0';
    fclose(df);
    char path_field[1024] = {0};
    char exec_field[1024] = {0};
    if (pw_desktop_field(buf, "Path", path_field, sizeof(path_field)) && pw_path_is_dir(path_field)) {
        char assets[1080];
        pw_join_userdata(assets, sizeof(assets), path_field, "assets");
        if (pw_path_is_dir(assets) || !pw_desktop_field(buf, "Exec", exec_field, sizeof(exec_field))) {
            snprintf(out, out_sz, "%s", path_field);
            pw_strip_trailing_slash(out);
            return true;
        }
    }
    if (pw_desktop_field(buf, "Exec", exec_field, sizeof(exec_field)))
        return pw_datadir_from_exe_path(exec_field, out, out_sz);
    if (path_field[0] && pw_path_is_dir(path_field)) {
        snprintf(out, out_sz, "%s", path_field);
        pw_strip_trailing_slash(out);
        return true;
    }
    return false;
#endif
}

bool platform_userdata_path(const char* filename, char* out, size_t out_sz) {
    if (!filename || !filename[0] || !out || out_sz < 2) return false;
    if (g_userdata_dir[0]) {
        pw_join_userdata(out, out_sz, g_userdata_dir, filename);
        return true;
    }
#ifdef __APPLE__
    {
        const char* home = getenv("HOME");
        if (home && home[0]) {
            char dir[768];
            if (snprintf(dir, sizeof(dir), "%s/Library/Application Support/PolyWorld", home) >= (int)sizeof(dir))
                return false;
            mkdir(dir, 0755);
            if (snprintf(out, out_sz, "%s/%s", dir, filename) >= (int)out_sz) return false;
            return true;
        }
    }
#endif

    if (snprintf(out, out_sz, "%s", filename) >= (int)out_sz) return false;
    return true;
}

void platform_show_soft_keyboard(bool show) { (void)show; }
bool platform_has_hardware_keyboard(void) { return true; }
bool platform_is_chromebook(void) { return false; }
bool platform_prefers_touch_controls(void) { return false; }
int platform_get_ime_bottom_inset(void) { return 0; }
bool platform_ime_visible(void) { return false; }
const char* platform_http_last_error(void) { return ""; }

#endif
