/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform.c                                                                          |
|   Purpose: emscripten / wasm platform                                                       |
\*-------------------------------------------------------------------------------------------*/

#include "platform.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#ifndef EM_TIMING_SET_TIMEOUT
#define EM_TIMING_SET_TIMEOUT EM_TIMING_SETTIMEOUT
#endif
#include <emscripten/html5.h>

static frame_callback_fn s_frame_callback = NULL;
static double s_last_time = 0.0;

static void em_main_loop(void* user_data) {
    (void)user_data;
    double now = emscripten_get_now() / 1000.0;
    double dt = now - s_last_time;
    s_last_time = now;

    if (dt > 0.25) {
        dt = 0.25;
    }

    if (s_frame_callback) {
        s_frame_callback(dt);
    }
}

bool platform_init(int width, int height, const char* title) {
    (void)title;
    (void)width;
    (void)height;

    double css_w, css_h;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    int real_w = (int)(css_w * dpr);
    int real_h = (int)(css_h * dpr);
    emscripten_set_canvas_element_size("#canvas", real_w, real_h);

    return true;
}

void platform_flush_frame(void) {}
void platform_set_busy_redraw(void (*fn)(void)) { (void)fn; }
void platform_set_window_size(int width, int height) { (void)width; (void)height; }
bool platform_was_resized_by_user(void) { return false; }
void platform_set_cursor_captured(bool captured) {
#ifdef __EMSCRIPTEN__
    if (!captured) {
        EM_ASM({
            if (document.pointerLockElement) document.exitPointerLock();
        });
    }
#else
    (void)captured;
#endif
}

void platform_set_cursor_pointer(bool pointer) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var c = (typeof Module !== 'undefined' && Module['canvas'])
            ? Module['canvas'] : document.getElementById('canvas');
        if (!c) return;
        if (c._pwGrab) return;
        c.style.cursor = $0 ? 'pointer' : 'default';
    }, pointer ? 1 : 0);
#else
    (void)pointer;
#endif
}

void platform_set_cursor_grab(int grab) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        var c = (typeof Module !== 'undefined' && Module['canvas'])
            ? Module['canvas'] : document.getElementById('canvas');
        if (!c) return;
        c._pwGrab = $0;
        if ($0 === 2) c.style.cursor = 'grabbing';
        else if ($0 === 1) c.style.cursor = 'grab';
        else c.style.cursor = 'default';
    }, grab);
#else
    (void)grab;
#endif
}

void platform_set_fullscreen(bool fullscreen) {
#ifdef __EMSCRIPTEN__
    if (fullscreen) {
        EM_ASM({
            var el = document.documentElement;
            if (el.requestFullscreen) el.requestFullscreen();
            else if (el.webkitRequestFullscreen) el.webkitRequestFullscreen();
        });
    } else {
        EM_ASM({
            if (document.exitFullscreen) document.exitFullscreen();
            else if (document.webkitExitFullscreen) document.webkitExitFullscreen();
        });
    }
#else
    (void)fullscreen;
#endif
}

bool platform_is_fullscreen(void) {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        return !!(document.fullscreenElement || document.webkitFullscreenElement);
    }) != 0;
#else
    return false;
#endif
}

void platform_shutdown(void) {
    s_frame_callback = NULL;
}

void platform_block_ui_events(bool block) { (void)block; }
bool platform_ui_events_blocked(void) { return false; }

void platform_run_loop(frame_callback_fn callback) {
    s_frame_callback = callback;
    s_last_time = emscripten_get_now() / 1000.0;
    emscripten_set_main_loop_timing(EM_TIMING_SET_TIMEOUT, 1000 / 60);
    emscripten_set_main_loop_arg(em_main_loop, NULL, 0, 1);
}

double platform_get_time(void) {
    return emscripten_get_now() / 1000.0;
}

typedef struct {
    file_load_callback cb;
    void* user;
    char path[256];
} FileLoadCtx;

void _pw_on_file_loaded(void* arg, void* data, int size) {
    FileLoadCtx* ctx = (FileLoadCtx*)arg;
    if (ctx->cb) {
        ctx->cb(ctx->path, (const uint8_t*)data, (size_t)size, ctx->user);
    }
    free(ctx);
}

void _pw_on_file_error(void* arg) {
    FileLoadCtx* ctx = (FileLoadCtx*)arg;
    if (ctx->cb) {
        ctx->cb(ctx->path, NULL, 0, ctx->user);
    }
    free(ctx);
}

void platform_load_file(const char* path, file_load_callback cb, void* user) {
    FileLoadCtx* ctx = (FileLoadCtx*)malloc(sizeof(FileLoadCtx));
    ctx->cb = cb;
    ctx->user = user;
    strncpy(ctx->path, path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';

    emscripten_async_wget_data(path, ctx, _pw_on_file_loaded, _pw_on_file_error);
}

char* platform_read_text_file(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!path || !path[0]) return NULL;
    FILE* f = fopen(path, "rb");
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

void platform_http_pump(void) {}

uint8_t* platform_http_get(const char* url, size_t* out_len) {
    (void)url;
    if (out_len) *out_len = 0;
    return NULL;
}

bool platform_userdata_path(const char* filename, char* out, size_t out_sz) {

    if (!filename || !filename[0] || !out || out_sz < 2) return false;
    if (snprintf(out, out_sz, "%s", filename) >= (int)out_sz) return false;
    return true;
}

uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len) {
    (void)url; (void)body;
    if (out_len) *out_len = 0;
    return NULL;
}

void platform_get_window_size(int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
}

void platform_show_soft_keyboard(bool show) { (void)show; }
bool platform_has_hardware_keyboard(void) { return false; }
bool platform_is_chromebook(void) { return false; }
bool platform_prefers_touch_controls(void) { return true; }
int platform_get_ime_bottom_inset(void) { return 0; }
bool platform_ime_visible(void) { return false; }
const char* platform_http_last_error(void) { return ""; }

bool platform_open_url(const char* url) {
    (void)url;
    return false;
}

void platform_clipboard_set(const char* utf8) { (void)utf8; }
const char* platform_clipboard_get(void) { return ""; }

void platform_request_close(void) {}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool platform_init(int width, int height, const char* title) {
    (void)width; (void)height; (void)title;
    return true;
}

void platform_shutdown(void) {}

void platform_run_loop(frame_callback_fn callback) {
    (void)callback;
}

double platform_get_time(void) {
    return 0.0;
}

void platform_load_file(const char* path, file_load_callback cb, void* user) {

    FILE* f = fopen(path, "rb");
    if (!f) {
        if (cb) cb(path, NULL, 0, user);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc((size_t)size);
    if (data) {
        fread(data, 1, (size_t)size, f);
    }
    fclose(f);
    if (cb) cb(path, data, (size_t)size, user);
    free(data);
}

char* platform_read_text_file(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!path || !path[0]) return NULL;
    FILE* f = fopen(path, "rb");
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

void platform_http_pump(void) {}

void platform_set_busy_redraw(void (*fn)(void)) { (void)fn; }
void platform_flush_frame(void) {}

uint8_t* platform_http_get(const char* url, size_t* out_len) {
    (void)url;
    if (out_len) *out_len = 0;
    return NULL;
}

bool platform_userdata_path(const char* filename, char* out, size_t out_sz) {
    if (!filename || !filename[0] || !out || out_sz < 2) return false;
    if (snprintf(out, out_sz, "%s", filename) >= (int)out_sz) return false;
    return true;
}

uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len) {
    (void)url; (void)body;
    if (out_len) *out_len = 0;
    return NULL;
}

void platform_get_window_size(int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
}

void platform_show_soft_keyboard(bool show) { (void)show; }
bool platform_has_hardware_keyboard(void) { return false; }
bool platform_is_chromebook(void) { return false; }
bool platform_prefers_touch_controls(void) { return false; }
int platform_get_ime_bottom_inset(void) { return 0; }
bool platform_ime_visible(void) { return false; }
const char* platform_http_last_error(void) { return ""; }

bool platform_open_url(const char* url) {
    (void)url;
    return false;
}

void platform_clipboard_set(const char* utf8) { (void)utf8; }
const char* platform_clipboard_get(void) { return ""; }

void platform_request_close(void) {}

void platform_set_cursor_captured(bool captured) { (void)captured; }
void platform_set_cursor_pointer(bool pointer) { (void)pointer; }
void platform_set_cursor_grab(int grab) { (void)grab; }

#endif
