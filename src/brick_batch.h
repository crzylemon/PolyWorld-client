/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: brick_batch.h                                                                       |
|   Purpose: merge static boxes so we don't draw 8000 bricks                                  |
\*-------------------------------------------------------------------------------------------*/

#ifndef BRICK_BATCH_H
#define BRICK_BATCH_H

#include "scene.h"
#include "mesh_loader.h"
#include "texture.h"
#include <stdbool.h>

void brick_batch_init(void);
void brick_batch_shutdown(void);

void brick_batch_rebuild(Scene* scene);

void brick_batch_clear(Scene* scene);

void brick_batch_mark_dirty(void);
void brick_batch_update(Scene* scene);

bool brick_batch_active(void);
bool brick_batch_is_building(void);

void brick_batch_draw(TextureManager* textures, int u_model, int u_color, int u_glow,
                      int u_alpha, int u_has_texture, int u_shadow_id, int u_normal_map,
                      const float* view_proj_frustum_optional,
                      const float identity_model[16]);

#endif
