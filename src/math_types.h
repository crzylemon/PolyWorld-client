/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: math_types.h                                                                        |
|   Purpose: vecs and mats                                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z, w; } Vec4;
typedef struct { float m[16]; } Mat4;

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
Vec3 vec3_normalize(Vec3 v);
float vec3_dot(Vec3 a, Vec3 b);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);

Mat4 mat4_identity(void);
Mat4 mat4_translate(Vec3 t);
Mat4 mat4_rotate_x(float degrees);
Mat4 mat4_rotate_y(float degrees);
Mat4 mat4_rotate_z(float degrees);
Mat4 mat4_scale(Vec3 s);
Mat4 mat4_multiply(Mat4 a, Mat4 b);
Mat4 mat4_perspective(float fov_deg, float aspect, float near, float far);
Mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);

Mat4 mat4_inverse(Mat4 m);
Vec4 mat4_mul_vec4(Mat4 m, Vec4 v);

#ifdef __cplusplus
}
#endif

#endif
