/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: part_material.c                                                                     |
|   Purpose: part material names/ids                                                          |
\*-------------------------------------------------------------------------------------------*/

#include "part_material.h"
#include <string.h>
#include <ctype.h>

static const char* k_names[PART_MATERIAL_COUNT] = {
    "Plastic", "Grass", "Dirt", "Rock", "Sand", "Wood", "Metal"
};

const char* part_material_name(uint8_t id) {
    if (id >= PART_MATERIAL_COUNT) return k_names[0];
    return k_names[id];
}

uint8_t part_material_from_name(const char* s) {
    if (!s || !s[0]) return PART_MATERIAL_PLASTIC;
    char buf[16];
    size_t n = 0;
    for (; s[n] && n + 1 < sizeof(buf); n++) {
        char c = s[n];
        if (c == '<' || c == '/' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            break;
        buf[n] = (char)tolower((unsigned char)c);
    }
    buf[n] = '\0';
    if (strcmp(buf, "grass") == 0) return PART_MATERIAL_GRASS;
    if (strcmp(buf, "dirt") == 0) return PART_MATERIAL_DIRT;
    if (strcmp(buf, "rock") == 0 || strcmp(buf, "slate") == 0 || strcmp(buf, "cobblestone") == 0)
        return PART_MATERIAL_ROCK;
    if (strcmp(buf, "sand") == 0) return PART_MATERIAL_SAND;
    if (strcmp(buf, "wood") == 0 || strcmp(buf, "woodplanks") == 0) return PART_MATERIAL_WOOD;
    if (strcmp(buf, "metal") == 0 || strcmp(buf, "steel") == 0 || strcmp(buf, "iron") == 0)
        return PART_MATERIAL_METAL;
    return PART_MATERIAL_PLASTIC;
}

static const char* k_mesh_col_label[MESH_COLLIDER_COUNT] = {
    "Cube", "Sphere", "Low Precision", "Medium Precision", "High Precision"
};
static const char* k_mesh_col_xml[MESH_COLLIDER_COUNT] = {
    "cube", "sphere", "low", "medium", "high"
};

const char* mesh_collider_label(uint8_t id) {
    if (id >= MESH_COLLIDER_COUNT) return k_mesh_col_label[0];
    return k_mesh_col_label[id];
}

const char* mesh_collider_xml(uint8_t id) {
    if (id >= MESH_COLLIDER_COUNT) return k_mesh_col_xml[0];
    return k_mesh_col_xml[id];
}

uint8_t mesh_collider_from_name(const char* s) {
    if (!s || !s[0]) return MESH_COLLIDER_CUBE;
    char buf[32];
    size_t n = 0;
    for (size_t i = 0; s[i] && n + 1 < sizeof(buf); i++) {
        char c = s[i];
        if (c == '<' || c == '/' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            break;
        if (c == '-' || c == '_') continue;
        buf[n++] = (char)tolower((unsigned char)c);
    }
    buf[n] = '\0';
    if (strcmp(buf, "sphere") == 0) return MESH_COLLIDER_SPHERE;
    if (strcmp(buf, "low") == 0 || strcmp(buf, "lowprecision") == 0)
        return MESH_COLLIDER_LOW;
    if (strcmp(buf, "medium") == 0 || strcmp(buf, "med") == 0 ||
        strcmp(buf, "mediumprecision") == 0)
        return MESH_COLLIDER_MED;
    if (strcmp(buf, "high") == 0 || strcmp(buf, "highprecision") == 0)
        return MESH_COLLIDER_HIGH;
    return MESH_COLLIDER_CUBE;
}
