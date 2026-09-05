/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: part_material.h                                                                     |
|   Purpose: part material names/ids                                                          |
\*-------------------------------------------------------------------------------------------*/

#ifndef PART_MATERIAL_H
#define PART_MATERIAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PART_MATERIAL_PLASTIC = 0,
    PART_MATERIAL_GRASS   = 1,
    PART_MATERIAL_DIRT    = 2,
    PART_MATERIAL_ROCK    = 3,
    PART_MATERIAL_SAND    = 4,
    PART_MATERIAL_WOOD    = 5,
    PART_MATERIAL_METAL   = 6,
    PART_MATERIAL_COUNT   = 7
};

const char* part_material_name(uint8_t id);
uint8_t part_material_from_name(const char* s);

enum {
    MESH_COLLIDER_CUBE   = 0,
    MESH_COLLIDER_SPHERE = 1,
    MESH_COLLIDER_LOW    = 2,
    MESH_COLLIDER_MED    = 3,
    MESH_COLLIDER_HIGH   = 4,
    MESH_COLLIDER_COUNT  = 5
};

const char* mesh_collider_label(uint8_t id);
const char* mesh_collider_xml(uint8_t id);
uint8_t mesh_collider_from_name(const char* s);

static inline void pw_wire_unpack_type(uint8_t b, uint8_t* obj_type, uint8_t* material) {
    if (b >= 8u) {
        if (obj_type) *obj_type = (uint8_t)(b & 7u);
        if (material) *material = (uint8_t)(b >> 3);
    } else {
        if (obj_type) *obj_type = b;
        if (material) *material = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif
