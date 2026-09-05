/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: math_types.c                                                                        |
|   Purpose: vecs and mats                                                                    |
\*-------------------------------------------------------------------------------------------*/

#include "math_types.h"
#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846f / 180.0f)

Vec3 vec3_add(Vec3 a, Vec3 b) {
    return (Vec3){ a.x + b.x, a.y + b.y, a.z + b.z };
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return (Vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3 vec3_scale(Vec3 v, float s) {
    return (Vec3){ v.x * s, v.y * s, v.z * s };
}

float vec3_length(Vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len < 1e-8f) {
        return (Vec3){ 0.0f, 0.0f, 0.0f };
    }
    float inv = 1.0f / len;
    return (Vec3){ v.x * inv, v.y * inv, v.z * inv };
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Mat4 mat4_identity(void) {
    Mat4 result = {{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    }};
    return result;
}

Mat4 mat4_translate(Vec3 t) {
    Mat4 result = mat4_identity();
    result.m[12] = t.x;
    result.m[13] = t.y;
    result.m[14] = t.z;
    return result;
}

Mat4 mat4_rotate_x(float degrees) {
    float rad = degrees * DEG_TO_RAD;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 result = mat4_identity();
    result.m[5]  =  c;
    result.m[6]  =  s;
    result.m[9]  = -s;
    result.m[10] =  c;
    return result;
}

Mat4 mat4_rotate_y(float degrees) {
    float rad = degrees * DEG_TO_RAD;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 result = mat4_identity();
    result.m[0]  =  c;
    result.m[2]  = -s;
    result.m[8]  =  s;
    result.m[10] =  c;
    return result;
}

Mat4 mat4_rotate_z(float degrees) {
    float rad = degrees * DEG_TO_RAD;
    float c = cosf(rad);
    float s = sinf(rad);
    Mat4 result = mat4_identity();
    result.m[0] =  c;
    result.m[1] =  s;
    result.m[4] = -s;
    result.m[5] =  c;
    return result;
}

Mat4 mat4_scale(Vec3 s) {
    Mat4 result = mat4_identity();
    result.m[0]  = s.x;
    result.m[5]  = s.y;
    result.m[10] = s.z;
    return result;
}

Mat4 mat4_multiply(Mat4 a, Mat4 b) {
    Mat4 result = {{ 0 }};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

Mat4 mat4_perspective(float fov_deg, float aspect, float near, float far) {
    float fov_rad = fov_deg * DEG_TO_RAD;
    float tan_half_fov = tanf(fov_rad * 0.5f);

    Mat4 result = {{ 0 }};
    result.m[0]  = 1.0f / (aspect * tan_half_fov);
    result.m[5]  = 1.0f / tan_half_fov;
    result.m[10] = -(far + near) / (far - near);
    result.m[11] = -1.0f;
    result.m[14] = -(2.0f * far * near) / (far - near);
    return result;
}

Mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    Mat4 result = {{ 0 }};
    result.m[0]  = 2.0f / (right - left);
    result.m[5]  = 2.0f / (top - bottom);
    result.m[10] = -2.0f / (far - near);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(far + near) / (far - near);
    result.m[15] = 1.0f;
    return result;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(target, eye));
    Vec3 r = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(r, f);

    Mat4 result = mat4_identity();

    result.m[0]  =  r.x;
    result.m[1]  =  u.x;
    result.m[2]  = -f.x;

    result.m[4]  =  r.y;
    result.m[5]  =  u.y;
    result.m[6]  = -f.y;

    result.m[8]  =  r.z;
    result.m[9]  =  u.z;
    result.m[10] = -f.z;

    result.m[12] = -vec3_dot(r, eye);
    result.m[13] = -vec3_dot(u, eye);
    result.m[14] =  vec3_dot(f, eye);

    return result;
}

Vec4 mat4_mul_vec4(Mat4 m, Vec4 v) {
    Vec4 r;
    r.x = m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z  + m.m[12]*v.w;
    r.y = m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z  + m.m[13]*v.w;
    r.z = m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14]*v.w;
    r.w = m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15]*v.w;
    return r;
}

Mat4 mat4_inverse(Mat4 m) {
    float* a = m.m;
    Mat4 r;
    float* inv = r.m;

    inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
    inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
    inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
    inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
    inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
    inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];

    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (fabsf(det) < 1e-10f) return mat4_identity();

    det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= det;
    return r;
}
