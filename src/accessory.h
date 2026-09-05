/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: accessory.h                                                                         |
|   Purpose: Accessories (Also used for Tools)                                                |
\*-------------------------------------------------------------------------------------------*/

#ifndef ACCESSORY_H
#define ACCESSORY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "mesh_loader.h"
#include "avatar_anim.h"
#include "math_types.h"

#define PW_MAX_EQUIPPED_ACCESSORIES 10
#define ACCESSORY_MAX_PARTS 6
#define ACCESSORY_MAX_GLOWS 8

typedef struct {
    GPUMesh mesh;
    int attach_part;
    bool valid;
} AccessoryPart;

typedef struct {
    int attach_part;
    Vec3 local_pos;
    float radius;
    float u, v;
    Vec3 color;
    bool has_uv;
    bool valid;
} AccessoryGlow;

typedef struct {
    AccessoryPart parts[ACCESSORY_MAX_PARTS];
    AccessoryGlow glows[ACCESSORY_MAX_GLOWS];
    uint32_t texture;
    int part_count;
    int glow_count;
    bool loaded;
    unsigned char* atlas_rgba;
    int atlas_w, atlas_h;
} Accessory;

bool accessory_load(Accessory* acc, const char* obj_data, size_t len);
void accessory_unload(Accessory* acc);
void accessory_set_atlas(Accessory* acc, const unsigned char* rgba, int w, int h);

#endif
