/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: viewer_brick_batch_stub.c                                                           |
|   Purpose: no-op brick_batch for the avatar-viewer wasm                                     |
\*-------------------------------------------------------------------------------------------*/

#include "brick_batch.h"

bool input_key_held(int keycode) { (void)keycode; return false; }

void brick_batch_init(void) {}
void brick_batch_shutdown(void) {}
void brick_batch_rebuild(Scene* scene) { (void)scene; }
void brick_batch_clear(Scene* scene) { (void)scene; }
void brick_batch_mark_dirty(void) {}
void brick_batch_update(Scene* scene) { (void)scene; }
bool brick_batch_active(void) { return false; }
bool brick_batch_is_building(void) { return false; }
void brick_batch_draw(TextureManager* textures, int u_model, int u_color, int u_glow,
                      int u_alpha, int u_has_texture, int u_shadow_id, int u_normal_map,
                      const float* view_proj_frustum_optional,
                      const float identity_model[16]) {
    (void)textures; (void)u_model; (void)u_color; (void)u_glow; (void)u_alpha;
    (void)u_has_texture; (void)u_shadow_id; (void)u_normal_map;
    (void)view_proj_frustum_optional; (void)identity_model;
}
