/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: skybox.c                                                                            |
|   Purpose: skybox                                                                           |
\*-------------------------------------------------------------------------------------------*/

#include "skybox.h"
#include "shader.h"
#include "log.h"
#include "platform.h"
#include "pw_gles.h"
#include "math_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if PW_USE_GLES
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif

extern unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int);
extern void stbi_image_free(void*);
extern void stbi_set_flip_vertically_on_load(int);

#if !PW_USE_GLES
static const float skybox_vertices[] = {

    -1, -1, -1,   1, -1, -1,   1,  1, -1,
     1,  1, -1,  -1,  1, -1,  -1, -1, -1,

    -1, -1,  1,   1,  1,  1,   1, -1,  1,
    -1, -1,  1,  -1,  1,  1,   1,  1,  1,

    -1, -1, -1,  -1,  1,  1,  -1, -1,  1,
    -1, -1, -1,  -1,  1, -1,  -1,  1,  1,

     1, -1, -1,   1, -1,  1,   1,  1,  1,
     1,  1,  1,   1,  1, -1,   1, -1, -1,

    -1,  1, -1,   1,  1,  1,  -1,  1,  1,
    -1,  1, -1,   1,  1, -1,   1,  1,  1,

    -1, -1, -1,  -1, -1,  1,   1, -1,  1,
     1, -1,  1,   1, -1, -1,  -1, -1, -1,
};
#endif

#if PW_USE_GLES
static const float sky_fs_tri[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f,
};
#endif

static void extract_face(const uint8_t* src, int img_w, int face_size,
                         int grid_x, int grid_y, uint8_t* dst) {
    int start_x = grid_x * face_size;
    int start_y = grid_y * face_size;
    for (int row = 0; row < face_size; row++) {
        int src_offset = ((start_y + row) * img_w + start_x) * 4;
        int dst_offset = row * face_size * 4;
        memcpy(dst + dst_offset, src + src_offset, face_size * 4);
    }
}

static void on_sky_loaded(const char* path, const uint8_t* data, size_t len, void* user) {
    Skybox* sky = (Skybox*)user;
    (void)path;

    if (!data || len == 0) {
        return;
    }

    int w, h, channels;

    stbi_set_flip_vertically_on_load(0);
    uint8_t* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    if (!pixels) {
        return;
    }

    int face_size = w / 4;
    if (face_size < 1 || h / 3 != face_size) {
        stbi_image_free(pixels);
        return;
    }

    uint8_t* face_data = (uint8_t*)malloc((size_t)face_size * (size_t)face_size * 4);
    if (!face_data) { stbi_image_free(pixels); return; }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

    struct { GLenum target; int gx, gy; } faces[] = {
        { GL_TEXTURE_CUBE_MAP_POSITIVE_X, 2, 1 },
        { GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, 1 },
        { GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 1, 0 },
        { GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 1, 2 },
        { GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 1, 1 },
        { GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 3, 1 },
    };

    for (int i = 0; i < 6; i++) {
        extract_face(pixels, w, face_size, faces[i].gx, faces[i].gy, face_data);
        glTexImage2D(faces[i].target, 0, GL_RGBA8, face_size, face_size, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, face_data);

        if (faces[i].target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y) {
            double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
            int n = face_size * face_size;
            for (int p = 0; p < n; p++) {
                sum_r += face_data[p * 4 + 0];
                sum_g += face_data[p * 4 + 1];
                sum_b += face_data[p * 4 + 2];
            }
            if (n > 0) {
                sky->fog_r = (float)(sum_r / (double)n / 255.0);
                sky->fog_g = (float)(sum_g / (double)n / 255.0);
                sky->fog_b = (float)(sum_b / (double)n / 255.0);
                sky->fog_ready = true;
            }
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    free(face_data);
    stbi_image_free(pixels);

    sky->cubemap_tex = tex;
    sky->loaded = true;
}

void skybox_set_yaw(Skybox* sky, float yaw_deg) {
    if (!sky) return;
    sky->yaw_deg = yaw_deg;
}

bool skybox_init(Skybox* sky) {
    memset(sky, 0, sizeof(Skybox));
    sky->yaw_deg = 0.0f;

    Shader sky_sh;
    memset(&sky_sh, 0, sizeof(sky_sh));
    if (!shader_compile_asset(&sky_sh, "skybox")) return false;
    sky->shader_program = sky_sh.program;

#if PW_USE_GLES
    glGenVertexArrays(1, &sky->vao);
    glGenBuffers(1, &sky->vbo);
    glBindVertexArray(sky->vao);
    glBindBuffer(GL_ARRAY_BUFFER, sky->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sky_fs_tri), sky_fs_tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    sky->u_view = glGetUniformLocation(sky->shader_program, "u_inv_view_proj");
    sky->u_projection = -1;
#else
    glGenVertexArrays(1, &sky->vao);
    glGenBuffers(1, &sky->vbo);
    glBindVertexArray(sky->vao);
    glBindBuffer(GL_ARRAY_BUFFER, sky->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skybox_vertices), skybox_vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    sky->u_view = glGetUniformLocation(sky->shader_program, "u_view");
    sky->u_projection = glGetUniformLocation(sky->shader_program, "u_projection");
#endif

    glUseProgram(sky->shader_program);
    glUniform1i(glGetUniformLocation(sky->shader_program, "u_skybox"), 0);
    glUseProgram(0);

#ifdef __EMSCRIPTEN__
    platform_load_file("https://polyworld.games/assets/wasm/sky.png", on_sky_loaded, sky);
#else
    platform_load_file("assets/sky.png", on_sky_loaded, sky);
#endif

    return true;
}

void skybox_render(const Skybox* sky, const Mat4* view, const Mat4* projection) {
    if (!sky->loaded || !sky->cubemap_tex) return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(sky->shader_program);

    Mat4 yaw_m = mat4_rotate_y(sky->yaw_deg);

#if PW_USE_GLES
    Mat4 sky_view = *view;
    sky_view.m[12] = 0.0f;
    sky_view.m[13] = 0.0f;
    sky_view.m[14] = 0.0f;
    Mat4 vp = mat4_multiply(*projection, sky_view);
    Mat4 inv_vp = mat4_inverse(vp);
    if (sky->u_view >= 0)
        glUniformMatrix4fv(sky->u_view, 1, GL_FALSE, inv_vp.m);

    Mat4 yaw_inv = mat4_rotate_y(-sky->yaw_deg);
    float yaw3[9] = {
        yaw_inv.m[0], yaw_inv.m[1], yaw_inv.m[2],
        yaw_inv.m[4], yaw_inv.m[5], yaw_inv.m[6],
        yaw_inv.m[8], yaw_inv.m[9], yaw_inv.m[10]
    };
    int u_yaw = glGetUniformLocation(sky->shader_program, "u_sky_yaw");
    if (u_yaw >= 0)
        glUniformMatrix3fv(u_yaw, 1, GL_FALSE, yaw3);

    glBindVertexArray(sky->vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky->cubemap_tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
#else
    Mat4 sky_view = *view;
    sky_view.m[12] = 0.0f;
    sky_view.m[13] = 0.0f;
    sky_view.m[14] = 0.0f;

    sky_view = mat4_multiply(sky_view, yaw_m);

    glUniformMatrix4fv(sky->u_view, 1, GL_FALSE, sky_view.m);
    glUniformMatrix4fv(sky->u_projection, 1, GL_FALSE, projection->m);

    glBindVertexArray(sky->vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky->cubemap_tex);
    glDrawArrays(GL_TRIANGLES, 0, 36);
#endif

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
}

void skybox_shutdown(Skybox* sky) {
    if (sky->vao) glDeleteVertexArrays(1, &sky->vao);
    if (sky->vbo) glDeleteBuffers(1, &sky->vbo);
    if (sky->cubemap_tex) glDeleteTextures(1, &sky->cubemap_tex);
    if (sky->shader_program) glDeleteProgram(sky->shader_program);
    memset(sky, 0, sizeof(Skybox));
}

bool skybox_get_fog_color(const Skybox* sky, float* r, float* g, float* b) {
    if (!sky || !sky->fog_ready) return false;
    if (r) *r = sky->fog_r;
    if (g) *g = sky->fog_g;
    if (b) *b = sky->fog_b;
    return true;
}
