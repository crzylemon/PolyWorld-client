/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: gfx_benchmark.h                                                                     |
|   Purpose: stress scene so we can pick a graphics preset                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef GFX_BENCHMARK_H
#define GFX_BENCHMARK_H

#include "renderer.h"
#include "avatar_anim.h"
#include "math_types.h"
#include <stdint.h>

typedef struct {
    const AvatarAnim* anim;
    uint32_t tex_shirt;
    uint32_t tex_pants;
    uint32_t tex_head;
    Vec3 skin_color;
} GfxBenchmarkAssets;

void gfx_benchmark_render(Renderer* r, float time_sec, int sw, int sh,
                          const GfxBenchmarkAssets* assets);

void gfx_benchmark_shutdown(void);

#endif
