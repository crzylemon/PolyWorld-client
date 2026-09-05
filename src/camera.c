/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: camera.c                                                                            |
|   Purpose: third-person orbit cam                                                           |
\*-------------------------------------------------------------------------------------------*/

#include "camera.h"
#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846f / 180.0f)

#define PITCH_MIN -89.0f
#define PITCH_MAX 89.0f

#define BODY_FADE_START 3.5f
#define BODY_FADE_END   0.55f

#define CAM_KEY_LEFT  37
#define CAM_KEY_UP    38
#define CAM_KEY_RIGHT 39
#define CAM_KEY_DOWN  40
#define CAM_KEY_I     73
#define CAM_KEY_O     79
#define CAM_KEY_ROT_DEG  120.0f
#define CAM_KEY_ZOOM_SPD 12.0f

static float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

void camera_init(Camera* cam) {
    cam->target = (Vec3){ 0.0f, 0.0f, 0.0f };
    cam->yaw = 0.0f;
    cam->pitch = 45.0f;
    cam->distance = 10.0f;
    cam->distance_goal = 10.0f;
    cam->distance_min = 0.0f;
    cam->distance_max = 20.0f;
    cam->orbit_speed = 0.3f;
    cam->zoom_speed = 1.0f;
}

void camera_update(Camera* cam, Vec3 avatar_pos, const InputState* input, bool shift_lock, float dt, bool keys_ok) {
    (void)shift_lock;
    cam->target = avatar_pos;

    if (input->mouse_right) {
        cam->yaw -= input->mouse_dx * cam->orbit_speed;
        cam->pitch += input->mouse_dy * cam->orbit_speed;
    }

    float dmin = cam->distance_min;
    float dmax = cam->distance_max;
    if (dmax < dmin) {
        float tmp = dmin;
        dmin = dmax;
        dmax = tmp;
    }
    if (dmin < 0.0f) dmin = 0.0f;
    if (dmax < dmin) dmax = dmin;

    float kdt = dt > 0.0f ? dt : 0.016f;
    if (keys_ok) {
        if (input_key_held(CAM_KEY_LEFT))  cam->yaw += CAM_KEY_ROT_DEG * kdt;
        if (input_key_held(CAM_KEY_RIGHT)) cam->yaw -= CAM_KEY_ROT_DEG * kdt;
        if (input_key_held(CAM_KEY_UP))    cam->pitch += CAM_KEY_ROT_DEG * kdt;
        if (input_key_held(CAM_KEY_DOWN))  cam->pitch -= CAM_KEY_ROT_DEG * kdt;
        if (input_key_held(CAM_KEY_I))     cam->distance_goal -= CAM_KEY_ZOOM_SPD * kdt;
        if (input_key_held(CAM_KEY_O))     cam->distance_goal += CAM_KEY_ZOOM_SPD * kdt;
    }

    if (input->scroll_delta != 0.0f) {
        cam->distance_goal += input->scroll_delta * cam->zoom_speed;
    }
    cam->distance_goal = clampf(cam->distance_goal, dmin, dmax);

    float t = 1.0f - expf(-12.0f * kdt);
    cam->distance += (cam->distance_goal - cam->distance) * t;

    cam->pitch = clampf(cam->pitch, PITCH_MIN, PITCH_MAX);
    cam->distance = clampf(cam->distance, dmin, dmax);
}

float camera_body_alpha(const Camera* cam) {
    float d = cam->distance;
    if (d >= BODY_FADE_START) return 1.0f;
    if (d <= BODY_FADE_END) return 0.0f;
    return (d - BODY_FADE_END) / (BODY_FADE_START - BODY_FADE_END);
}

Mat4 camera_get_view_matrix(const Camera* cam) {

    float yaw_rad = cam->yaw * DEG_TO_RAD;
    float pitch_rad = cam->pitch * DEG_TO_RAD;

    float cos_pitch = cosf(pitch_rad);
    float sin_pitch = sinf(pitch_rad);
    float cos_yaw = cosf(yaw_rad);
    float sin_yaw = sinf(yaw_rad);

    Vec3 offset = {
        cam->distance * cos_pitch * sin_yaw,
        cam->distance * sin_pitch,
        cam->distance * cos_pitch * cos_yaw
    };

    Vec3 eye = vec3_add(cam->target, offset);
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    return mat4_look_at(eye, cam->target, up);
}

Mat4 camera_get_projection_matrix(const Camera* cam, float aspect, float fov, float near, float far) {
    (void)cam;
    return mat4_perspective(fov, aspect, near, far);
}

Mat4 camera_look_from_pose(Vec3 eye, float yaw_deg, float pitch_deg, float roll_deg) {
    float yaw_rad = yaw_deg * DEG_TO_RAD;
    float pitch_rad = pitch_deg * DEG_TO_RAD;
    float cos_p = cosf(pitch_rad);
    float sin_p = sinf(pitch_rad);
    float cos_y = cosf(yaw_rad);
    float sin_y = sinf(yaw_rad);
    Vec3 forward = { -cos_p * sin_y, -sin_p, -cos_p * cos_y };
    Vec3 look_at = vec3_add(eye, forward);
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    if (fabsf(roll_deg) > 0.001f) {
        float roll_rad = roll_deg * DEG_TO_RAD;
        float c = cosf(roll_rad);
        float s = sinf(roll_rad);
        Vec3 f = vec3_normalize(forward);
        Vec3 fcu = vec3_cross(f, up);
        float fdu = vec3_dot(f, up);
        float oc = 1.0f - c;
        up = (Vec3){
            up.x * c + fcu.x * s + f.x * fdu * oc,
            up.y * c + fcu.y * s + f.y * fdu * oc,
            up.z * c + fcu.z * s + f.z * fdu * oc
        };
        up = vec3_normalize(up);
    }
    return mat4_look_at(eye, look_at, up);
}

static Vec3 camera_quat_rotate(float qx, float qy, float qz, float qw, Vec3 v) {
    Vec3 u = { qx, qy, qz };
    Vec3 uv = vec3_cross(u, v);
    Vec3 uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * qw);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

Mat4 camera_look_from_quat(Vec3 eye, float qx, float qy, float qz, float qw) {
    float n = qx * qx + qy * qy + qz * qz + qw * qw;
    if (n < 1e-8f) {
        qw = 1.0f;
        qx = qy = qz = 0.0f;
    } else {
        float s = 1.0f / sqrtf(n);
        qx *= s; qy *= s; qz *= s; qw *= s;
    }

    Vec3 r = camera_quat_rotate(qx, qy, qz, qw, (Vec3){ 1.0f, 0.0f, 0.0f });
    Vec3 u = camera_quat_rotate(qx, qy, qz, qw, (Vec3){ 0.0f, 1.0f, 0.0f });
    Vec3 f = camera_quat_rotate(qx, qy, qz, qw, (Vec3){ 0.0f, 0.0f, -1.0f });
    Mat4 result = mat4_identity();
    result.m[0] = r.x; result.m[4] = r.y; result.m[8]  = r.z;
    result.m[1] = u.x; result.m[5] = u.y; result.m[9]  = u.z;
    result.m[2] = -f.x; result.m[6] = -f.y; result.m[10] = -f.z;
    result.m[12] = -vec3_dot(r, eye);
    result.m[13] = -vec3_dot(u, eye);
    result.m[14] =  vec3_dot(f, eye);
    return result;
}

Vec3 camera_get_forward_xz(const Camera* cam) {

    float yaw_rad = cam->yaw * DEG_TO_RAD;
    Vec3 forward = {
        -sinf(yaw_rad),
        0.0f,
        -cosf(yaw_rad)
    };
    return forward;
}
