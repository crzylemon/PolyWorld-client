/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar_anim.h                                                                       |
|   Purpose: R6 part anims                                                                    |
\*-------------------------------------------------------------------------------------------*/

#ifndef AVATAR_ANIM_H
#define AVATAR_ANIM_H

#include "mesh_loader.h"
#include "math_types.h"
#include <stdbool.h>
#include <stdint.h>

#define AVATAR_PART_COUNT 6

#define AVATAR_SCALE 0.5f

#define AVATAR_PREVIEW_SCALE 0.476f

typedef enum {
    ANIM_STATE_IDLE = 0,
    ANIM_STATE_WALKING = 1,
    ANIM_STATE_JUMPING = 2,
    ANIM_STATE_DANCING = 3,
    ANIM_STATE_DANCING2 = 4,
    ANIM_STATE_DANCING3 = 5,
    ANIM_STATE_DEAD = 6,
    ANIM_STATE_CLIMBING = 7,
    ANIM_STATE_EMOTE = 8,
} AnimState;

struct EmoteClip;

typedef enum {
    ANIM_PART_HEAD = 0,
    ANIM_PART_TORSO,
    ANIM_PART_RIGHT_ARM,
    ANIM_PART_LEFT_ARM,
    ANIM_PART_RIGHT_LEG,
    ANIM_PART_LEFT_LEG,
} AvatarPartIndex;

typedef struct {
    GPUMesh mesh;
    Vec3 pivot;
    bool valid;
} AvatarPart;

typedef struct {
    AvatarPart parts[AVATAR_PART_COUNT];
    float walk_phase;
    float dance_phase;
    float climb_phase;
    AnimState state;
    bool tool_hold;
    bool vr_ik;

    uint32_t emote_id;
    float emote_time;
    const struct EmoteClip* emote_clip;

    Vec3 rot[AVATAR_PART_COUNT];
    Vec3 pos[AVATAR_PART_COUNT];
} AvatarAnim;

bool avatar_anim_load(AvatarAnim* anim, const char* obj_data, size_t len);

void avatar_anim_clear(AvatarAnim* anim);

void avatar_anim_detach(AvatarAnim* anim);

bool avatar_anim_compose(AvatarAnim* out,
                         const char* legacy_data, size_t legacy_len,
                         const char* new_data, size_t new_len,
                         int mesh_flags);

void avatar_anim_apply_mesh_flags(AvatarAnim* dst,
                                  const AvatarAnim* legacy_body,
                                  const AvatarAnim* new_body,
                                  int mesh_flags);

void avatar_anim_update(AvatarAnim* anim, AnimState state, float speed, float dt);

Mat4 avatar_anim_get_part_matrix(const AvatarAnim* anim, int part_index,
                                  Vec3 position, float yaw, float scale);

Vec3 avatar_anim_get_part_center(int part_index);

#endif
