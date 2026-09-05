/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: android_boot.c                                                                      |
|   Purpose: Android entry point!                                                             |
\*-------------------------------------------------------------------------------------------*/

#ifdef __ANDROID__

#include "platform.h"
#include "platform_android.h"
#include "input.h"
#include "login_screen.h"
#include "chat.h"
#include "pw_android_game.h"
#ifdef PW_QUEST
#include "vr_hub.h"
#endif

#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <string.h>

#define PW_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "PolyWorld", __VA_ARGS__)

static int g_prev_active_field = -1;
static bool g_chat_ime_up = false;
static bool g_login_ime_up = false;
#ifdef PW_QUEST
static int g_quest_boot_frames;
#endif

extern bool chat_handle_key(int keycode, bool shift, bool ctrl);
extern bool chat_handle_char(unsigned int codepoint);
extern bool login_handle_mouse(int x, int y);
extern bool login_handle_mouseup(void);
extern bool login_screen_scroll_bridge(float delta);
extern bool avatar_editor_scroll_bridge(float x, float y, float delta);
extern int pw_android_chat_wants_ime(void);

static void on_text_key(int js_keycode) {
    LoginScreen* ls = pw_game_login_screen();
    if (ls && ls->phase == 0) {
        login_screen_on_key(ls, js_keycode, false, false);
        return;
    }
    chat_handle_key(js_keycode, false, false);
}

static void on_text_char(unsigned int codepoint) {
    if (g_login_ime_up || g_chat_ime_up) return;
    LoginScreen* ls = pw_game_login_screen();
    if (ls && ls->phase == 0) {
        login_screen_on_char(ls, codepoint);
        return;
    }
    chat_handle_char(codepoint);
}

static bool copy_ime_printable(const char* utf8, char* out, size_t out_sz, int* out_len) {
    bool submit = false;
    size_t n = 0;
    if (!utf8) utf8 = "";
    for (size_t i = 0; utf8[i] && n + 1 < out_sz; i++) {
        unsigned char ch = (unsigned char)utf8[i];
        if (ch == '\n' || ch == '\r') {
            submit = true;
            continue;
        }
        if (ch < 32) continue;
        out[n++] = (char)ch;
    }
    out[n] = '\0';
    if (out_len) *out_len = (int)n;
    return submit;
}

static void on_text_input(const char* utf8, int len) {
    (void)len;
    char buf[256];
    int blen = 0;
    bool submit = copy_ime_printable(utf8, buf, sizeof(buf), &blen);

    LoginScreen* ls = pw_game_login_screen();
    if (ls && ls->phase == 0 && ls->active_field >= 0) {
        if (ls->active_field == 0) {
            size_t max = sizeof(ls->username) - 1;
            size_t n = (size_t)blen < max ? (size_t)blen : max;
            memcpy(ls->username, buf, n);
            ls->username[n] = '\0';
            ls->username_len = (int)n;
        } else {
            size_t max = sizeof(ls->password) - 1;
            size_t n = (size_t)blen < max ? (size_t)blen : max;
            memcpy(ls->password, buf, n);
            ls->password[n] = '\0';
            ls->password_len = (int)n;
        }
        if (submit) {
            platform_android_set_ime_text(buf);
            on_text_key(13);
            PW_ALOG("IME submit login");
        }
        return;
    }
    if (pw_android_chat_wants_ime()) {
        Chat* c = pw_game_chat();
        if (c) chat_set_input_text(c, buf);
        if (submit) {
            platform_android_set_ime_text(buf);
            on_text_key(13);
            platform_android_set_ime_text("");
            PW_ALOG("IME submit chat");
        }
    }
}

static void update_ime(void) {
    LoginScreen* ls = pw_game_login_screen();
    bool want_login = ls && ls->phase == 0 && ls->active_field >= 0;
    bool want_chat = pw_android_chat_wants_ime() != 0;
    bool hw = false;

#ifdef PW_QUEST
    if (g_quest_boot_frames < 45 || vr_hub_active()) {
        want_login = false;
        if (g_quest_boot_frames == 1)
            platform_show_soft_keyboard(false);
    }
#endif
    if (want_login) {
        bool field_changed = (ls->active_field != g_prev_active_field);
        if (field_changed || !g_login_ime_up) {
            g_prev_active_field = ls->active_field;
            g_login_ime_up = true;
            g_chat_ime_up = false;
            if (!hw) {
                bool password = (ls->active_field == 1);
                platform_android_configure_ime(password, 1);
                const char* seed = password ? ls->password : ls->username;
                platform_android_set_ime_text(seed);
                platform_show_soft_keyboard(true);
                PW_ALOG("showing soft keyboard for login field %d", ls->active_field);
            } else {
                platform_show_soft_keyboard(false);
            }
        }
        return;
    }

    if (g_login_ime_up) {
        g_login_ime_up = false;
        g_prev_active_field = -1;
        platform_show_soft_keyboard(false);
    }

    if (want_chat) {
        if (!g_chat_ime_up) {
            g_chat_ime_up = true;
            if (!hw) {
                Chat* c = pw_game_chat();
                platform_android_configure_ime(false, 2);
                platform_android_set_ime_text(c ? c->input_buf : "");
                platform_show_soft_keyboard(true);
                PW_ALOG("showing soft keyboard for chat");
            }
        }
    } else if (g_chat_ime_up) {
        g_chat_ime_up = false;
        platform_show_soft_keyboard(false);
        platform_android_set_ime_text("");
    }
}

static void android_game_frame(double dt) {
    const InputState* in = input_get_state();
    static bool prev_mouse_left = false;
    static bool prev_mouse_held = false;
#ifdef PW_QUEST
    g_quest_boot_frames++;
    bool allow_ptr = g_quest_boot_frames > 20;
#else
    bool allow_ptr = true;
#endif
    bool held = input_mouse_left_held();
    if (allow_ptr && in->mouse_left && !prev_mouse_left) {
        login_handle_mouse((int)in->mouse_x, (int)in->mouse_y);
    }
    if (prev_mouse_held && !held) {
        login_handle_mouseup();
    }
    if (in->scroll_delta != 0.0f) {
        if (!avatar_editor_scroll_bridge(in->mouse_x, in->mouse_y, in->scroll_delta))
            login_screen_scroll_bridge(in->scroll_delta);
    }
    pw_game_frame(dt);
    input_post_frame();
    prev_mouse_left = in->mouse_left;
    prev_mouse_held = input_mouse_left_held();
    update_ime();
}

void android_main(struct android_app* app) {
    PW_ALOG("android_main starting (full game / offline)");

    platform_android_attach(app);
    platform_android_set_text_handlers(on_text_key, on_text_char);
    platform_android_set_text_input_handler(on_text_input);
    input_init();

    if (!platform_init(0, 0, "PolyWorld")) {
        PW_ALOG("platform_init failed");
        return;
    }

    if (!pw_game_init()) {
        PW_ALOG("pw_game_init failed");
        return;
    }

    PW_ALOG("hw_keyboard=%d", platform_has_hardware_keyboard() ? 1 : 0);
    PW_ALOG("entering game loop");
    platform_run_loop(android_game_frame);
    PW_ALOG("android_main exiting");
}

#endif
