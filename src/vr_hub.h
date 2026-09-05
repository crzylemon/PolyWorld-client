/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: vr_hub.h                                                                            |
|   Purpose: VR lobby. Local copy of game 159, UI on the wall.                                |
\*-------------------------------------------------------------------------------------------*/

#ifndef VR_HUB_H
#define VR_HUB_H

#include "login_screen.h"
#include "math_types.h"
#include "protocol.h"
#include "renderer.h"
#include "scene.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(VR) && !defined(__EMSCRIPTEN__)

#define VR_HUB_PLACE_PATH "assets/places/vr_hub.xml"
#define VR_HUB_FB_W 1280
#define VR_HUB_FB_H 720

void vr_hub_shutdown(void);
void vr_hub_invalidate_gl(bool context_alive);
bool vr_hub_active(void);
void vr_hub_set_active(bool on);
void vr_hub_on_world_loaded(void);

bool vr_hub_laser(const PwVrPose* pose, const Scene* scene,
                  float* out_u, float* out_v,
                  Vec3* out_from, Vec3* out_to);

void vr_hub_render_ui(LoginScreen* ls);
void vr_hub_draw(Renderer* r, const Scene* scene,
                 const Mat4* view, const Mat4* projection,
                 Vec3 laser_from, Vec3 laser_to, bool hit, float u, float v);

#else

static inline void vr_hub_shutdown(void) {}
static inline void vr_hub_invalidate_gl(bool context_alive) { (void)context_alive; }
static inline bool vr_hub_active(void) { return false; }
static inline void vr_hub_set_active(bool on) { (void)on; }
static inline void vr_hub_on_world_loaded(void) {}

#endif

#ifdef __cplusplus
}
#endif

#endif
