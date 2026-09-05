/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_openxr_quest.c                                                                   |
|   Purpose: Quest OpenXR: GLES, stereo swapchains, Touch                                     |
\*-------------------------------------------------------------------------------------------*/

#if defined(VR) && defined(PW_OPENXR) && defined(PW_QUEST)

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include "vr_openxr.h"
#include "vr_ik.h"
#include "platform_android.h"
#include "input.h"
#include "log.h"
#include "protocol.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>
#include <math.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PW_QLOG(...) __android_log_print(ANDROID_LOG_INFO, "PolyWorldXR", __VA_ARGS__)
#define PW_QERR(...) __android_log_print(ANDROID_LOG_ERROR, "PolyWorldXR", __VA_ARGS__)
#define XR_MAX_SWAP_IMAGES 8

typedef struct {
    XrSwapchain handle;
    uint32_t nimg;
    XrSwapchainImageOpenGLESKHR imgs[XR_MAX_SWAP_IMAGES];
    GLuint fbo[XR_MAX_SWAP_IMAGES];
    GLuint depth[XR_MAX_SWAP_IMAGES];
    uint32_t width, height;
} EyeChain;

typedef struct {
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSpace local;
    XrActionSet action_set;
    XrAction pose_l, pose_r;
    XrAction stick_l, stick_r;
    XrAction trigger_r;
    XrAction grip_l, grip_r;
    XrAction jump;
    XrAction menu;
    XrAction pause;
    XrSpace hand_l, hand_r;
    EyeChain eye[2];
    XrView view[2];
    XrFrameState frame;
    GLuint game_fbo, game_color, game_depth;
    uint32_t game_w, game_h;
    bool session_running;
    bool frame_open;
    bool floor_space;
    bool did_snap;
    bool want_recalib;
    bool want_pause;
    bool ui_cursor;
    bool snap_latched;
    int turn_mode;
    float seated_yaw;
    float turn_amount;
    float snap_x, snap_z;
    Vec3 origin_feet;
    PwVrPose last_pose;
    float last_yaw, last_pitch;
    Vec3 last_eye;
    float cursor_x, cursor_y;
    bool trigger_was_down;
    bool ik_debug;
    bool submit_have;
    XrPosef submit_pose;
    XrFovf submit_fov;
    int draw_eye;
    bool eye_blitted[2];
    Vec3 draw_pos;
    float draw_qx, draw_qy, draw_qz, draw_qw;
} Oxr;

static Oxr g_oxr;

static void quat_to_yp(XrQuaternionf q, float* yaw_deg, float* pitch_deg) {

    float x = q.x, y = q.y, z = q.z, w = q.w;
    Vec3 f = {
        -(2.0f * (x * z + w * y)),
        -(2.0f * (y * z - w * x)),
        -(1.0f - 2.0f * (x * x + y * y))
    };
    float len = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
    if (len > 1e-6f) {
        f.x /= len; f.y /= len; f.z /= len;
    }
    if (yaw_deg)
        *yaw_deg = atan2f(-f.x, -f.z) * (180.0f / (float)M_PI);
    if (pitch_deg) {
        float fy = f.y < -1.f ? -1.f : (f.y > 1.f ? 1.f : f.y);
        *pitch_deg = -asinf(fy) * (180.0f / (float)M_PI);
    }
}

static void yaw_xz(float* dx, float* dz) {
    float yaw = g_oxr.seated_yaw;
    if (yaw == 0.0f) return;
    float c = cosf(yaw), s = sinf(yaw);
    float x = *dx, z = *dz;
    *dx = x * c - z * s;
    *dz = x * s + z * c;
}

static void yaw_quat(float* qx, float* qy, float* qz, float* qw) {
    float yaw = g_oxr.seated_yaw;
    if (yaw == 0.0f) return;
    float hy = yaw * 0.5f;
    float yw = cosf(hy), yy = sinf(hy);
    float x = *qx, y = *qy, z = *qz, w = *qw;
    *qw = yw * w - yy * y;
    *qx = yw * x + yy * z;
    *qy = yw * y + yy * w;
    *qz = yw * z - yy * x;
}

static void tracker_from_pose(const XrPosef* xp, PwVrTracker* t, Vec3 origin) {
    float dx = xp->position.x - g_oxr.snap_x;
    float dz = xp->position.z - g_oxr.snap_z;
    yaw_xz(&dx, &dz);
    t->x = origin.x + pw_metres_to_studs(dx);
    t->y = origin.y + pw_metres_to_studs(xp->position.y);
    t->z = origin.z + pw_metres_to_studs(dz);
    float qx = xp->orientation.x, qy = xp->orientation.y;
    float qz = xp->orientation.z, qw = xp->orientation.w;
    yaw_quat(&qx, &qy, &qz, &qw);
    t->qx = qx;
    t->qy = qy;
    t->qz = qz;
    t->qw = qw;
}

static XrQuaternionf nlerp_quat(XrQuaternionf a, XrQuaternionf b) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
    }
    XrQuaternionf q = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-8f) {
        n = 1.0f / n;
        q.x *= n; q.y *= n; q.z *= n; q.w *= n;
    } else {
        q.x = 0.0f; q.y = 0.0f; q.z = 0.0f; q.w = 1.0f;
    }
    return q;
}

static void snap_xz_from_head(const XrPosef* hp) {
    g_oxr.snap_x = hp->position.x;
    g_oxr.snap_z = hp->position.z;
    g_oxr.did_snap = true;
}

void vr_openxr_snap_playspace(void) {
    XrPosef hp = g_oxr.view[0].pose;
    hp.position.x = 0.5f * (g_oxr.view[0].pose.position.x + g_oxr.view[1].pose.position.x);
    hp.position.z = 0.5f * (g_oxr.view[0].pose.position.z + g_oxr.view[1].pose.position.z);
    snap_xz_from_head(&hp);
}

bool vr_openxr_consume_recalibrate(void) {
    bool r = g_oxr.want_recalib;
    g_oxr.want_recalib = false;
    return r;
}

void vr_openxr_set_comfort(int turn_mode, bool ui_cursor) {
    if (turn_mode < 0) turn_mode = 0;
    if (turn_mode > 2) turn_mode = 2;
    g_oxr.turn_mode = turn_mode;
    g_oxr.ui_cursor = ui_cursor;
}

float vr_openxr_turn_amount(void) {
    return g_oxr.turn_amount;
}

bool vr_openxr_consume_pause(void) {
    bool r = g_oxr.want_pause;
    g_oxr.want_pause = false;
    return r;
}

static XrFovf union_fov(const XrFovf* a, const XrFovf* b) {
    XrFovf u = *a;
    if (b->angleLeft < u.angleLeft) u.angleLeft = b->angleLeft;
    if (b->angleRight > u.angleRight) u.angleRight = b->angleRight;
    if (b->angleUp > u.angleUp) u.angleUp = b->angleUp;
    if (b->angleDown < u.angleDown) u.angleDown = b->angleDown;
    return u;
}

static void cache_submit_from_views(uint32_t got) {
    if (got < 1) {
        g_oxr.submit_have = false;
        return;
    }
    XrPosef hp = g_oxr.view[0].pose;
    XrFovf fov = g_oxr.view[0].fov;
    if (got > 1) {
        hp.position.x = 0.5f * (g_oxr.view[0].pose.position.x + g_oxr.view[1].pose.position.x);
        hp.position.y = 0.5f * (g_oxr.view[0].pose.position.y + g_oxr.view[1].pose.position.y);
        hp.position.z = 0.5f * (g_oxr.view[0].pose.position.z + g_oxr.view[1].pose.position.z);
        fov = union_fov(&g_oxr.view[0].fov, &g_oxr.view[1].fov);
    }
    g_oxr.submit_pose = hp;
    g_oxr.submit_fov = fov;
    g_oxr.submit_have = true;
}

static bool fov_to_gl_proj(const XrFovf* f, float znear, float zfar, Mat4* out) {
    float tl = tanf(f->angleLeft);
    float tr = tanf(f->angleRight);
    float tu = tanf(f->angleUp);
    float td = tanf(f->angleDown);
    float w = tr - tl;
    float h = tu - td;
    if (w < 1e-4f || h < 1e-4f || zfar <= znear)
        return false;
    memset(out, 0, sizeof(*out));
    out->m[0] = 2.0f / w;
    out->m[5] = 2.0f / h;
    out->m[8] = (tr + tl) / w;
    out->m[9] = (tu + td) / h;
    out->m[10] = -(zfar + znear) / (zfar - znear);
    out->m[11] = -1.0f;
    out->m[14] = -(2.0f * zfar * znear) / (zfar - znear);
    return true;
}

bool vr_openxr_fill_projection(Mat4* out, float znear, float zfar) {
    if (!out)
        return false;
    int e = g_oxr.draw_eye;
    if (e < 0) e = 0;
    if (e > 1) e = 1;
    if (g_oxr.view[e].fov.angleRight != 0.0f || g_oxr.view[e].fov.angleLeft != 0.0f)
        return fov_to_gl_proj(&g_oxr.view[e].fov, znear, zfar, out);
    if (!g_oxr.submit_have)
        return false;
    return fov_to_gl_proj(&g_oxr.submit_fov, znear, zfar, out);
}

static Vec3 play_origin(void) {
    Vec3 origin = g_oxr.origin_feet;
    if (!g_oxr.floor_space)
        origin.y += pw_metres_to_studs(VR_IK_DEFAULT_HEIGHT_M);
    return origin;
}

int vr_openxr_draw_eyes(void) {
    return (g_oxr.session_running && g_oxr.game_fbo) ? 2 : 1;
}

void vr_openxr_select_eye(int eye) {
    if (eye < 0) eye = 0;
    if (eye > 1) eye = 1;
    g_oxr.draw_eye = eye;
    Vec3 origin = play_origin();

    if (g_oxr.last_pose.flags & PW_VR_FLAG_HEAD) {
        PwVrTracker mid = g_oxr.last_pose.head;
        float dx = g_oxr.view[1].pose.position.x - g_oxr.view[0].pose.position.x;
        float dy = g_oxr.view[1].pose.position.y - g_oxr.view[0].pose.position.y;
        float dz = g_oxr.view[1].pose.position.z - g_oxr.view[0].pose.position.z;
        yaw_xz(&dx, &dz);
        float s = (eye == 0) ? -0.5f : 0.5f;
        g_oxr.draw_pos.x = mid.x + pw_metres_to_studs(dx) * s;
        g_oxr.draw_pos.y = mid.y + pw_metres_to_studs(dy) * s;
        g_oxr.draw_pos.z = mid.z + pw_metres_to_studs(dz) * s;
        g_oxr.draw_qx = mid.qx;
        g_oxr.draw_qy = mid.qy;
        g_oxr.draw_qz = mid.qz;
        g_oxr.draw_qw = mid.qw;
        return;
    }
    PwVrTracker t;
    memset(&t, 0, sizeof(t));
    t.qw = 1.0f;
    tracker_from_pose(&g_oxr.view[eye].pose, &t, origin);
    g_oxr.draw_pos = (Vec3){ t.x, t.y, t.z };
    g_oxr.draw_qx = t.qx;
    g_oxr.draw_qy = t.qy;
    g_oxr.draw_qz = t.qz;
    g_oxr.draw_qw = t.qw;
}

bool vr_openxr_eye_camera(Vec3* pos, float* qx, float* qy, float* qz, float* qw) {
    if (!g_oxr.session_running) return false;
    if (pos) *pos = g_oxr.draw_pos;
    if (qx) *qx = g_oxr.draw_qx;
    if (qy) *qy = g_oxr.draw_qy;
    if (qz) *qz = g_oxr.draw_qz;
    if (qw) *qw = g_oxr.draw_qw;
    return true;
}

void vr_openxr_blit_eye(int eye) {
    if (!g_oxr.frame_open || !g_oxr.game_fbo) return;
    if (eye < 0) eye = 0;
    if (eye > 1) eye = 1;

    int dest = 1 - eye;
    EyeChain* ch = &g_oxr.eye[dest];
    if (!ch->handle) return;
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo acq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(xrAcquireSwapchainImage(ch->handle, &acq, &idx)) || idx >= ch->nimg)
        return;
    XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(ch->handle, &wait);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_oxr.game_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ch->fbo[idx]);
    glBlitFramebuffer(0, 0, (GLint)g_oxr.game_w, (GLint)g_oxr.game_h,
                      0, 0, (GLint)ch->width, (GLint)ch->height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, g_oxr.game_fbo);
    XrSwapchainImageReleaseInfo rel = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(ch->handle, &rel);
    g_oxr.eye_blitted[dest] = true;
}

static bool has_ext(const char* name) {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL)) || n == 0)
        return false;
    XrExtensionProperties* props = (XrExtensionProperties*)calloc(n, sizeof(*props));
    if (!props) return false;
    for (uint32_t i = 0; i < n; i++)
        props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    uint32_t got = n;
    bool ok = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, n, &got, props))) {
        for (uint32_t i = 0; i < got; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                ok = true;
                break;
            }
        }
    }
    free(props);
    return ok;
}

static void poll_session_events(void) {
    for (;;) {
        XrEventDataBuffer ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult r = xrPollEvent(g_oxr.instance, &ev);
        if (r == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(r)) break;
        if (ev.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        XrEventDataSessionStateChanged* sc = (XrEventDataSessionStateChanged*)&ev;
        if (sc->state == XR_SESSION_STATE_READY && g_oxr.session) {
            XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
            bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (XR_SUCCEEDED(xrBeginSession(g_oxr.session, &bi))) {
                g_oxr.session_running = true;
                PW_QLOG("session READY -> begun");
            }
        } else if (sc->state == XR_SESSION_STATE_STOPPING) {
            xrEndSession(g_oxr.session);
            g_oxr.session_running = false;
        } else if (sc->state == XR_SESSION_STATE_EXITING ||
                   sc->state == XR_SESSION_STATE_LOSS_PENDING) {
            g_oxr.session_running = false;
        }
    }
}

static bool make_game_fbo(uint32_t w, uint32_t h) {
    if (g_oxr.game_fbo && g_oxr.game_w == w && g_oxr.game_h == h)
        return true;
    if (g_oxr.game_fbo) {
        glDeleteFramebuffers(1, &g_oxr.game_fbo);
        glDeleteTextures(1, &g_oxr.game_color);
        glDeleteRenderbuffers(1, &g_oxr.game_depth);
        g_oxr.game_fbo = g_oxr.game_color = g_oxr.game_depth = 0;
    }
    glGenTextures(1, &g_oxr.game_color);
    glBindTexture(GL_TEXTURE_2D, g_oxr.game_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenRenderbuffers(1, &g_oxr.game_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_oxr.game_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)w, (GLsizei)h);

    glGenFramebuffers(1, &g_oxr.game_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_oxr.game_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_oxr.game_color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_oxr.game_depth);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        PW_QERR("game FBO incomplete 0x%x", (unsigned)st);
        return false;
    }
    g_oxr.game_w = w;
    g_oxr.game_h = h;
    g_oxr.cursor_x = (float)w * 0.5f;
    g_oxr.cursor_y = (float)h * 0.5f;
    PW_QLOG("game FBO %ux%u", w, h);
    {
        extern void resize_canvas(int width, int height);
        resize_canvas((int)w, (int)h);
    }
    return true;
}

static bool make_eye_chain(EyeChain* ch, uint32_t w, uint32_t h) {
    memset(ch, 0, sizeof(*ch));
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = (int64_t)GL_RGBA8;
    ci.sampleCount = 1;
    ci.width = w;
    ci.height = h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (XR_FAILED(xrCreateSwapchain(g_oxr.session, &ci, &ch->handle))) {
        PW_QERR("xrCreateSwapchain failed");
        return false;
    }
    ch->width = w;
    ch->height = h;
    if (XR_FAILED(xrEnumerateSwapchainImages(ch->handle, 0, &ch->nimg, NULL)) ||
        ch->nimg == 0 || ch->nimg > XR_MAX_SWAP_IMAGES) {
        PW_QERR("swapchain image count %u", ch->nimg);
        return false;
    }
    for (uint32_t i = 0; i < ch->nimg; i++) {
        ch->imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        ch->imgs[i].next = NULL;
    }
    if (XR_FAILED(xrEnumerateSwapchainImages(ch->handle, ch->nimg, &ch->nimg,
                                             (XrSwapchainImageBaseHeader*)ch->imgs)))
        return false;

    for (uint32_t i = 0; i < ch->nimg; i++) {
        glGenFramebuffers(1, &ch->fbo[i]);
        glGenRenderbuffers(1, &ch->depth[i]);
        glBindRenderbuffer(GL_RENDERBUFFER, ch->depth[i]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei)w, (GLsizei)h);
        glBindFramebuffer(GL_FRAMEBUFFER, ch->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               ch->imgs[i].image, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                  ch->depth[i]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            PW_QERR("eye FBO %u incomplete", i);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

static void destroy_eye_chain(EyeChain* ch) {
    for (uint32_t i = 0; i < ch->nimg; i++) {
        if (ch->fbo[i]) glDeleteFramebuffers(1, &ch->fbo[i]);
        if (ch->depth[i]) glDeleteRenderbuffers(1, &ch->depth[i]);
    }
    if (ch->handle) xrDestroySwapchain(ch->handle);
    memset(ch, 0, sizeof(*ch));
}

static bool setup_actions(void) {
    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strncpy(asci.actionSetName, "pw_quest", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    strncpy(asci.localizedActionSetName, "PolyWorld Quest", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_FAILED(xrCreateActionSet(g_oxr.instance, &asci, &g_oxr.action_set)))
        return false;

    XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
    aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strncpy(aci.actionName, "lhand", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Left hand", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.pose_l);
    strncpy(aci.actionName, "rhand", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Right hand", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.pose_r);

    aci.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strncpy(aci.actionName, "lstick", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Left stick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.stick_l);
    strncpy(aci.actionName, "rstick", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Right stick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.stick_r);

    aci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strncpy(aci.actionName, "rtrig", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Right trigger", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.trigger_r);

    strncpy(aci.actionName, "lgrip", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Left grip", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.grip_l);
    strncpy(aci.actionName, "rgrip", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Right grip", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.grip_r);

    aci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    strncpy(aci.actionName, "jump", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Jump", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.jump);

    strncpy(aci.actionName, "menu", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Menu", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.menu);

    strncpy(aci.actionName, "pause", XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(aci.localizedActionName, "Pause", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    xrCreateAction(g_oxr.action_set, &aci, &g_oxr.pause);

    XrPath profile = 0, p_lg = 0, p_rg = 0, p_ls = 0, p_rs = 0, p_rt = 0, p_a = 0, p_x = 0, p_menu = 0, p_y = 0, p_b = 0;
    XrPath p_lgr = 0, p_rgr = 0;
    xrStringToPath(g_oxr.instance, "/interaction_profiles/oculus/touch_controller", &profile);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/grip/pose", &p_lg);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/grip/pose", &p_rg);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/thumbstick", &p_ls);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/thumbstick", &p_rs);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/trigger/value", &p_rt);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/a/click", &p_a);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/x/click", &p_x);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/menu/click", &p_menu);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/y/click", &p_y);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/b/click", &p_b);
    xrStringToPath(g_oxr.instance, "/user/hand/left/input/squeeze/value", &p_lgr);
    xrStringToPath(g_oxr.instance, "/user/hand/right/input/squeeze/value", &p_rgr);

    XrActionSuggestedBinding binds[12];
    binds[0].action = g_oxr.pose_l; binds[0].binding = p_lg;
    binds[1].action = g_oxr.pose_r; binds[1].binding = p_rg;
    binds[2].action = g_oxr.stick_l; binds[2].binding = p_ls;
    binds[3].action = g_oxr.stick_r; binds[3].binding = p_rs;
    binds[4].action = g_oxr.trigger_r; binds[4].binding = p_rt;
    binds[5].action = g_oxr.jump; binds[5].binding = p_a;
    binds[6].action = g_oxr.jump; binds[6].binding = p_x;
    binds[7].action = g_oxr.menu; binds[7].binding = p_menu;
    binds[8].action = g_oxr.menu; binds[8].binding = p_y;
    binds[9].action = g_oxr.pause; binds[9].binding = p_b;
    binds[10].action = g_oxr.grip_l; binds[10].binding = p_lgr;
    binds[11].action = g_oxr.grip_r; binds[11].binding = p_rgr;
    XrInteractionProfileSuggestedBinding sug = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    sug.interactionProfile = profile;
    sug.countSuggestedBindings = 12;
    sug.suggestedBindings = binds;
    xrSuggestInteractionProfileBindings(g_oxr.instance, &sug);

    XrSessionActionSetsAttachInfo att = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    att.countActionSets = 1;
    att.actionSets = &g_oxr.action_set;
    xrAttachSessionActionSets(g_oxr.session, &att);

    XrActionSpaceCreateInfo asci_s = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
    asci_s.poseInActionSpace.orientation.w = 1.0f;
    asci_s.action = g_oxr.pose_l;
    xrCreateActionSpace(g_oxr.session, &asci_s, &g_oxr.hand_l);
    asci_s.action = g_oxr.pose_r;
    xrCreateActionSpace(g_oxr.session, &asci_s, &g_oxr.hand_r);
    return true;
}

#ifndef XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR
#define XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR ((XrReferenceSpaceType)1000426000)
#endif

static bool playspace_has(const XrReferenceSpaceType* types, uint32_t n, XrReferenceSpaceType t) {
    for (uint32_t i = 0; i < n; i++)
        if (types[i] == t) return true;
    return false;
}

static bool try_playspace(XrReferenceSpaceType t, bool floor) {
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.referenceSpaceType = t;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    if (XR_FAILED(xrCreateReferenceSpace(g_oxr.session, &rs, &g_oxr.local)))
        return false;
    g_oxr.floor_space = floor;
    PW_QLOG("playspace type=%d floor=%d", (int)t, (int)floor);
    return true;
}

static bool create_playspace(void) {
    XrReferenceSpaceType types[8];
    memset(types, 0, sizeof(types));
    uint32_t n = 0;
    bool listed = XR_SUCCEEDED(xrEnumerateReferenceSpaces(g_oxr.session, 8, &n, types)) && n > 0;
    if (n > 8) n = 8;
    if ((!listed || playspace_has(types, n, XR_REFERENCE_SPACE_TYPE_STAGE)) &&
        try_playspace(XR_REFERENCE_SPACE_TYPE_STAGE, true))
        return true;
    if ((!listed || playspace_has(types, n, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR)) &&
        try_playspace(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR, true))
        return true;
    return try_playspace(XR_REFERENCE_SPACE_TYPE_LOCAL, false);
}

bool vr_openxr_init(void) {
    memset(&g_oxr, 0, sizeof(g_oxr));
    g_oxr.last_pose.head.qw = g_oxr.last_pose.lhand.qw = g_oxr.last_pose.rhand.qw = 1.0f;

    struct android_app* app = platform_android_app();
    if (!app || !app->activity) {
        PW_QERR("no GameActivity");
        return false;
    }
    JavaVM* vm = app->activity->vm;
    jobject activity = app->activity->javaGameActivity;
    if (!vm || !activity) {
        PW_QERR("missing VM/activity");
        return false;
    }

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                          (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    if (!xrInitializeLoaderKHR) {
        PW_QERR("no xrInitializeLoaderKHR");
        return false;
    }
    XrLoaderInitInfoAndroidKHR loader = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    loader.applicationVM = vm;
    loader.applicationContext = activity;
    if (XR_FAILED(xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loader))) {
        PW_QERR("xrInitializeLoaderKHR failed");
        return false;
    }

    if (!has_ext(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) ||
        !has_ext(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
        PW_QERR("runtime missing Android/GLES extensions");
        return false;
    }

    const char* exts[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    XrInstanceCreateInfoAndroidKHR and_ci = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    and_ci.applicationVM = vm;
    and_ci.applicationActivity = activity;

    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    ci.next = &and_ci;
    strncpy(ci.applicationInfo.applicationName, "PolyWorld", XR_MAX_APPLICATION_NAME_SIZE - 1);
#ifdef XR_API_VERSION_1_0
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
#else
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
#endif
    ci.enabledExtensionCount = 2;
    ci.enabledExtensionNames = exts;

    XrResult r = xrCreateInstance(&ci, &g_oxr.instance);
    if (XR_FAILED(r)) {
        PW_QERR("xrCreateInstance failed %d", (int)r);
        return false;
    }

    XrSystemGetInfo si = { XR_TYPE_SYSTEM_GET_INFO };
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(xrGetSystem(g_oxr.instance, &si, &g_oxr.system))) {
        PW_QERR("xrGetSystem failed");
        xrDestroyInstance(g_oxr.instance);
        g_oxr.instance = 0;
        return false;
    }

    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = NULL;
    xrGetInstanceProcAddr(g_oxr.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                          (PFN_xrVoidFunction*)&xrGetOpenGLESGraphicsRequirementsKHR);
    if (xrGetOpenGLESGraphicsRequirementsKHR) {
        XrGraphicsRequirementsOpenGLESKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
        xrGetOpenGLESGraphicsRequirementsKHR(g_oxr.instance, g_oxr.system, &req);
    }

    XrGraphicsBindingOpenGLESAndroidKHR bind = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    bind.display = platform_android_egl_display();
    bind.config = platform_android_egl_config();
    bind.context = platform_android_egl_context();
    if (bind.display == EGL_NO_DISPLAY || !bind.config || bind.context == EGL_NO_CONTEXT) {
        PW_QERR("EGL not ready for OpenXR");
        xrDestroyInstance(g_oxr.instance);
        memset(&g_oxr, 0, sizeof(g_oxr));
        return false;
    }

    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &bind;
    sci.systemId = g_oxr.system;
    if (XR_FAILED(xrCreateSession(g_oxr.instance, &sci, &g_oxr.session))) {
        PW_QERR("xrCreateSession failed");
        xrDestroyInstance(g_oxr.instance);
        memset(&g_oxr, 0, sizeof(g_oxr));
        return false;
    }

    if (!create_playspace()) {
        PW_QERR("xrCreateReferenceSpace failed");
        vr_openxr_shutdown();
        return false;
    }

    uint32_t nviews = 0;
    xrEnumerateViewConfigurationViews(g_oxr.instance, g_oxr.system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nviews, NULL);
    if (nviews < 2) nviews = 2;
    XrViewConfigurationView cfg[2];
    cfg[0].type = cfg[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    cfg[0].next = cfg[1].next = NULL;
    uint32_t got = 0;
    xrEnumerateViewConfigurationViews(g_oxr.instance, g_oxr.system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &got, cfg);
    uint32_t ew = cfg[0].recommendedImageRectWidth;
    uint32_t eh = cfg[0].recommendedImageRectHeight;
    if (ew < 640) ew = 1440;
    if (eh < 640) eh = 1584;

    if (ew > 1440) ew = 1440;
    if (eh > 1584) eh = 1584;

    if (!make_eye_chain(&g_oxr.eye[0], ew, eh) || !make_eye_chain(&g_oxr.eye[1], ew, eh) ||
        !make_game_fbo(ew, eh)) {
        vr_openxr_shutdown();
        return false;
    }

    setup_actions();
    g_oxr.view[0].type = g_oxr.view[1].type = XR_TYPE_VIEW;
    PW_WARN("[VR] Quest OpenXR session created (%ux%u per eye). Left stick move, right stick turn, trigger click, A/X jump, B pause, Menu recalibrates.\n",
            ew, eh);
    g_oxr.turn_mode = 1;
    PW_QLOG("OpenXR session created %ux%u", ew, eh);
    return true;
}

void vr_openxr_shutdown(void) {
    if (g_oxr.session_running && g_oxr.session)
        xrRequestExitSession(g_oxr.session);
    destroy_eye_chain(&g_oxr.eye[0]);
    destroy_eye_chain(&g_oxr.eye[1]);
    if (g_oxr.game_fbo) glDeleteFramebuffers(1, &g_oxr.game_fbo);
    if (g_oxr.game_color) glDeleteTextures(1, &g_oxr.game_color);
    if (g_oxr.game_depth) glDeleteRenderbuffers(1, &g_oxr.game_depth);
    if (g_oxr.hand_l) xrDestroySpace(g_oxr.hand_l);
    if (g_oxr.hand_r) xrDestroySpace(g_oxr.hand_r);
    if (g_oxr.pose_l) xrDestroyAction(g_oxr.pose_l);
    if (g_oxr.pose_r) xrDestroyAction(g_oxr.pose_r);
    if (g_oxr.stick_l) xrDestroyAction(g_oxr.stick_l);
    if (g_oxr.stick_r) xrDestroyAction(g_oxr.stick_r);
    if (g_oxr.trigger_r) xrDestroyAction(g_oxr.trigger_r);
    if (g_oxr.grip_l) xrDestroyAction(g_oxr.grip_l);
    if (g_oxr.grip_r) xrDestroyAction(g_oxr.grip_r);
    if (g_oxr.jump) xrDestroyAction(g_oxr.jump);
    if (g_oxr.menu) xrDestroyAction(g_oxr.menu);
    if (g_oxr.pause) xrDestroyAction(g_oxr.pause);
    if (g_oxr.action_set) xrDestroyActionSet(g_oxr.action_set);
    if (g_oxr.local) xrDestroySpace(g_oxr.local);
    if (g_oxr.session) xrDestroySession(g_oxr.session);
    if (g_oxr.instance) xrDestroyInstance(g_oxr.instance);
    memset(&g_oxr, 0, sizeof(g_oxr));
}

static void locate_and_cache_pose(Vec3 origin_feet) {
    memset(&g_oxr.last_pose, 0, sizeof(g_oxr.last_pose));
    g_oxr.last_pose.head.qw = g_oxr.last_pose.lhand.qw = g_oxr.last_pose.rhand.qw = 1.0f;
    g_oxr.last_pose.flags = PW_VR_FLAG_ACTIVE;

    g_oxr.origin_feet = origin_feet;
    Vec3 origin = origin_feet;

    if (!g_oxr.floor_space)
        origin.y += pw_metres_to_studs(VR_IK_DEFAULT_HEIGHT_M);

    XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = g_oxr.frame.predictedDisplayTime;
    vli.space = g_oxr.local;
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    uint32_t got = 0;
    if (XR_SUCCEEDED(xrLocateViews(g_oxr.session, &vli, &vs, 2, &got, g_oxr.view)) &&
        got > 0 && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
        XrPosef hp = g_oxr.view[0].pose;
        if (got > 1) {
            hp.position.x = 0.5f * (g_oxr.view[0].pose.position.x + g_oxr.view[1].pose.position.x);
            hp.position.y = 0.5f * (g_oxr.view[0].pose.position.y + g_oxr.view[1].pose.position.y);
            hp.position.z = 0.5f * (g_oxr.view[0].pose.position.z + g_oxr.view[1].pose.position.z);

            hp.orientation = nlerp_quat(g_oxr.view[0].pose.orientation,
                                        g_oxr.view[1].pose.orientation);
        }
        if (!g_oxr.did_snap)
            snap_xz_from_head(&hp);
        cache_submit_from_views(got);
        tracker_from_pose(&hp, &g_oxr.last_pose.head, origin);
        g_oxr.last_pose.flags |= PW_VR_FLAG_HEAD;
        {
            XrQuaternionf oq = {
                g_oxr.last_pose.head.qx, g_oxr.last_pose.head.qy,
                g_oxr.last_pose.head.qz, g_oxr.last_pose.head.qw
            };
            quat_to_yp(oq, &g_oxr.last_yaw, &g_oxr.last_pitch);
        }
        g_oxr.last_eye.x = g_oxr.last_pose.head.x;
        g_oxr.last_eye.y = g_oxr.last_pose.head.y;
        g_oxr.last_eye.z = g_oxr.last_pose.head.z;
    }

    if (g_oxr.action_set) {
        XrActiveActionSet aas = { g_oxr.action_set, XR_NULL_PATH };
        XrActionsSyncInfo syn = { XR_TYPE_ACTIONS_SYNC_INFO };
        syn.countActiveActionSets = 1;
        syn.activeActionSets = &aas;
        xrSyncActions(g_oxr.session, &syn);

        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (g_oxr.hand_l &&
            XR_SUCCEEDED(xrLocateSpace(g_oxr.hand_l, g_oxr.local, g_oxr.frame.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            tracker_from_pose(&loc.pose, &g_oxr.last_pose.lhand, origin);
            g_oxr.last_pose.flags |= PW_VR_FLAG_LHAND;
        }
        loc = (XrSpaceLocation){ XR_TYPE_SPACE_LOCATION };
        if (g_oxr.hand_r &&
            XR_SUCCEEDED(xrLocateSpace(g_oxr.hand_r, g_oxr.local, g_oxr.frame.predictedDisplayTime, &loc)) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            tracker_from_pose(&loc.pose, &g_oxr.last_pose.rhand, origin);
            g_oxr.last_pose.flags |= PW_VR_FLAG_RHAND;
        }
    }
}

bool vr_openxr_begin_frame(void) {
    if (!g_oxr.session) return false;
    poll_session_events();
    if (!g_oxr.session_running) return false;

    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    g_oxr.frame = (XrFrameState){ XR_TYPE_FRAME_STATE };
    if (XR_FAILED(xrWaitFrame(g_oxr.session, &wi, &g_oxr.frame)))
        return false;
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    if (XR_FAILED(xrBeginFrame(g_oxr.session, &bi)))
        return false;
    g_oxr.frame_open = true;

    locate_and_cache_pose(g_oxr.origin_feet);
    g_oxr.draw_eye = 0;
    g_oxr.eye_blitted[0] = g_oxr.eye_blitted[1] = false;
    vr_openxr_select_eye(0);

    if (g_oxr.game_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_oxr.game_fbo);
        glViewport(0, 0, (GLsizei)g_oxr.game_w, (GLsizei)g_oxr.game_h);
    }
    vr_openxr_apply_input();
    return true;
}

bool vr_openxr_submit_frame(void) {
    if (!g_oxr.frame_open) return false;
    g_oxr.frame_open = false;

    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    ei.displayTime = g_oxr.frame.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    if (!g_oxr.frame.shouldRender) {
        xrEndFrame(g_oxr.session, &ei);
        return true;
    }

    XrCompositionLayerProjectionView pv[2];
    memset(pv, 0, sizeof(pv));
    XrSwapchainImageReleaseInfo rel = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    bool stereo = g_oxr.eye_blitted[0] && g_oxr.eye_blitted[1];

    for (int e = 0; e < 2; e++) {
        EyeChain* ch = &g_oxr.eye[e];
        uint32_t idx = 0;
        if (!stereo) {
            XrSwapchainImageAcquireInfo acq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if (XR_FAILED(xrAcquireSwapchainImage(ch->handle, &acq, &idx)) || idx >= ch->nimg)
                continue;
            XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            wait.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(ch->handle, &wait);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_oxr.game_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ch->fbo[idx]);
            glBlitFramebuffer(0, 0, (GLint)g_oxr.game_w, (GLint)g_oxr.game_h,
                              0, 0, (GLint)ch->width, (GLint)ch->height,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, g_oxr.game_fbo);
            xrReleaseSwapchainImage(ch->handle, &rel);
        }

        pv[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        if (stereo) {
            pv[e].pose = g_oxr.view[e].pose;
            pv[e].fov = g_oxr.view[e].fov;
        } else if (g_oxr.submit_have) {

            pv[e].pose = g_oxr.submit_pose;
            pv[e].fov = g_oxr.submit_fov;
        } else {
            pv[e].pose = g_oxr.view[e].pose;
            pv[e].fov = g_oxr.view[e].fov;
        }
        pv[e].subImage.swapchain = ch->handle;
        pv[e].subImage.imageRect.offset.x = 0;
        pv[e].subImage.imageRect.offset.y = 0;
        pv[e].subImage.imageRect.extent.width = (int32_t)ch->width;
        pv[e].subImage.imageRect.extent.height = (int32_t)ch->height;
    }

    g_oxr.eye_blitted[0] = g_oxr.eye_blitted[1] = false;

    XrCompositionLayerProjection proj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    proj.space = g_oxr.local;
    proj.viewCount = 2;
    proj.views = pv;
    const XrCompositionLayerBaseHeader* layers[1] = { (const XrCompositionLayerBaseHeader*)&proj };
    ei.layerCount = 1;
    ei.layers = layers;
    xrEndFrame(g_oxr.session, &ei);
    return true;
}

void vr_openxr_apply_input(void) {
    if (!g_oxr.action_set || !g_oxr.session_running) return;

    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = g_oxr.stick_l;
    XrActionStateVector2f stick = { XR_TYPE_ACTION_STATE_VECTOR2F };
    if (XR_SUCCEEDED(xrGetActionStateVector2f(g_oxr.session, &gi, &stick)) && stick.isActive) {
        input_set_move_axes(stick.currentState.x, -stick.currentState.y);
    }

    gi.action = g_oxr.stick_r;
    XrActionStateVector2f look = { XR_TYPE_ACTION_STATE_VECTOR2F };
    if (XR_SUCCEEDED(xrGetActionStateVector2f(g_oxr.session, &gi, &look)) && look.isActive) {
        float x = look.currentState.x;
        float y = look.currentState.y;
        int mode = g_oxr.ui_cursor ? 0 : g_oxr.turn_mode;
        if (mode == 0) {
            float w = g_oxr.game_w > 0 ? (float)g_oxr.game_w : 1280.f;
            float h = g_oxr.game_h > 0 ? (float)g_oxr.game_h : 720.f;
            g_oxr.cursor_x += x * 28.0f;
            g_oxr.cursor_y -= y * 28.0f;
            if (g_oxr.cursor_x < 0) g_oxr.cursor_x = 0;
            if (g_oxr.cursor_y < 0) g_oxr.cursor_y = 0;
            if (g_oxr.cursor_x > w - 1) g_oxr.cursor_x = w - 1;
            if (g_oxr.cursor_y > h - 1) g_oxr.cursor_y = h - 1;
            input_set_mouse_pos(g_oxr.cursor_x, g_oxr.cursor_y);
            g_oxr.turn_amount *= 0.82f;
        } else {
            float dt = 1.0f / 72.0f;
            if (g_oxr.frame.predictedDisplayPeriod > 0)
                dt = (float)g_oxr.frame.predictedDisplayPeriod * 1e-9f;
            if (dt < 0.004f) dt = 0.004f;
            if (dt > 0.05f) dt = 0.05f;
            if (mode == 1) {
                const float snap = 45.0f * (float)M_PI / 180.0f;
                if (!g_oxr.snap_latched && x > 0.65f) {
                    g_oxr.seated_yaw += snap;
                    g_oxr.snap_latched = true;
                    g_oxr.turn_amount = 1.0f;
                } else if (!g_oxr.snap_latched && x < -0.65f) {
                    g_oxr.seated_yaw -= snap;
                    g_oxr.snap_latched = true;
                    g_oxr.turn_amount = 1.0f;
                } else {
                    g_oxr.turn_amount *= 0.82f;
                }
                if (fabsf(x) < 0.35f)
                    g_oxr.snap_latched = false;
            } else {
                if (fabsf(x) > 0.18f) {
                    g_oxr.seated_yaw += x * 2.1f * dt;
                    g_oxr.turn_amount = fabsf(x);
                } else {
                    g_oxr.turn_amount *= 0.82f;
                }
            }
            while (g_oxr.seated_yaw > (float)M_PI) g_oxr.seated_yaw -= 2.0f * (float)M_PI;
            while (g_oxr.seated_yaw < -(float)M_PI) g_oxr.seated_yaw += 2.0f * (float)M_PI;
        }
    }

    gi.action = g_oxr.trigger_r;
    XrActionStateFloat trig = { XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_SUCCEEDED(xrGetActionStateFloat(g_oxr.session, &gi, &trig)) && trig.isActive) {
        bool down = trig.currentState > 0.7f;
        if (down && !g_oxr.trigger_was_down)
            input_on_mousedown(0);
        if (!down && g_oxr.trigger_was_down)
            input_on_mouseup(0);
        g_oxr.trigger_was_down = down;
    }

    g_oxr.ik_debug = false;
    gi.action = g_oxr.grip_l;
    XrActionStateFloat grip = { XR_TYPE_ACTION_STATE_FLOAT };
    if (g_oxr.grip_l && XR_SUCCEEDED(xrGetActionStateFloat(g_oxr.session, &gi, &grip)) &&
        grip.isActive && grip.currentState > 0.6f)
        g_oxr.ik_debug = true;
    gi.action = g_oxr.grip_r;
    if (g_oxr.grip_r && XR_SUCCEEDED(xrGetActionStateFloat(g_oxr.session, &gi, &grip)) &&
        grip.isActive && grip.currentState > 0.6f)
        g_oxr.ik_debug = true;

    gi.action = g_oxr.jump;
    XrActionStateBoolean jmp = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (XR_SUCCEEDED(xrGetActionStateBoolean(g_oxr.session, &gi, &jmp)) && jmp.isActive) {
        if (jmp.currentState && jmp.changedSinceLastSync)
            input_on_keydown(32);
        if (!jmp.currentState && jmp.changedSinceLastSync)
            input_on_keyup(32);
    }

    gi.action = g_oxr.menu;
    XrActionStateBoolean men = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (g_oxr.menu && XR_SUCCEEDED(xrGetActionStateBoolean(g_oxr.session, &gi, &men)) &&
        men.isActive && men.currentState && men.changedSinceLastSync)
        g_oxr.want_recalib = true;

    gi.action = g_oxr.pause;
    XrActionStateBoolean pau = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (g_oxr.pause && XR_SUCCEEDED(xrGetActionStateBoolean(g_oxr.session, &gi, &pau)) &&
        pau.isActive && pau.currentState && pau.changedSinceLastSync)
        g_oxr.want_pause = true;
}

bool vr_openxr_poll(PwVrPose* pose, float* yaw_deg, float* pitch_deg,
                    Vec3* eye_studs, Vec3 origin_feet) {
    if (!g_oxr.session_running || !pose) return false;
    locate_and_cache_pose(origin_feet);
    *pose = g_oxr.last_pose;
    if (yaw_deg) *yaw_deg = g_oxr.last_yaw;
    if (pitch_deg) *pitch_deg = g_oxr.last_pitch;
    if (eye_studs) *eye_studs = g_oxr.last_eye;
    return (pose->flags & PW_VR_FLAG_HEAD) != 0;
}

int vr_openxr_game_width(void) { return (int)g_oxr.game_w; }
int vr_openxr_game_height(void) { return (int)g_oxr.game_h; }
unsigned vr_openxr_game_fbo(void) { return g_oxr.game_fbo; }

void vr_openxr_set_yaw_offset(float deg) {
    float yaw = deg * ((float)M_PI / 180.0f);
    while (yaw > (float)M_PI) yaw -= 2.0f * (float)M_PI;
    while (yaw < -(float)M_PI) yaw += 2.0f * (float)M_PI;
    g_oxr.seated_yaw = yaw;
}

bool vr_openxr_ik_debug(void) {
    return g_oxr.ik_debug;
}

#endif

#if !defined(PW_QUEST) || !defined(VR) || !defined(PW_OPENXR)
typedef int pw_vr_openxr_quest_tu;
#endif
