/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: texture.h                                                                           |
|   Purpose: textures                                                                         |
\*-------------------------------------------------------------------------------------------*/

#ifndef TEXTURE_H
#define TEXTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "part_material.h"

typedef uint32_t TextureID;
#define TEXTURE_INVALID 0

typedef struct {
    TextureID tex_stud;
    TextureID tex_inlet;
    TextureID tex_stud_normal;
    TextureID tex_inlet_normal;
    TextureID tex_mat_albedo[PART_MATERIAL_COUNT];
    TextureID tex_mat_normal[PART_MATERIAL_COUNT];
    TextureID tex_mat_specular[PART_MATERIAL_COUNT];
    TextureID tex_black;
    bool initialized;
} TextureManager;

bool texture_manager_init(TextureManager* tm);
void texture_manager_shutdown(TextureManager* tm);

TextureID texture_get_for_surface(const TextureManager* tm, int surface_type);
TextureID texture_get_normal_for_surface(const TextureManager* tm, int surface_type);
TextureID texture_get_mat_albedo(const TextureManager* tm, int material_id);
TextureID texture_get_mat_normal(const TextureManager* tm, int material_id);
TextureID texture_get_mat_specular(const TextureManager* tm, int material_id);

TextureID texture_load_from_memory(const uint8_t* data, int width, int height, int channels);

TextureID texture_load_atlas_from_memory(const uint8_t* data, int width, int height, int channels);
TextureID texture_load_png(const char* path);

#endif
