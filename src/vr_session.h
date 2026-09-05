/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_session.h                                                                        |
|   Purpose: desktop VR (fake trackers, optional OpenXR)                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef VR_SESSION_H
#define VR_SESSION_H

#include "input.h"
#include "math_types.h"
#include "protocol.h"
#include "vr_ik.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VrSessionState {
    bool active;
    bool calib_done;
    bool openxr;
    PwVrPose local;
    VrIkCalib calib;
    float hmd_yaw;
    float hmd_pitch;
    Vec3 hmd_eye;

    bool inspect;

    bool recal_ui;
    float inspect_yaw;
    float inspect_pitch;
    float inspect_dist;
} VrSessionState;

#ifdef VR
void vr_session_init(VrSessionState* s);
void vr_session_shutdown(VrSessionState* s);
void vr_session_update(VrSessionState* s, float dt, Vec3 feet_world,
                       const InputState* in, bool ui_blocks);
void vr_session_recalibrate(VrSessionState* s, Vec3 feet_world, float mesh_yaw_deg);
#else
static inline void vr_session_init(VrSessionState* s) { (void)s; }
static inline void vr_session_shutdown(VrSessionState* s) { (void)s; }
static inline void vr_session_update(VrSessionState* s, float dt, Vec3 feet_world,
                                     const InputState* in, bool ui_blocks) {
    (void)s; (void)dt; (void)feet_world; (void)in; (void)ui_blocks;
}
static inline void vr_session_recalibrate(VrSessionState* s, Vec3 feet_world, float mesh_yaw_deg) {
    (void)s; (void)feet_world; (void)mesh_yaw_deg;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
