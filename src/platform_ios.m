/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: platform_ios.m                                                                      |
|   Purpose: UIKit + EAGL / GLES3 (sketch). OpenGL ES is deprecated; Metal is later.          |
\*-------------------------------------------------------------------------------------------*/

#ifdef PW_IOS

#include "platform.h"
#include "platform_ios.h"
#include "input.h"
#include "pw_gles.h"

#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <QuartzCore/QuartzCore.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static UIView* g_view = nil;
static EAGLContext* g_ctx = nil;
static GLuint g_framebuffer = 0;
static GLuint g_color_rb = 0;
static GLuint g_depth_rb = 0;
static int g_width = 0;
static int g_height = 0;
static bool g_resized = false;
static bool g_close = false;
static int g_ui_block = 0;
static int g_fps_limit = 0;
static char g_userdata_dir[512] = {0};
static char g_http_last_error[256] = {0};
static char g_clip[4096] = {0};
static double g_time_origin = 0.0;
static frame_callback_fn g_frame_cb = NULL;
static void (*g_busy_redraw)(void) = NULL;
static bool g_touch_down = false;
static bool g_kb_visible = false;

static double ios_now(void) {
    return CACurrentMediaTime() - g_time_origin;
}

static void ios_log(const char* msg) {
    NSLog(@"[PolyWorld] %s", msg ? msg : "");
}

static bool mkdir_p(const char* path) {
    if (!path || !path[0]) return false;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(tmp, 0755);
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static bool ios_make_buffers(void) {
    if (!g_ctx || !g_view) return false;
    [EAGLContext setCurrentContext:g_ctx];
    if (g_color_rb) { glDeleteRenderbuffers(1, &g_color_rb); g_color_rb = 0; }
    if (g_depth_rb) { glDeleteRenderbuffers(1, &g_depth_rb); g_depth_rb = 0; }
    if (g_framebuffer) { glDeleteFramebuffers(1, &g_framebuffer); g_framebuffer = 0; }

    glGenFramebuffers(1, &g_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    glGenRenderbuffers(1, &g_color_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, g_color_rb);
    [g_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer*)g_view.layer];
    GLint w = 0, h = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    g_width = (int)w;
    g_height = (int)h;
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_color_rb);

    glGenRenderbuffers(1, &g_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g_width, g_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth_rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, g_color_rb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ios_log("EAGL framebuffer incomplete");
        return false;
    }
    g_resized = true;
    return g_width > 0 && g_height > 0;
}

void platform_ios_attach(UIView* view) {
    g_view = view;
}

bool platform_init(int width, int height, const char* title) {
    (void)width; (void)height; (void)title;
    if (!g_view) {
        ios_log("platform_ios_attach before platform_init");
        return false;
    }
    CAEAGLLayer* layer = (CAEAGLLayer*)g_view.layer;
    layer.opaque = YES;
    layer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking: @NO,
        kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8
    };
    CGFloat scale = UIScreen.mainScreen.nativeScale;
    layer.contentsScale = scale;
    g_view.contentScaleFactor = scale;

    g_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!g_ctx || ![EAGLContext setCurrentContext:g_ctx]) {
        ios_log("OpenGL ES 3 context failed");
        return false;
    }
    if (!ios_make_buffers()) return false;

    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString* docs = paths.firstObject;
    snprintf(g_userdata_dir, sizeof(g_userdata_dir), "%s", docs.UTF8String);
    mkdir_p(g_userdata_dir);

    g_time_origin = CACurrentMediaTime();
    input_init();
    return true;
}

void platform_shutdown(void) {
    if (g_ctx) {
        [EAGLContext setCurrentContext:g_ctx];
        if (g_color_rb) glDeleteRenderbuffers(1, &g_color_rb);
        if (g_depth_rb) glDeleteRenderbuffers(1, &g_depth_rb);
        if (g_framebuffer) glDeleteFramebuffers(1, &g_framebuffer);
        g_color_rb = g_depth_rb = g_framebuffer = 0;
        [EAGLContext setCurrentContext:nil];
        g_ctx = nil;
    }
}

void platform_run_loop(frame_callback_fn callback) {
    // UIKit already owns the process run loop. ios_boot starts CADisplayLink.
    g_frame_cb = callback;
}

void platform_ios_tick(void) {
    if (g_close || !g_frame_cb) return;
    static double prev = 0.0;
    double now = ios_now();
    double dt = prev > 0.0 ? (now - prev) : (1.0 / 60.0);
    if (dt > 0.1) dt = 0.1;
    prev = now;
    g_frame_cb(dt);
    input_post_frame();
}

void platform_flush_frame(void) {
    if (!g_ctx) return;
    [EAGLContext setCurrentContext:g_ctx];
    glBindRenderbuffer(GL_RENDERBUFFER, g_color_rb);
    [g_ctx presentRenderbuffer:GL_RENDERBUFFER];
}

double platform_get_time(void) { return ios_now(); }
void platform_set_busy_redraw(void (*fn)(void)) { g_busy_redraw = fn; }
void platform_set_window_size(int width, int height) { (void)width; (void)height; }
bool platform_was_resized_by_user(void) {
    bool r = g_resized;
    g_resized = false;
    return r;
}
void platform_get_window_size(int* out_w, int* out_h) {
    if (out_w) *out_w = g_width;
    if (out_h) *out_h = g_height;
}

typedef struct LoadJob {
    char path[512];
    file_load_callback cb;
    void* user;
    uint8_t* data;
    size_t len;
    struct LoadJob* next;
} LoadJob;

static LoadJob* g_jobs = NULL;
static pthread_mutex_t g_jobs_mu = PTHREAD_MUTEX_INITIALIZER;

static void ios_push_job(LoadJob* job) {
    pthread_mutex_lock(&g_jobs_mu);
    job->next = g_jobs;
    g_jobs = job;
    pthread_mutex_unlock(&g_jobs_mu);
}

static uint8_t* ios_read_local(const char* path, size_t* out_len) {
    if (out_len) *out_len = 0;
    if (!path || !path[0]) return NULL;
    NSString* rel = [NSString stringWithUTF8String:path];
    NSString* bundled = [[NSBundle mainBundle] pathForResource:rel ofType:nil];
    if (!bundled) bundled = [[NSBundle mainBundle].resourcePath stringByAppendingPathComponent:rel];
    NSData* data = [NSData dataWithContentsOfFile:bundled];
    if (!data) data = [NSData dataWithContentsOfFile:rel];
    if (!data) return NULL;
    uint8_t* buf = (uint8_t*)malloc(data.length + 1);
    if (!buf) return NULL;
    memcpy(buf, data.bytes, data.length);
    buf[data.length] = 0;
    if (out_len) *out_len = data.length;
    return buf;
}

void platform_load_file(const char* path, file_load_callback cb, void* user) {
    if (!path || !cb) return;
    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:path]];
        LoadJob* job = (LoadJob*)calloc(1, sizeof(LoadJob));
        snprintf(job->path, sizeof(job->path), "%s", path);
        job->cb = cb;
        job->user = user;
        [[[NSURLSession sharedSession] dataTaskWithURL:url completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
            (void)resp;
            if (data && !err) {
                job->len = data.length;
                job->data = (uint8_t*)malloc(job->len + 1);
                if (job->data) {
                    memcpy(job->data, data.bytes, job->len);
                    job->data[job->len] = 0;
                }
            } else {
                snprintf(g_http_last_error, sizeof(g_http_last_error), "%s",
                         err.localizedDescription.UTF8String ?: "http failed");
            }
            ios_push_job(job);
        }] resume];
        return;
    }
    size_t len = 0;
    uint8_t* data = ios_read_local(path, &len);
    cb(path, data, len, user);
    free(data);
}

void platform_http_pump(void) {
    LoadJob* list = NULL;
    pthread_mutex_lock(&g_jobs_mu);
    list = g_jobs;
    g_jobs = NULL;
    pthread_mutex_unlock(&g_jobs_mu);
    while (list) {
        LoadJob* n = list->next;
        if (list->cb) list->cb(list->path, list->data, list->len, list->user);
        free(list->data);
        free(list);
        list = n;
    }
}

static uint8_t* ios_http_sync(const char* url, const char* post, size_t* out_len) {
    if (out_len) *out_len = 0;
    g_http_last_error[0] = 0;
    if (!url) return NULL;
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
    req.timeoutInterval = 20.0;
    if (post) {
        req.HTTPMethod = @"POST";
        req.HTTPBody = [NSData dataWithBytes:post length:strlen(post)];
        [req setValue:@"application/x-www-form-urlencoded" forHTTPHeaderField:@"Content-Type"];
    }
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    __block NSData* body = nil;
    __block NSError* err = nil;
    [[[NSURLSession sharedSession] dataTaskWithRequest:req completionHandler:^(NSData* data, NSURLResponse* resp, NSError* e) {
        (void)resp;
        body = data;
        err = e;
        dispatch_semaphore_signal(sem);
    }] resume];
    if (g_busy_redraw) {
        while (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 16 * NSEC_PER_MSEC))) {
            g_busy_redraw();
            platform_flush_frame();
        }
    } else {
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    }
    if (err || !body) {
        snprintf(g_http_last_error, sizeof(g_http_last_error), "%s",
                 err.localizedDescription.UTF8String ?: "http failed");
        return NULL;
    }
    uint8_t* buf = (uint8_t*)malloc(body.length + 1);
    if (!buf) return NULL;
    memcpy(buf, body.bytes, body.length);
    buf[body.length] = 0;
    if (out_len) *out_len = body.length;
    return buf;
}

uint8_t* platform_http_get(const char* url, size_t* out_len) { return ios_http_sync(url, NULL, out_len); }
uint8_t* platform_http_post(const char* url, const char* body, size_t* out_len) {
    return ios_http_sync(url, body ? body : "", out_len);
}

bool platform_userdata_path(const char* filename, char* out, size_t out_sz) {
    if (!filename || !out || out_sz < 2) return false;
    if (snprintf(out, out_sz, "%s/%s", g_userdata_dir[0] ? g_userdata_dir : ".", filename) >= (int)out_sz)
        return false;
    return true;
}

char* platform_read_text_file(const char* path, size_t* out_len) {
    return (char*)ios_read_local(path, out_len);
}

bool platform_open_url(const char* url) {
    if (!url) return false;
    NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
    if (!u) return false;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] openURL:u options:@{} completionHandler:nil];
    });
    return true;
}

void platform_clipboard_set(const char* utf8) {
    if (!utf8) utf8 = "";
    snprintf(g_clip, sizeof(g_clip), "%s", utf8);
    UIPasteboard.generalPasteboard.string = [NSString stringWithUTF8String:utf8];
}
const char* platform_clipboard_get(void) {
    NSString* s = UIPasteboard.generalPasteboard.string;
    snprintf(g_clip, sizeof(g_clip), "%s", s ? s.UTF8String : "");
    return g_clip;
}

void platform_set_cursor_captured(bool captured) { (void)captured; }
void platform_set_cursor_pointer(bool pointer) { (void)pointer; }
void platform_set_cursor_grab(int grab) { (void)grab; }
void platform_set_fullscreen(bool fullscreen) { (void)fullscreen; }
bool platform_is_fullscreen(void) { return true; }

void platform_show_soft_keyboard(bool show) { g_kb_visible = show; }
bool platform_has_hardware_keyboard(void) { return false; }
bool platform_is_chromebook(void) { return false; }
bool platform_prefers_touch_controls(void) { return true; }
int platform_get_ime_bottom_inset(void) { return g_kb_visible ? (int)(280.0 * UIScreen.mainScreen.nativeScale) : 0; }
bool platform_ime_visible(void) { return g_kb_visible; }

void platform_block_ui_events(bool block) {
    if (block) g_ui_block++;
    else if (g_ui_block > 0) g_ui_block--;
}
bool platform_ui_events_blocked(void) { return g_ui_block > 0; }
const char* platform_http_last_error(void) { return g_http_last_error; }
void platform_request_close(void) { g_close = true; }
int platform_get_fps_limit(void) { return g_fps_limit; }

void platform_ios_on_touch(float x, float y, int phase) {
    if (g_ui_block) return;
    float sx = x * (float)g_view.contentScaleFactor;
    float sy = y * (float)g_view.contentScaleFactor;
    input_set_mouse_pos(sx, sy);
    if (phase == 0) {
        g_touch_down = true;
        extern bool chat_handle_click(float x, float y);
        extern bool login_handle_mouse(int x, int y);
        if (chat_handle_click(sx, sy) || login_handle_mouse((int)sx, (int)sy))
            return;
        input_on_mousedown(0);
    } else if (phase == 2 && g_touch_down) {
        g_touch_down = false;
        extern bool chat_handle_mouseup(float x, float y);
        extern bool login_handle_mouseup(void);
        bool ui = chat_handle_mouseup(sx, sy);
        login_handle_mouseup();
        if (ui) return;
        input_on_mouseup(0);
    }
}

void platform_ios_on_resize(void) {
    ios_make_buffers();
}

#endif // PW_IOS
