/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: physics.h                                                                           |
|   Purpose: Jolt physics world                                                               |
\*-------------------------------------------------------------------------------------------*/

#ifndef PHYSICS_H
#define PHYSICS_H

#include "math_types.h"
#include "constraint_type.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PHYSICS_BODIES 8192
#define MAX_CONTACTS_PER_PAIR 4

typedef uint32_t PhysicsBodyID;
#define PHYSICS_BODY_INVALID 0

typedef enum {
    BODY_STATIC,
    BODY_DYNAMIC,
    BODY_KINEMATIC
} BodyType;

typedef enum {
    COLLIDER_BOX,
    COLLIDER_SPHERE,
    COLLIDER_HULL,
    COLLIDER_CYLINDER
} ColliderType;

#define BODY_DESC_MAX_HULL_POINTS 128

typedef struct {
    BodyType type;
    ColliderType collider;
    Vec3 position;
    Vec3 half_extents;
    float radius;
    float mass;
    float restitution;
    float friction;
    Vec3 hull_points[BODY_DESC_MAX_HULL_POINTS];
    int hull_point_count;
    const Vec3* hull_points_ext;
} BodyDesc;

typedef struct PhysicsWorld PhysicsWorld;

PhysicsWorld* physics_create(Vec3 gravity);
void physics_destroy(PhysicsWorld* world);

PhysicsBodyID physics_create_body(PhysicsWorld* world, const BodyDesc* desc);
void physics_destroy_body(PhysicsWorld* world, PhysicsBodyID id);

void physics_step(PhysicsWorld* world, float dt);

Vec3 physics_get_position(const PhysicsWorld* world, PhysicsBodyID id);
Vec3 physics_get_rotation(const PhysicsWorld* world, PhysicsBodyID id);

Mat4 physics_get_transform_mat4(const PhysicsWorld* world, PhysicsBodyID id);
Vec3 physics_get_velocity(const PhysicsWorld* world, PhysicsBodyID id);
Vec3 physics_get_angular_velocity(const PhysicsWorld* world, PhysicsBodyID id);
float physics_get_mass(const PhysicsWorld* world, PhysicsBodyID id);
bool physics_same_rigid_body(const PhysicsWorld* world, PhysicsBodyID a, PhysicsBodyID b);
bool physics_is_on_ground(const PhysicsWorld* world, PhysicsBodyID id);

void physics_set_position(PhysicsWorld* world, PhysicsBodyID id, Vec3 pos);
void physics_set_velocity(PhysicsWorld* world, PhysicsBodyID id, Vec3 velocity);
void physics_make_dynamic(PhysicsWorld* world, PhysicsBodyID id, float restitution);
void physics_make_static(PhysicsWorld* world, PhysicsBodyID id);
void physics_set_angular_velocity(PhysicsWorld* world, PhysicsBodyID id, Vec3 angular);
void physics_activate(PhysicsWorld* world, PhysicsBodyID id);
void physics_apply_impulse(PhysicsWorld* world, PhysicsBodyID id, Vec3 impulse);
void physics_apply_force(PhysicsWorld* world, PhysicsBodyID id, Vec3 force);
void physics_set_never_disable(PhysicsWorld* world, PhysicsBodyID id);
void physics_lock_rotation(PhysicsWorld* world, PhysicsBodyID id);
void physics_unlock_rotation(PhysicsWorld* world, PhysicsBodyID id);
void physics_set_rotation_euler(PhysicsWorld* world, PhysicsBodyID id, Vec3 euler_deg);
void physics_set_rotation_mat4(PhysicsWorld* world, PhysicsBodyID id, Mat4 rot);
void physics_set_geom_bits(PhysicsWorld* world, PhysicsBodyID id, unsigned long category, unsigned long collide);

void physics_add_box_geom(PhysicsWorld* world, PhysicsBodyID id, Vec3 half_extents, Vec3 offset);

typedef struct {
    bool hit;
    Vec3 point;
    Vec3 normal;
    float distance;
    PhysicsBodyID body;
} RaycastHit;

RaycastHit physics_raycast(const PhysicsWorld* world, Vec3 origin, Vec3 direction, float max_dist);

typedef struct {
    bool active;
    ColliderType collider;
    Vec3 position;
    Vec3 half_extents;
    float radius;
    float transform[16];
} PhysicsBodyInfo;

uint32_t physics_get_body_count(const PhysicsWorld* world);
uint32_t physics_get_active_body_count(const PhysicsWorld* world);
PhysicsBodyInfo physics_get_body_info(const PhysicsWorld* world, PhysicsBodyID id);

bool physics_init(PhysicsWorld* world, Vec3 gravity);
void physics_shutdown(PhysicsWorld* world);
void physics_get_rotation_euler(const PhysicsWorld* world, PhysicsBodyID id, Vec3* euler_out);

int physics_get_contacts(PhysicsWorld* world, PhysicsBodyID id, Vec3* push_out);

void physics_disable_geom(PhysicsWorld* world, PhysicsBodyID id);
void physics_enable_geom(PhysicsWorld* world, PhysicsBodyID id);

void physics_add_cylinder_geom(PhysicsWorld* world, PhysicsBodyID id, float radius, float length, Vec3 offset);

void physics_weld_batch_begin(PhysicsWorld* world);
void physics_weld_batch_end(PhysicsWorld* world);

typedef uint32_t ConnectorID;
#define MAX_CONNECTORS 8192
typedef struct {
    uint8_t type;
    uint8_t point_set;
    Vec3 axis;
    Vec3 point;
    float limits_min, limits_max;
    float stiffness, damping;
    float motor;
    float torque;
} ConstraintDesc;
ConnectorID physics_create_connector(PhysicsWorld* world, PhysicsBodyID body_a, PhysicsBodyID body_b);
ConnectorID physics_create_constraint(PhysicsWorld* world, PhysicsBodyID body_a, PhysicsBodyID body_b,
                                     const ConstraintDesc* desc);
void physics_destroy_connector(PhysicsWorld* world, ConnectorID id);
bool physics_connector_is_active(const PhysicsWorld* world, ConnectorID id);
Vec3 physics_get_connector_position(const PhysicsWorld* world, ConnectorID id);
int physics_break_connectors_in_radius(PhysicsWorld* world, Vec3 center, float radius);
int physics_break_connectors_for_body(PhysicsWorld* world, PhysicsBodyID body);
void physics_clear_connectors(PhysicsWorld* world);

void physics_optimize_broadphase(PhysicsWorld* world);

#ifdef __cplusplus
}
#endif

#endif
