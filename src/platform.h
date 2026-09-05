/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform.h                                                                          |
|   Purpose: WASM / desktop / Android / Apple (macOS GLFW, iOS EAGL sketch)                   |
\*-------------------------------------------------------------------------------------------*/

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool platform_init(int width, int height, const char* title);
void platform_shutdown(void);

typedef void (*frame_callback_fn)(double dt);
void platform_run_loop(frame_callback_fn callback);
double platform_get_time(void);
void platform_flush_frame(void);
void platform_set_busy_redraw(void (*fn)(void));
void platform_set_window_size(int width, int height);
bool platform_was_resized_by_user(void);

typedef void (*file_load_callback)(const char* path, const uint8_t* data, size_t len, void* user);

void platform_load_file(const char* path, file_load_callback cb, void* user);

void platform_http_pump(void);

uint8_t* platform_http_get(const char* url, size_t* out_len);
uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len);

void platform_get_window_size(int* out_w, int* out_h);

bool platform_userdata_path(const char* filename, char* out, size_t out_sz);

char* platform_read_text_file(const char* path, size_t* out_len);

bool platform_open_url(const char* url);

void platform_clipboard_set(const char* utf8);
const char* platform_clipboard_get(void);

void platform_set_cursor_captured(bool captured);

void platform_set_cursor_pointer(bool pointer);

void platform_set_cursor_grab(int grab);
void platform_set_fullscreen(bool fullscreen);
bool platform_is_fullscreen(void);

void platform_show_soft_keyboard(bool show);
bool platform_has_hardware_keyboard(void);
bool platform_is_chromebook(void);
bool platform_prefers_touch_controls(void);

int platform_get_ime_bottom_inset(void);
bool platform_ime_visible(void);

void platform_block_ui_events(bool block);
bool platform_ui_events_blocked(void);

const char* platform_http_last_error(void);
void platform_request_close(void);
int platform_get_fps_limit(void);

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(PW_IOS)

void platform_host_attach(void* glfw_window);
void platform_host_set_canvas(int w, int h);
bool platform_host_active(void);
int platform_glfw_key_to_js(int glfw_key);

void platform_set_userdata_dir(const char* dir);
bool platform_find_registered_client_datadir(char* out, size_t out_sz);
void platform_migrate_userdata_files(const char* from_dir, const char* to_dir);
#endif

#endif
