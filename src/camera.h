/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: camera.h                                                                            |
|   Purpose: third-person orbit cam                                                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef CAMERA_H
#define CAMERA_H

#include "math_types.h"
#include "input.h"

typedef struct {
    Vec3 target;
    float yaw;
    float pitch;
    float distance;
    float distance_goal;
    float distance_min;
    float distance_max;
    float orbit_speed;
    float zoom_speed;
} Camera;

void camera_init(Camera* cam);
void camera_update(Camera* cam, Vec3 avatar_pos, const InputState* input, bool shift_lock, float dt, bool keys_ok);
float camera_body_alpha(const Camera* cam);
Mat4 camera_get_view_matrix(const Camera* cam);
Mat4 camera_get_projection_matrix(const Camera* cam, float aspect, float fov, float near, float far);
Vec3 camera_get_forward_xz(const Camera* cam);

Mat4 camera_look_from_pose(Vec3 eye, float yaw_deg, float pitch_deg, float roll_deg);

Mat4 camera_look_from_quat(Vec3 eye, float qx, float qy, float qz, float qw);

#endif
