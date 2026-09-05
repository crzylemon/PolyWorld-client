/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_ik.h                                                                             |
|   Purpose: R6 full-body IK for VR. It's... a lot.                                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef VR_IK_H
#define VR_IK_H

#include "avatar_anim.h"
#include "protocol.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VR_IK_CANONICAL_HEIGHT_STUDS 5.25f
#define VR_IK_DEFAULT_HEIGHT_M       1.70f

typedef struct {
    bool ready;
    float height_m;
    float arm_span_m;
    float height_studs;
    float arm_span_studs;

    float ik_scale;
    float arm_len_l_studs;
    float arm_len_r_studs;
} VrIkCalib;

void vr_ik_calib_reset(VrIkCalib* c);

void vr_ik_calib_defaults(VrIkCalib* c);

void vr_ik_calibrate(VrIkCalib* c, const PwVrPose* standing, Vec3 feet_world,
                     float mesh_yaw_deg, bool sample_arms);

void vr_ik_calib_from_pose(VrIkCalib* c, const PwVrPose* pose);

void vr_ik_apply(AvatarAnim* anim, const PwVrPose* pose, const VrIkCalib* calib,
                 Vec3 feet_world, float mesh_yaw_deg);

#ifdef __cplusplus
}
#endif

#endif
