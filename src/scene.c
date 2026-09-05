/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: scene.c                                                                             |
|   Purpose: scene graph                                                                      |
\*-------------------------------------------------------------------------------------------*/

#include "scene.h"
#include <stddef.h>
#include <string.h>

static Mat4 compute_local_matrix(const Transform* t) {
    Mat4 s = mat4_scale(t->scale);
    Mat4 rx = mat4_rotate_x(t->rotation.x);
    Mat4 ry = mat4_rotate_y(t->rotation.y);
    Mat4 rz = mat4_rotate_z(t->rotation.z);
    Mat4 tr = mat4_translate(t->position);

    Mat4 result = mat4_multiply(rx, s);
    result = mat4_multiply(ry, result);
    result = mat4_multiply(rz, result);
    result = mat4_multiply(tr, result);
    return result;
}

EntityID scene_create_entity(Scene* s) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        if (!s->entities[i].active) {
            Entity* e = &s->entities[i];
            e->id = i;
            e->active = true;
            e->transform.position = (Vec3){ 0.0f, 0.0f, 0.0f };
            e->transform.rotation = (Vec3){ 0.0f, 0.0f, 0.0f };
            e->transform.scale = (Vec3){ 1.0f, 1.0f, 1.0f };
            e->material.color = (Vec3){ 1.0f, 1.0f, 1.0f };
            e->material.texture_id = 0;
            e->material.texture_mode = 0;
            e->material.glow = 0.0f;
            e->material.alpha = 1.0f;
            e->material.part_material = 0;
            memset(e->material.surfaces, 0, sizeof(e->material.surfaces));
            e->mesh = NULL;
            e->parent = ENTITY_INVALID;
            e->physics_body = 0;
            e->new_object = false;
            e->static_batch = false;
            e->render_batched = false;
            e->use_phys_model = false;
            e->phys_model = mat4_identity();
            s->count++;
            return i;
        }
    }
    return ENTITY_INVALID;
}

void scene_destroy_entity(Scene* s, EntityID id) {
    if (id >= MAX_ENTITIES) return;
    if (!s->entities[id].active) return;
    s->entities[id].active = false;

}

Entity* scene_get_entity(Scene* s, EntityID id) {
    if (id >= MAX_ENTITIES) return NULL;
    if (!s->entities[id].active) return NULL;
    return &s->entities[id];
}

Mat4 scene_get_world_matrix(const Scene* s, EntityID id) {
    if (id >= MAX_ENTITIES || !s->entities[id].active) {
        return mat4_identity();
    }

    Mat4 local = s->entities[id].use_phys_model
        ? s->entities[id].phys_model
        : compute_local_matrix(&s->entities[id].transform);

    EntityID parent = s->entities[id].parent;
    if (parent == ENTITY_INVALID) {
        return local;
    }

    Mat4 parent_world = scene_get_world_matrix(s, parent);
    return mat4_multiply(parent_world, local);
}
