/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: skybox.h                                                                            |
|   Purpose: skybox                                                                           |
\*-------------------------------------------------------------------------------------------*/

#ifndef SKYBOX_H
#define SKYBOX_H

#include "math_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned int vao;
    unsigned int vbo;
    unsigned int cubemap_tex;
    unsigned int shader_program;
    int u_view;
    int u_projection;
    bool loaded;

    bool fog_ready;
    float fog_r, fog_g, fog_b;

    float yaw_deg;
} Skybox;

bool skybox_init(Skybox* sky);

void skybox_set_yaw(Skybox* sky, float yaw_deg);

void skybox_render(const Skybox* sky, const Mat4* view, const Mat4* projection);
bool skybox_get_fog_color(const Skybox* sky, float* r, float* g, float* b);
void skybox_shutdown(Skybox* sky);

#endif
