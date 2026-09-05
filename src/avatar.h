/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: avatar.h                                                                            |
|   Purpose: avatar controller                                                                |
\*-------------------------------------------------------------------------------------------*/

#ifndef AVATAR_H
#define AVATAR_H

#include "scene.h"
#include "physics.h"
#include "input.h"
#include "camera.h"

typedef enum {
    AVATAR_MODE_PLAYER,
    AVATAR_MODE_PHYSICS
} AvatarMode;

typedef struct {
    EntityID entity;
    PhysicsBodyID body;
    float walk_speed;
    float jump_impulse;
    float gravity;
    bool scripted_movement;
    float current_yaw;
    AvatarMode mode;
    Vec3 pos;
    Vec3 vel;
    float move_intent_x, move_intent_z;
    Vec3 death_vel;
    bool on_ground;
    bool jump_buffered;
    float jump_buffer_time;
    float step_offset;

    int health;
    bool dead;
    bool shift_lock;

    bool climbing;
    Vec3 climb_normal;
    float climb_anchor_y;
    bool climb_anchor_locked;

    PhysicsBodyID ground_body;
    Vec3 ground_prev_pos;
    bool has_ground_platform;
    PhysicsBodyID walk_hit_body;

    bool freeze_locomotion;
} Avatar;

void avatar_init(Avatar* av, Scene* scene, PhysicsWorld* physics);
void avatar_update(Avatar* av, const InputState* input, const Camera* cam,
                   PhysicsWorld* physics, Scene* scene, float dt);
void avatar_toggle_mode(Avatar* av, PhysicsWorld* physics);
void avatar_set_health(Avatar* av, int amount);

#endif
