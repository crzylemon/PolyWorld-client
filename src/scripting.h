/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: scripting.h                                                                         |
|   Purpose: client Lua (LocalScripts)                                                        |
\*-------------------------------------------------------------------------------------------*/

#ifndef SCRIPTING_H
#define SCRIPTING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "scene.h"
#include "avatar.h"

typedef struct ClientScriptEngine ClientScriptEngine;

ClientScriptEngine* client_script_create(Scene* scene);
void client_script_destroy(ClientScriptEngine* engine);

void client_script_set_player(ClientScriptEngine* engine, Avatar* avatar);

void client_script_set_local_name(ClientScriptEngine* engine, const char* name);

void client_script_set_playtest(ClientScriptEngine* engine, bool playtest);

uint32_t client_script_load(ClientScriptEngine* engine, EntityID parent_entity, const char* source);

void client_script_tick(ClientScriptEngine* engine, float dt);

void client_script_fire_touched(ClientScriptEngine* engine, EntityID entity);

void client_script_fire_clicked(ClientScriptEngine* engine, EntityID entity);

bool client_script_part_has_clicked(ClientScriptEngine* engine, EntityID entity);

void client_script_send_remote(const char* name, const uint8_t* data, size_t data_len);

void client_script_ui_notify(const char* text, float seconds);
void client_script_ui_hud(const char* text);

int client_script_world_raycast(float ox, float oy, float oz,
                                float dx, float dy, float dz, float max_dist,
                                float* hx, float* hy, float* hz,
                                float* nx, float* ny, float* nz,
                                float* dist, EntityID* entity);
void client_script_screen_to_world(float sx, float sy, float depth,
                                   float* ox, float* oy, float* oz,
                                   float* dx, float* dy, float* dz);
void client_script_point_to_screen(float wx, float wy, float wz,
                                   float* sx, float* sy, int* on_screen);

void client_script_part_meta(EntityID entity, const char** shape_out, int* can_collide_out);

EntityID client_script_spawn_local_part(void);
void client_script_destroy_local_part(EntityID entity);
int client_script_is_local_part(EntityID entity);
const char* client_script_local_part_name(EntityID entity);
void client_script_local_part_set_name(EntityID entity, const char* name);
void client_script_local_part_set_collide(EntityID entity, int on);
void client_script_local_part_set_shape(EntityID entity, uint8_t obj_type);
void client_script_local_part_sync_pose(EntityID entity);
void client_script_local_part_rebuild(EntityID entity);

void client_script_part_commit_transform(EntityID entity);
void client_script_part_commit_size(EntityID entity);
int client_script_local_part_aabb_ray(float ox, float oy, float oz,
                                      float dx, float dy, float dz, float max_dist,
                                      float* t_hit, EntityID* entity);

void client_script_apply_avatar(ClientScriptEngine* engine, Avatar* avatar, Camera* camera);
void client_script_set_camera_live(ClientScriptEngine* engine,
                                   float x, float y, float z,
                                   float yaw, float pitch, float roll, float fov);
int client_script_camera_override(ClientScriptEngine* engine,
                                  float* x, float* y, float* z,
                                  float* yaw, float* pitch, float* roll, float* fov);
float client_script_camera_fov(ClientScriptEngine* engine);

void client_script_fire_message_from_server(ClientScriptEngine* engine, const char* name,
                                            const uint8_t* data, size_t data_len);

#endif
