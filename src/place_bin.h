/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: place_bin.h                                                                         |
|   Purpose: PWBF (binary places)                                                             |
\*-------------------------------------------------------------------------------------------*/

#ifndef PLACE_BIN_H
#define PLACE_BIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLACE_BIN_MAGIC    0x46425750u
#define PLACE_BIN_VERSION  1
#define PLACE_BIN_PART_REC 104

enum {
    PLACE_BIN_FLAG_COPY_ALLOW          = 1u << 0,
    PLACE_BIN_FLAG_PROJECTILE_UNSTUCK  = 1u << 1,
    PLACE_BIN_FLAG_DEBRIS_UNSTUCK      = 1u << 2,
    PLACE_BIN_FLAG_AUTO_TWEEN          = 1u << 3
};

typedef struct {
    uint8_t shape;
    uint8_t material;
    uint8_t anchored;
    uint8_t can_collide;
    float pos[3];
    float rot[3];
    float size[3];
    float color[3];
    float glow;
    float alpha;
    uint8_t surfaces[6];
    char name[32];
} PlaceBinPart;

typedef struct {
    char title[64];
    uint32_t flags;
    PlaceBinPart* parts;
    uint32_t part_count;
} PlaceBin;

int place_bin_encode(const PlaceBin* src, uint8_t** out, size_t* out_len);

int place_bin_decode(const uint8_t* data, size_t len, PlaceBin* dst);

void place_bin_free(PlaceBin* p);

#ifdef __cplusplus
}
#endif

#endif
