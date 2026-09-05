/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_openxr.h                                                                         |
|   Purpose: OpenXR if we have it (Monado / Quest)                                            |
\*-------------------------------------------------------------------------------------------*/

#ifndef VR_OPENXR_H
#define VR_OPENXR_H

#include "protocol.h"
#include "math_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(VR) && defined(PW_OPENXR)
bool vr_openxr_init(void);
void vr_openxr_shutdown(void);

bool vr_openxr_poll(PwVrPose* pose, float* yaw_deg, float* pitch_deg,
                    Vec3* eye_studs, Vec3 origin_feet);

void vr_openxr_snap_playspace(void);
bool vr_openxr_consume_recalibrate(void);

void vr_openxr_set_comfort(int turn_mode, bool ui_cursor);
float vr_openxr_turn_amount(void);
bool vr_openxr_consume_pause(void);
#ifdef PW_QUEST

bool vr_openxr_begin_frame(void);

bool vr_openxr_submit_frame(void);

void vr_openxr_apply_input(void);
int vr_openxr_game_width(void);
int vr_openxr_game_height(void);
unsigned vr_openxr_game_fbo(void);
void vr_openxr_set_yaw_offset(float deg_y);

bool vr_openxr_fill_projection(Mat4* out, float znear, float zfar);
int vr_openxr_draw_eyes(void);
void vr_openxr_select_eye(int eye);
void vr_openxr_blit_eye(int eye);
bool vr_openxr_eye_camera(Vec3* pos, float* qx, float* qy, float* qz, float* qw);
bool vr_openxr_ik_debug(void);
#else
static inline bool vr_openxr_begin_frame(void) { return false; }
static inline bool vr_openxr_submit_frame(void) { return false; }
static inline void vr_openxr_apply_input(void) {}
static inline int vr_openxr_game_width(void) { return 0; }
static inline int vr_openxr_game_height(void) { return 0; }
static inline unsigned vr_openxr_game_fbo(void) { return 0; }
static inline void vr_openxr_set_yaw_offset(float deg_y) { (void)deg_y; }
static inline bool vr_openxr_fill_projection(Mat4* out, float znear, float zfar) {
    (void)out; (void)znear; (void)zfar;
    return false;
}
static inline int vr_openxr_draw_eyes(void) { return 1; }
static inline void vr_openxr_select_eye(int eye) { (void)eye; }
static inline void vr_openxr_blit_eye(int eye) { (void)eye; }
static inline bool vr_openxr_eye_camera(Vec3* pos, float* qx, float* qy, float* qz, float* qw) {
    (void)pos; (void)qx; (void)qy; (void)qz; (void)qw;
    return false;
}
static inline bool vr_openxr_ik_debug(void) { return false; }
#endif
#else
static inline bool vr_openxr_init(void) { return false; }
static inline void vr_openxr_shutdown(void) {}
static inline bool vr_openxr_poll(PwVrPose* pose, float* yaw_deg, float* pitch_deg,
                                  Vec3* eye_studs, Vec3 origin_feet) {
    (void)pose; (void)yaw_deg; (void)pitch_deg; (void)eye_studs; (void)origin_feet;
    return false;
}
static inline void vr_openxr_snap_playspace(void) {}
static inline bool vr_openxr_consume_recalibrate(void) { return false; }
static inline void vr_openxr_set_comfort(int turn_mode, bool ui_cursor) {
    (void)turn_mode; (void)ui_cursor;
}
static inline float vr_openxr_turn_amount(void) { return 0.0f; }
static inline bool vr_openxr_consume_pause(void) { return false; }
static inline bool vr_openxr_begin_frame(void) { return false; }
static inline bool vr_openxr_submit_frame(void) { return false; }
static inline void vr_openxr_apply_input(void) {}
static inline int vr_openxr_game_width(void) { return 0; }
static inline int vr_openxr_game_height(void) { return 0; }
static inline unsigned vr_openxr_game_fbo(void) { return 0; }
static inline void vr_openxr_set_yaw_offset(float deg_y) { (void)deg_y; }
static inline bool vr_openxr_fill_projection(Mat4* out, float znear, float zfar) {
    (void)out; (void)znear; (void)zfar;
    return false;
}
static inline int vr_openxr_draw_eyes(void) { return 1; }
static inline void vr_openxr_select_eye(int eye) { (void)eye; }
static inline void vr_openxr_blit_eye(int eye) { (void)eye; }
static inline bool vr_openxr_eye_camera(Vec3* pos, float* qx, float* qy, float* qz, float* qw) {
    (void)pos; (void)qx; (void)qy; (void)qz; (void)qw;
    return false;
}
static inline bool vr_openxr_ik_debug(void) { return false; }
#endif

#ifdef __cplusplus
}
#endif

#endif
