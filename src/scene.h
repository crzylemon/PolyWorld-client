/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: scene.h                                                                             |
|   Purpose: scene graph                                                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef SCENE_H
#define SCENE_H

#include "math_types.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_ENTITIES 8192

typedef uint32_t EntityID;
#define ENTITY_INVALID ((EntityID)0xFFFFFFFF)

typedef struct GPUMesh GPUMesh;

typedef struct {
    Vec3 position;
    Vec3 rotation;
    Vec3 scale;
} Transform;

typedef enum {
    SURFACE_SMOOTH = 0,
    SURFACE_STUD,
    SURFACE_INLET
} SurfaceType;

typedef struct {
    Vec3 color;
    SurfaceType surfaces[6];
    uint32_t texture_id;
    int texture_mode;
    float glow;
    float alpha;
    uint8_t part_material;
} Material;

typedef struct {
    EntityID id;
    Transform transform;
    Material material;
    GPUMesh* mesh;
    EntityID parent;
    uint32_t physics_body;
    bool new_object;
    bool active;
    bool static_batch;
    bool render_batched;
    bool use_phys_model;
    Mat4 phys_model;
} Entity;

typedef struct {
    Entity entities[MAX_ENTITIES];
    uint32_t count;
} Scene;

EntityID scene_create_entity(Scene* s);

void scene_destroy_entity(Scene* s, EntityID id);

Entity* scene_get_entity(Scene* s, EntityID id);

Mat4 scene_get_world_matrix(const Scene* s, EntityID id);

#endif
