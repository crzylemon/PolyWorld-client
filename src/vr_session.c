/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_session.c                                                                        |
|   Purpose: desktop VR (fake trackers, optional OpenXR)                                      |
\*-------------------------------------------------------------------------------------------*/

#if !defined(VR)
typedef int pw_vr_session_tu;
#endif

#ifdef VR

#include "vr_session.h"
#include "vr_openxr.h"
#include "log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD ((float)M_PI / 180.0f)
#define KEY_Q 81
#define KEY_E 69

static Vec3 g_lhand_cam;
static Vec3 g_rhand_cam;
static bool g_hands_inited;
static bool g_look_inited;

static void euler_to_quat(float yaw_deg, float pitch_deg, PwVrTracker* t) {
    float hy = yaw_deg * DEG2RAD * 0.5f;
    float hp = pitch_deg * DEG2RAD * 0.5f;
    float cy = cosf(hy), sy = sinf(hy);
    float cp = cosf(hp), sp = sinf(hp);

    t->qx = sp * cy;
    t->qy = cp * sy;
    t->qz = -sp * sy;
    t->qw = cp * cy;
}

static Vec3 yaw_rotate(float yaw_deg, Vec3 local) {
    float r = yaw_deg * DEG2RAD;
    float c = cosf(r), s = sinf(r);
    return (Vec3){ local.x * c - local.z * s, local.y, local.x * s + local.z * c };
}

void vr_session_init(VrSessionState* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->local.head.qw = s->local.lhand.qw = s->local.rhand.qw = 1.0f;
    vr_ik_calib_defaults(&s->calib);
    s->calib_done = false;
    s->active = true;
#ifdef PW_OPENXR
    s->openxr = vr_openxr_init();
    if (s->openxr) {
#ifdef PW_QUEST
        PW_WARN("[VR] Using OpenXR on Quest. Left stick move, right stick turn, trigger click, A/X jump, B pause, Menu recalibrates.\n");
#else
        PW_WARN("[VR] Using OpenXR (Monado). Hold Q/E and move the mouse to pose left/right hand. F10 recalibrates.\n");
#endif
    } else
#endif
    {
        s->openxr = false;
#ifdef PW_QUEST
        PW_WARN("[VR] OpenXR failed. check logcat tag PolyWorldXR.\n");
#else
        PW_WARN("[VR] Fake VR: mouse look = headset. Hold Q/E and move the mouse to pose left/right hand. F10 recalibrates.\n");
        PW_WARN("[VR] For Monado: ./scripts/setup_monado.sh && ./scripts/run_vr.sh\n");
#endif
    }
}

void vr_session_shutdown(VrSessionState* s) {
#ifdef PW_OPENXR
    vr_openxr_shutdown();
#endif
    if (s)
        s->openxr = false;
}

void vr_session_recalibrate(VrSessionState* s, Vec3 feet_world, float mesh_yaw_deg) {
    if (!s) return;
#ifdef PW_OPENXR
    vr_openxr_snap_playspace();
#endif
    vr_ik_calibrate(&s->calib, &s->local, feet_world, mesh_yaw_deg, true);
    s->calib_done = true;
    PW_WARN("[VR] Calibrated: height=%.2fm (%.2f studs) arm L/R=%.2f/%.2f span=%.2fm\n",
           (double)s->calib.height_m, (double)s->calib.height_studs,
           (double)s->calib.arm_len_l_studs, (double)s->calib.arm_len_r_studs,
           (double)s->calib.arm_span_m);
}

static bool desktop_openxr_overlay(void) {
    const char* n = getenv("XRT_COMPOSITOR_NULL");
    return n && n[0] && n[0] != '0';
}

static bool posing_left(const InputState* in, bool ui_blocks) {
    return !ui_blocks && in && input_key_held(KEY_Q);
}
static bool posing_right(const InputState* in, bool ui_blocks) {
    return !ui_blocks && in && input_key_held(KEY_E);
}

static void apply_mouse_look(VrSessionState* s, const InputState* in, bool ui_blocks,
                             bool hmd_from_mouse) {
    if (!in || ui_blocks) return;
    bool pose_l = posing_left(in, ui_blocks);
    bool pose_r = posing_right(in, ui_blocks);
    float sens = 0.3f;

    if (!pose_l && !pose_r && hmd_from_mouse && !in->mouse_right) {
        s->hmd_yaw -= in->mouse_dx * sens;
        s->hmd_pitch += in->mouse_dy * sens;
    }
    if (s->hmd_pitch < -89.0f) s->hmd_pitch = -89.0f;
    if (s->hmd_pitch > 89.0f) s->hmd_pitch = 89.0f;
}

static void clamp_hand_cam(Vec3* h, bool left) {
    if (left) {
        if (h->x < -2.2f) h->x = -2.2f;
        if (h->x > 0.45f) h->x = 0.45f;
    } else {
        if (h->x > 2.2f) h->x = 2.2f;
        if (h->x < -0.45f) h->x = -0.45f;
    }
    if (h->y < -1.8f) h->y = -1.8f;
    if (h->y > 0.9f) h->y = 0.9f;

    if (h->z > -0.15f) h->z = -0.15f;
    if (h->z < -2.6f) h->z = -2.6f;
}

static void nudge_hand(Vec3* h, const InputState* in, bool left) {
    float s = 0.032f;
    h->x += in->mouse_dx * s;
    h->y -= in->mouse_dy * s;
    if (in->scroll_delta != 0.0f)
        h->z -= in->scroll_delta * 0.22f;
    clamp_hand_cam(h, left);
}

static void apply_fake_hands(VrSessionState* s, const InputState* in, bool ui_blocks) {
    if (!s->calib.ready)
        vr_ik_calib_defaults(&s->calib);

    if (!g_hands_inited) {
        g_lhand_cam = (Vec3){ -0.55f, -0.40f, -1.15f };
        g_rhand_cam = (Vec3){  0.55f, -0.40f, -1.15f };
        g_hands_inited = true;
    }

    if (posing_left(in, ui_blocks))
        nudge_hand(&g_lhand_cam, in, true);
    else if (posing_right(in, ui_blocks))
        nudge_hand(&g_rhand_cam, in, false);

    Vec3 lw = vec3_add(s->hmd_eye, yaw_rotate(s->hmd_yaw, g_lhand_cam));
    Vec3 rw = vec3_add(s->hmd_eye, yaw_rotate(s->hmd_yaw, g_rhand_cam));
    PwVrPose* p = &s->local;
    p->lhand.x = lw.x; p->lhand.y = lw.y; p->lhand.z = lw.z;
    p->rhand.x = rw.x; p->rhand.y = rw.y; p->rhand.z = rw.z;
    euler_to_quat(s->hmd_yaw, s->hmd_pitch, &p->lhand);
    euler_to_quat(s->hmd_yaw, s->hmd_pitch, &p->rhand);
    p->flags |= PW_VR_FLAG_LHAND | PW_VR_FLAG_RHAND;
}

static void stamp_calib_flags(VrSessionState* s) {
    if (!s->calib_done) return;
    s->local.flags |= PW_VR_FLAG_IK | PW_VR_FLAG_CALIB;
    s->local.height_m = s->calib.height_m;
    s->local.arm_span_m = s->calib.arm_span_m;
}

static void fake_update(VrSessionState* s, float dt, Vec3 feet, const InputState* in,
                        bool ui_blocks) {
    (void)dt;
    if (!g_look_inited) {
        s->hmd_yaw = 0.0f;
        s->hmd_pitch = 0.0f;
        g_look_inited = true;
    }

    apply_mouse_look(s, in, ui_blocks, true);

    if (!s->calib.ready)
        vr_ik_calib_defaults(&s->calib);

    float h = s->calib.height_studs;
    if (h < 3.0f) h = 6.07f;

    if (in && in->key_shift)
        h *= 0.58f;
    s->hmd_eye = (Vec3){ feet.x, feet.y + h, feet.z };

    PwVrPose* p = &s->local;
    memset(p, 0, sizeof(*p));
    p->head.qw = p->lhand.qw = p->rhand.qw = 1.0f;
    p->flags = PW_VR_FLAG_ACTIVE | PW_VR_FLAG_HEAD;
    p->head.x = s->hmd_eye.x;
    p->head.y = s->hmd_eye.y;
    p->head.z = s->hmd_eye.z;
    euler_to_quat(s->hmd_yaw, s->hmd_pitch, &p->head);
    apply_fake_hands(s, in, ui_blocks);
    stamp_calib_flags(s);
}

void vr_session_update(VrSessionState* s, float dt, Vec3 feet_world,
                       const InputState* in, bool ui_blocks) {
    if (!s || !s->active) return;

#ifdef PW_OPENXR
    if (s->openxr) {
        PwVrPose pose;
        float yaw = s->hmd_yaw, pitch = s->hmd_pitch;
        Vec3 eye = s->hmd_eye;
        if (vr_openxr_poll(&pose, &yaw, &pitch, &eye, feet_world)) {
            s->local = pose;
            s->hmd_eye = eye;
            bool overlay = desktop_openxr_overlay();
            bool have_hands = (pose.flags & PW_VR_FLAG_LHAND) && (pose.flags & PW_VR_FLAG_RHAND);
            if (overlay || !(pose.flags & PW_VR_FLAG_HEAD)) {

                s->hmd_eye.x = feet_world.x;
                s->hmd_eye.z = feet_world.z;
                s->local.head.x = s->hmd_eye.x;
                s->local.head.z = s->hmd_eye.z;
                if (!g_look_inited) {
                    s->hmd_yaw = yaw;
                    s->hmd_pitch = pitch;
                    g_look_inited = true;
                }
                apply_mouse_look(s, in, ui_blocks, true);
                euler_to_quat(s->hmd_yaw, s->hmd_pitch, &s->local.head);
                s->local.flags |= PW_VR_FLAG_HEAD;
            } else {
                s->hmd_yaw = yaw;
                s->hmd_pitch = pitch;
                if (!have_hands)
                    apply_mouse_look(s, in, ui_blocks, false);
            }
            if (!have_hands)
                apply_fake_hands(s, in, ui_blocks);
            stamp_calib_flags(s);
            return;
        }
#ifdef PW_QUEST

        return;
#endif

        PW_WARN("[VR] OpenXR poll failed, switching to fake trackers\n");
        s->openxr = false;
        vr_openxr_shutdown();
    }
#endif
    fake_update(s, dt, feet_world, in, ui_blocks);
}

#endif
