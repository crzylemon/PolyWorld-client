/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform_android.h                                                                  |
|   Purpose: Android-specific hooks                                                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef PLATFORM_ANDROID_H
#define PLATFORM_ANDROID_H

#include <EGL/egl.h>

struct android_app;

void platform_android_attach(struct android_app* app);
struct android_app* platform_android_app(void);
EGLDisplay platform_android_egl_display(void);
EGLConfig platform_android_egl_config(void);
EGLContext platform_android_egl_context(void);

typedef void (*platform_android_key_fn)(int js_keycode);
typedef void (*platform_android_char_fn)(unsigned int codepoint);
typedef void (*platform_android_text_fn)(const char* utf8, int len);
void platform_android_set_text_handlers(platform_android_key_fn on_key,
                                       platform_android_char_fn on_char);
void platform_android_set_text_input_handler(platform_android_text_fn on_text);

void platform_android_set_ime_text(const char* utf8);

void platform_android_configure_ime(bool password, int action);

#endif
