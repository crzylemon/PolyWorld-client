/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: scripting.c                                                                         |
|   Purpose: client Lua (LocalScripts)                                                        |
\*-------------------------------------------------------------------------------------------*/

#include "scripting.h"
#include "lua_pw_event.h"
#include "lua_pw_task.h"
#include "part_material.h"
#include "input.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef __EMSCRIPTEN__
#include "../libs/lua-5.4.7/src/lua.h"
#include "../libs/lua-5.4.7/src/lauxlib.h"
#include "../libs/lua-5.4.7/src/lualib.h"
#else
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#endif

#define MAX_CLIENT_SCRIPTS 64

static int client_env_newindex(lua_State* L) {
    if (lua_isnoneornil(L, 2))
        return luaL_error(L, "table index is nil");
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char* key = lua_tostring(L, 2);
        if (key && (
            strcmp(key, "script") == 0 ||
            strcmp(key, "Touched") == 0 ||
            strcmp(key, "Clicked") == 0 ||
            strcmp(key, "player") == 0 ||
            strcmp(key, "OnRemoteEvent") == 0
        )) {
            lua_pushvalue(L, 2);
            lua_pushvalue(L, 3);
            lua_rawset(L, 1);
            return 0;
        }
    }

    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, 1);
    return 0;
}

typedef struct {
    uint32_t id;
    EntityID parent_entity;
    lua_State* co;
    int co_ref;
    int env_ref;
    float wait_timer;
    float wait_duration;
    bool running;
    bool dead;
    bool is_touching;
    bool touched_this_frame;
} ClientScript;

struct ClientScriptEngine {
    lua_State* L;
    Scene* scene;
    Avatar* player;
    char player_name[32];
    ClientScript scripts[MAX_CLIENT_SCRIPTS];
    uint32_t next_id;
    int count;
    int message_from_server_ref;
    int run_frame_ref;
    int input_began_ref;
    int input_ended_ref;
    int input_changed_ref;
    int builtin_pressed_ref;
    bool input_prev_inited;
    bool input_prev_keys[256];
    bool input_prev_mouse[3];
    float input_prev_mx, input_prev_my;
    bool is_playtest;

    float move_gravity;
    float move_walk_speed;
    float move_jump_impulse;
    int move_mode;
    float cam_dist_min;
    float cam_dist_max;

    bool cam_override;
    Vec3 cam_pos;
    float cam_yaw, cam_pitch, cam_roll, cam_fov;
    Vec3 cam_live_pos;
    float cam_live_yaw, cam_live_pitch, cam_live_roll, cam_live_fov;

#define PW_CAM_TW_POS 1
#define PW_CAM_TW_YAW 2
#define PW_CAM_TW_PITCH 4
#define PW_CAM_TW_ROLL 8
#define PW_CAM_TW_FOV 16
    int cam_tween;
    float cam_tween_t, cam_tween_dur;
    uint8_t cam_tween_ease;
    int cam_tween_bits;
    Vec3 cam_tween_from_pos, cam_tween_to_pos;
    float cam_tween_from_yaw, cam_tween_to_yaw;
    float cam_tween_from_pitch, cam_tween_to_pitch;
    float cam_tween_from_roll, cam_tween_to_roll;
    float cam_tween_from_fov, cam_tween_to_fov;
    int cam_tween_completed_ref;

#define PW_MAX_CLIENT_PART_TOUCH 64
    struct {
        EntityID entity;
        int touched_ref;
        int touchended_ref;
        int clicked_ref;
        bool is_touching;
        bool touched_this_frame;
    } part_touch[PW_MAX_CLIENT_PART_TOUCH];

#define PW_MAX_TWEENS 128
#define PW_TW_POS   1u
#define PW_TW_ROT   2u
#define PW_TW_SIZE  4u
#define PW_TW_COLOR 8u
#define PW_TW_ALPHA 16u
#define PW_TW_PLAYING   0
#define PW_TW_PAUSED    1
#define PW_TW_COMPLETED 2
#define PW_TW_CANCELLED 3
    struct {
        bool active;
        bool paused;
        uint8_t playback;
        uint8_t easing;
        uint8_t props;
        EntityID entity;
        uint32_t gen;
        float t, duration;
        float from_pos[3], to_pos[3];
        float from_rot[3], to_rot[3];
        float from_size[3], to_size[3];
        float from_color[3], to_color[3];
        float from_alpha, to_alpha;
        int completed_ref;
    } tweens[PW_MAX_TWEENS];
    uint32_t next_tween_gen;
};

static void tween_cancel_entity(ClientScriptEngine* e, EntityID entity, uint8_t bits);

static int lua_player_index(lua_State* L);
static int lua_player_newindex(lua_State* L);

static int lua_wait(lua_State* L) {
    float seconds = (float)luaL_optnumber(L, 1, 0.0);
    lua_pushlightuserdata(L, (void*)L);
    lua_pushnumber(L, seconds);
    lua_settable(L, LUA_REGISTRYINDEX);
    return lua_yield(L, 0);
}

static int lua_rgbcolor_new(lua_State* L) {
    lua_newtable(L);
    double r = luaL_checknumber(L, 1) / 255.0;
    double g = luaL_checknumber(L, 2) / 255.0;
    double b = luaL_checknumber(L, 3) / 255.0;
    lua_pushnumber(L, r); lua_setfield(L, -2, "r");
    lua_pushnumber(L, g); lua_setfield(L, -2, "g");
    lua_pushnumber(L, b); lua_setfield(L, -2, "b");
    lua_pushnumber(L, r); lua_setfield(L, -2, "R");
    lua_pushnumber(L, g); lua_setfield(L, -2, "G");
    lua_pushnumber(L, b); lua_setfield(L, -2, "B");
    return 1;
}

static int lua_rgbcolor_random(lua_State* L) {
    lua_newtable(L);
    double r = (double)(rand() % 256) / 255.0;
    double g = (double)(rand() % 256) / 255.0;
    double b = (double)(rand() % 256) / 255.0;
    lua_pushnumber(L, r); lua_setfield(L, -2, "r");
    lua_pushnumber(L, g); lua_setfield(L, -2, "g");
    lua_pushnumber(L, b); lua_setfield(L, -2, "b");
    lua_pushnumber(L, r); lua_setfield(L, -2, "R");
    lua_pushnumber(L, g); lua_setfield(L, -2, "G");
    lua_pushnumber(L, b); lua_setfield(L, -2, "B");
    return 1;
}

typedef struct { Scene* scene; EntityID entity; } ObjProxy;

static void push_vec3(lua_State* L, float x, float y, float z) {
    lua_newtable(L);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, x); lua_setfield(L, -2, "X");
    lua_pushnumber(L, y); lua_setfield(L, -2, "Y");
    lua_pushnumber(L, z); lua_setfield(L, -2, "Z");
}

static void push_color(lua_State* L, float r, float g, float b) {
    lua_newtable(L);
    lua_pushnumber(L, r); lua_setfield(L, -2, "r");
    lua_pushnumber(L, g); lua_setfield(L, -2, "g");
    lua_pushnumber(L, b); lua_setfield(L, -2, "b");
    lua_pushnumber(L, r); lua_setfield(L, -2, "R");
    lua_pushnumber(L, g); lua_setfield(L, -2, "G");
    lua_pushnumber(L, b); lua_setfield(L, -2, "B");
}

static ClientScriptEngine* get_engine_from_state(lua_State* L);
static int client_mod_base(lua_State* L);

static int lua_obj_destroy(lua_State* L);
static int ieq_ascii(const char* a, const char* b);

static int lua_obj_istype(lua_State* L) {
    (void)luaL_checkudata(L, 1, "PW_Obj");
    const char* type_name = luaL_checkstring(L, 2);
    lua_pushboolean(L, strcmp(type_name, "Part") == 0 ||
                       strcmp(type_name, "BasePart") == 0 ||
                       strcmp(type_name, "Instance") == 0);
    return 1;
}

static int read_vec3_field(lua_State* L, int idx, const char* key, float* out) {
    lua_getfield(L, idx, key);
    if (!lua_isnumber(L, -1) && key && key[0] >= 'a' && key[0] <= 'z') {
        char up[2] = { (char)(key[0] - ('a' - 'A')), 0 };
        lua_pop(L, 1);
        lua_getfield(L, idx, up);
    }
    if (!lua_isnumber(L, -1)) { lua_pop(L, 1); return 0; }
    *out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return 1;
}

static int client_part_touch_ensure(ClientScriptEngine* e, EntityID entity) {
    if (!e || entity == ENTITY_INVALID) return -1;
    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity == entity)
            return i;
    }
    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity != ENTITY_INVALID) continue;
        pw_event_open(e->L);
        pw_event_new(e->L);
        e->part_touch[i].touched_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
        pw_event_new(e->L);
        e->part_touch[i].touchended_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
        pw_event_new(e->L);
        e->part_touch[i].clicked_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
        e->part_touch[i].entity = entity;
        e->part_touch[i].is_touching = false;
        e->part_touch[i].touched_this_frame = false;
        return i;
    }
    return -1;
}

static int client_push_proxy_event(lua_State* L, EntityID entity, const char* key) {
    ClientScriptEngine* e = get_engine_from_state(L);
    int slot = (e && entity != ENTITY_INVALID) ? client_part_touch_ensure(e, entity) : -1;
    if (slot >= 0) {
        if (strcmp(key, "ClickDetector") == 0) {
            lua_newtable(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, e->part_touch[slot].clicked_ref);
            lua_setfield(L, -2, "MouseClick");
            lua_pushvalue(L, -1);
            lua_setfield(L, -2, "Clicked");
            return 1;
        }
        int ref = e->part_touch[slot].touched_ref;
        if (strcmp(key, "TouchEnded") == 0)
            ref = e->part_touch[slot].touchended_ref;
        else if (strcmp(key, "Clicked") == 0 || strcmp(key, "MouseClick") == 0)
            ref = e->part_touch[slot].clicked_ref;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        return 1;
    }

    lua_getiuservalue(L, 1, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        pw_event_open(L);
        pw_event_new(L); lua_setfield(L, -2, "Touched");
        pw_event_new(L); lua_setfield(L, -2, "TouchEnded");
        pw_event_new(L); lua_setfield(L, -2, "Clicked");
        lua_pushvalue(L, -1);
        lua_setiuservalue(L, 1, 1);
    }
    if (strcmp(key, "ClickDetector") == 0) {
        lua_getfield(L, -1, "Clicked");
        lua_newtable(L);
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "MouseClick");
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "Clicked");
        lua_remove(L, -2);
        lua_remove(L, -2);
        return 1;
    }
    const char* ek = (strcmp(key, "MouseClick") == 0) ? "Clicked" : key;
    lua_getfield(L, -1, ek);
    lua_remove(L, -2);
    return 1;
}

static int obj_index(lua_State* L) {
    ObjProxy* p = (ObjProxy*)luaL_checkudata(L, 1, "PW_Obj");
    const char* key = luaL_checkstring(L, 2);

    if (strcmp(key, "Touched") == 0 || strcmp(key, "TouchEnded") == 0 ||
        strcmp(key, "Clicked") == 0 || strcmp(key, "MouseClick") == 0 ||
        strcmp(key, "ClickDetector") == 0)
        return client_push_proxy_event(L, p->entity, key);

    Entity* ent = scene_get_entity(p->scene, p->entity);
    if (!ent) { lua_pushnil(L); return 1; }

    if (strcmp(key, "color") == 0 || strcmp(key, "Color") == 0) {
        push_color(L, ent->material.color.x, ent->material.color.y, ent->material.color.z);
    } else if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0) {
        push_vec3(L, ent->transform.position.x, ent->transform.position.y, ent->transform.position.z);
    } else if (strcmp(key, "rotation") == 0 || strcmp(key, "Rotation") == 0) {
        push_vec3(L, ent->transform.rotation.x, ent->transform.rotation.y, ent->transform.rotation.z);
    } else if (strcmp(key, "size") == 0 || strcmp(key, "Size") == 0) {
        push_vec3(L, ent->transform.scale.x, ent->transform.scale.y, ent->transform.scale.z);
    } else if (strcmp(key, "Transparency") == 0 || strcmp(key, "transparency") == 0) {

        lua_pushnumber(L, 1.0 - (double)ent->material.alpha);
    } else if (strcmp(key, "Glow") == 0 || strcmp(key, "glow") == 0) {
        lua_pushnumber(L, ent->material.glow);
    } else if (strcmp(key, "Shape") == 0 || strcmp(key, "shape") == 0) {
        const char* sh = "Box";
        int can_col = 1;
        client_script_part_meta(p->entity, &sh, &can_col);
        lua_pushstring(L, sh ? sh : "Box");
    } else if (strcmp(key, "Material") == 0 || strcmp(key, "material") == 0) {
        lua_pushstring(L, part_material_name(ent->material.part_material));
    } else if (strcmp(key, "Anchored") == 0 || strcmp(key, "anchored") == 0) {
        lua_pushboolean(L, 1);
    } else if (strcmp(key, "CanCollide") == 0 || strcmp(key, "canCollide") == 0 ||
               strcmp(key, "cancollide") == 0) {
        const char* sh = NULL;
        int can_col = 1;
        client_script_part_meta(p->entity, &sh, &can_col);
        lua_pushboolean(L, can_col);
    } else if (strcmp(key, "ClassName") == 0) {
        lua_pushstring(L, "Part");
    } else if (strcmp(key, "Name") == 0 || strcmp(key, "name") == 0) {
        const char* nm = client_script_local_part_name(p->entity);
        lua_pushstring(L, nm ? nm : "Part");
    } else if (strcmp(key, "Client") == 0 || strcmp(key, "client") == 0) {
        lua_pushboolean(L, client_script_is_local_part(p->entity) ? 1 : 0);
    } else if (strcmp(key, "Parent") == 0 || strcmp(key, "parent") == 0) {
        lua_pushnil(L);
    } else if (strcmp(key, "IsType") == 0) {
        lua_pushcfunction(L, lua_obj_istype);
        return 1;
    } else if (strcmp(key, "Destroy") == 0) {
        lua_pushcfunction(L, lua_obj_destroy);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int obj_newindex(lua_State* L) {
    ObjProxy* p = (ObjProxy*)luaL_checkudata(L, 1, "PW_Obj");
    const char* key = luaL_checkstring(L, 2);
    Entity* ent = scene_get_entity(p->scene, p->entity);
    if (!ent) return 0;

    if (strcmp(key, "color") == 0 || strcmp(key, "Color") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "r", &ent->material.color.x);
        read_vec3_field(L, 3, "g", &ent->material.color.y);
        read_vec3_field(L, 3, "b", &ent->material.color.z);
    } else if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &ent->transform.position.x);
        read_vec3_field(L, 3, "y", &ent->transform.position.y);
        read_vec3_field(L, 3, "z", &ent->transform.position.z);
    } else if (strcmp(key, "rotation") == 0 || strcmp(key, "Rotation") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &ent->transform.rotation.x);
        read_vec3_field(L, 3, "y", &ent->transform.rotation.y);
        read_vec3_field(L, 3, "z", &ent->transform.rotation.z);
    } else if (strcmp(key, "size") == 0 || strcmp(key, "Size") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &ent->transform.scale.x);
        read_vec3_field(L, 3, "y", &ent->transform.scale.y);
        read_vec3_field(L, 3, "z", &ent->transform.scale.z);
    } else if (strcmp(key, "Transparency") == 0 || strcmp(key, "transparency") == 0) {
        float t = (float)luaL_checknumber(L, 3);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        ent->material.alpha = 1.0f - t;
    } else if (strcmp(key, "Glow") == 0 || strcmp(key, "glow") == 0) {
        float g = (float)luaL_checknumber(L, 3);
        if (g < 0.0f) g = 0.0f;
        ent->material.glow = g;
    } else if (strcmp(key, "Name") == 0 || strcmp(key, "name") == 0) {
        if (!client_script_is_local_part(p->entity))
            return luaL_error(L, "Property 'Name' is read-only on server Parts");
        client_script_local_part_set_name(p->entity, luaL_checkstring(L, 3));
    } else if (strcmp(key, "CanCollide") == 0 || strcmp(key, "canCollide") == 0 ||
               strcmp(key, "cancollide") == 0) {
        if (!client_script_is_local_part(p->entity))
            return luaL_error(L, "Property 'CanCollide' is read-only on server Parts");
        client_script_local_part_set_collide(p->entity, lua_toboolean(L, 3) ? 1 : 0);
    } else if (strcmp(key, "Shape") == 0 || strcmp(key, "shape") == 0) {
        if (!client_script_is_local_part(p->entity))
            return luaL_error(L, "Property 'Shape' is read-only on server Parts");
        const char* sh = luaL_checkstring(L, 3);
        uint8_t t = 0;
        if (ieq_ascii(sh, "Sphere")) t = 1;
        else if (ieq_ascii(sh, "Cylinder")) t = 2;
        else if (ieq_ascii(sh, "Wedge")) t = 3;
        else if (!ieq_ascii(sh, "Box") && !ieq_ascii(sh, "Brick"))
            return luaL_error(L, "Shape must be Box, Sphere, Cylinder, or Wedge");
        client_script_local_part_set_shape(p->entity, t);
    } else if (strcmp(key, "Material") == 0 || strcmp(key, "material") == 0) {
        const char* ms = luaL_checkstring(L, 3);
        uint8_t m = part_material_from_name(ms);
        if (m == PART_MATERIAL_PLASTIC && !ieq_ascii(ms, "Plastic"))
            return luaL_error(L, "Material must be Plastic, Grass, Dirt, Rock, Sand, Wood, or Metal");
        ent->material.part_material = m;
    } else if (strcmp(key, "Client") == 0 || strcmp(key, "client") == 0) {
        return luaL_error(L, "Property 'Client' is read-only");
    }
    {
        ClientScriptEngine* eng = get_engine_from_state(L);
        uint8_t bits = 0;
        if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0) bits = (uint8_t)PW_TW_POS;
        else if (strcmp(key, "rotation") == 0 || strcmp(key, "Rotation") == 0) bits = (uint8_t)PW_TW_ROT;
        else if (strcmp(key, "size") == 0 || strcmp(key, "Size") == 0) bits = (uint8_t)PW_TW_SIZE;
        else if (strcmp(key, "color") == 0 || strcmp(key, "Color") == 0) bits = (uint8_t)PW_TW_COLOR;
        else if (strcmp(key, "Transparency") == 0 || strcmp(key, "transparency") == 0)
            bits = (uint8_t)PW_TW_ALPHA;
        if (eng && bits) tween_cancel_entity(eng, p->entity, bits);
    }
    if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0 ||
        strcmp(key, "rotation") == 0 || strcmp(key, "Rotation") == 0)
        client_script_part_commit_transform(p->entity);
    else if (strcmp(key, "size") == 0 || strcmp(key, "Size") == 0)
        client_script_part_commit_size(p->entity);
    return 0;
}

static int lua_obj_destroy(lua_State* L) {
    ObjProxy* p = (ObjProxy*)luaL_checkudata(L, 1, "PW_Obj");
    if (!client_script_is_local_part(p->entity))
        return luaL_error(L, "Destroy: only client-created Parts can be destroyed from LocalScripts");
    ClientScriptEngine* e = get_engine_from_state(L);
    if (e) {
        tween_cancel_entity(e, p->entity,
            (uint8_t)(PW_TW_POS | PW_TW_ROT | PW_TW_SIZE | PW_TW_COLOR | PW_TW_ALPHA));
        for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
            if (e->part_touch[i].entity != p->entity) continue;
            luaL_unref(e->L, LUA_REGISTRYINDEX, e->part_touch[i].touched_ref);
            luaL_unref(e->L, LUA_REGISTRYINDEX, e->part_touch[i].touchended_ref);
            luaL_unref(e->L, LUA_REGISTRYINDEX, e->part_touch[i].clicked_ref);
            e->part_touch[i].entity = ENTITY_INVALID;
            break;
        }
    }
    client_script_destroy_local_part(p->entity);
    p->entity = ENTITY_INVALID;
    return 0;
}

static int lua_explosion_new(lua_State* L);

static ClientScriptEngine* get_engine_from_state(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_pw_engine");
    ClientScriptEngine* e = (ClientScriptEngine*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return e;
}

static int lua_player_gethealth(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !e->player) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, e->player->health);
    return 1;
}

static int lua_player_getposition(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !e->player) { lua_pushnil(L); return 1; }
    push_vec3(L, e->player->pos.x, e->player->pos.y, e->player->pos.z);
    return 1;
}

static int lua_player_setposition(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !e->player) return 0;

    if (lua_istable(L, 2)) {
        read_vec3_field(L, 2, "x", &e->player->pos.x);
        read_vec3_field(L, 2, "y", &e->player->pos.y);
        read_vec3_field(L, 2, "z", &e->player->pos.z);
    } else {
        e->player->pos.x = (float)luaL_checknumber(L, 2);
        e->player->pos.y = (float)luaL_checknumber(L, 3);
        e->player->pos.z = (float)luaL_checknumber(L, 4);
    }
    return 0;
}

static int lua_player_notify(lua_State* L) {
    const char* text = luaL_checkstring(L, 2);
    float secs = (float)luaL_optnumber(L, 3, 3.0);
    client_script_ui_notify(text, secs);
    return 0;
}

static int lua_char_clone(lua_State* L) {
    (void)L;
    lua_pushnil(L);
    return 1;
}

static int lua_char_istype(lua_State* L) {
    const char* type_name = luaL_checkstring(L, 2);
    lua_pushboolean(L, strcmp(type_name, "Character") == 0 ||
                       strcmp(type_name, "Instance") == 0);
    return 1;
}

static int lua_char_index(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    const char* key = luaL_checkstring(L, 2);

    if (strcmp(key, "IsType") == 0) { lua_pushcfunction(L, lua_char_istype); return 1; }
    if (strcmp(key, "Clone") == 0) { lua_pushcfunction(L, lua_char_clone); return 1; }
    if (strcmp(key, "SetPosition") == 0) {
        lua_pushcfunction(L, lua_player_setposition); return 1;
    }
    if (strcmp(key, "ClassName") == 0) {
        lua_pushstring(L, "Character"); return 1;
    }
    if (strcmp(key, "Player") == 0) {
        lua_getglobal(L, "player");
        return 1;
    }
    if (strcmp(key, "Name") == 0 || strcmp(key, "name") == 0) {
        lua_pushstring(L, (e && e->player_name[0]) ? e->player_name : "Character");
        return 1;
    }
    if (strcmp(key, "Yaw") == 0 || strcmp(key, "yaw") == 0) {
        lua_pushnumber(L, (e && e->player) ? e->player->current_yaw : 0);
        return 1;
    }
    if (strcmp(key, "Health") == 0 || strcmp(key, "health") == 0 ||
        strcmp(key, "Position") == 0 || strcmp(key, "position") == 0 ||
        strcmp(key, "x") == 0 || strcmp(key, "y") == 0 || strcmp(key, "z") == 0) {
        return lua_player_index(L);
    }
    if (strcmp(key, "Animation") == 0 || strcmp(key, "animation") == 0) {
        lua_pushinteger(L, 0);
        return 1;
    }
    if (strcmp(key, "Transparency") == 0 || strcmp(key, "transparency") == 0) {
        lua_pushnumber(L, 0);
        return 1;
    }
    if (strcmp(key, "Velocity") == 0 || strcmp(key, "velocity") == 0) {
        if (!e || !e->player) { lua_pushnil(L); return 1; }
        push_vec3(L, e->player->vel.x, e->player->vel.y, e->player->vel.z);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int lua_char_newindex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Player") == 0 || strcmp(key, "ClassName") == 0 ||
        strcmp(key, "Yaw") == 0 || strcmp(key, "yaw") == 0 ||
        strcmp(key, "Name") == 0 || strcmp(key, "name") == 0) {
        return luaL_error(L, "Property '%s' is read-only!", key);
    }
    if (strcmp(key, "Health") == 0 || strcmp(key, "health") == 0) {
        ClientScriptEngine* e = get_engine_from_state(L);
        if (e && e->player) {
            int val = (int)luaL_checkinteger(L, 3);
            if (val < 0) val = 0;
            if (val > 100) val = 100;
            e->player->health = val;
        }
        return 0;
    }
    if (strcmp(key, "Velocity") == 0 || strcmp(key, "velocity") == 0) {
        ClientScriptEngine* e = get_engine_from_state(L);
        if (e && e->player) {
            luaL_checktype(L, 3, LUA_TTABLE);
            read_vec3_field(L, 3, "x", &e->player->vel.x);
            read_vec3_field(L, 3, "y", &e->player->vel.y);
            read_vec3_field(L, 3, "z", &e->player->vel.z);
        }
        return 0;
    }
    return lua_player_newindex(L);
}

static int lua_player_index(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    const char* key = luaL_checkstring(L, 2);

    if (strcmp(key, "health") == 0 || strcmp(key, "Health") == 0) {
        lua_pushinteger(L, (e && e->player) ? e->player->health : 0);
    } else if (strcmp(key, "name") == 0 || strcmp(key, "Name") == 0) {
        lua_pushstring(L, (e && e->player_name[0]) ? e->player_name : "Player");
    } else if (strcmp(key, "x") == 0) {
        lua_pushnumber(L, (e && e->player) ? e->player->pos.x : 0);
    } else if (strcmp(key, "y") == 0) {
        lua_pushnumber(L, (e && e->player) ? e->player->pos.y : 0);
    } else if (strcmp(key, "z") == 0) {
        lua_pushnumber(L, (e && e->player) ? e->player->pos.z : 0);
    } else if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0) {
        if (!e || !e->player) { lua_pushnil(L); return 1; }
        push_vec3(L, e->player->pos.x, e->player->pos.y, e->player->pos.z);
    } else if (strcmp(key, "yaw") == 0 || strcmp(key, "Yaw") == 0) {
        lua_pushnumber(L, (e && e->player) ? e->player->current_yaw : 0);
    } else if (strcmp(key, "Character") == 0) {
        lua_getglobal(L, "Character");
        if (lua_isnil(L, -1)) {

            lua_pop(L, 1);
            lua_newtable(L);
            lua_newtable(L);
            lua_pushcfunction(L, lua_char_index);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, lua_char_newindex);
            lua_setfield(L, -2, "__newindex");
            lua_setmetatable(L, -2);
        }
    } else if (strcmp(key, "dead") == 0 || strcmp(key, "Dead") == 0) {
        lua_pushboolean(L, (e && e->player) ? e->player->dead : 0);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lua_player_index_full(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_rawget(L, 1);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    return lua_player_index(L);
}

static int lua_player_newindex(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !e->player) return 0;
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0) {
        e->player->pos.x = (float)luaL_checknumber(L, 3);
    } else if (strcmp(key, "y") == 0) {
        e->player->pos.y = (float)luaL_checknumber(L, 3);
    } else if (strcmp(key, "z") == 0) {
        e->player->pos.z = (float)luaL_checknumber(L, 3);
    } else if (strcmp(key, "position") == 0 || strcmp(key, "Position") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &e->player->pos.x);
        read_vec3_field(L, 3, "y", &e->player->pos.y);
        read_vec3_field(L, 3, "z", &e->player->pos.z);
    }
    return 0;
}

static int lua_vector3_new(lua_State* L) {
    push_vec3(L, (float)luaL_optnumber(L, 1, 0),
                 (float)luaL_optnumber(L, 2, 0),
                 (float)luaL_optnumber(L, 3, 0));
    return 1;
}

static int lua_print_override(lua_State* L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) PW_LOG("\t");
        printf("%s", luaL_tolstring(L, i, NULL));
        lua_pop(L, 1);
    }
    printf("\n");
    return 0;
}

static int lua_warn_override(lua_State* L) {
    int n = lua_gettop(L);
    fprintf(stderr, "[LocalScript warn] ");
    for (int i = 1; i <= n; i++) {
        if (i > 1) fputc('\t', stderr);
        fputs(luaL_tolstring(L, i, NULL), stderr);
        lua_pop(L, 1);
    }
    fputc('\n', stderr);
    return 0;
}

static int ieq_ascii(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int key_code_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    if (ieq_ascii(name, "Space")) return 32;
    if (ieq_ascii(name, "LeftShift") || ieq_ascii(name, "RightShift") || ieq_ascii(name, "Shift"))
        return 16;
    if (ieq_ascii(name, "LeftControl") || ieq_ascii(name, "RightControl") ||
        ieq_ascii(name, "LeftCtrl") || ieq_ascii(name, "Control") || ieq_ascii(name, "Ctrl"))
        return 17;
    if (ieq_ascii(name, "LeftAlt") || ieq_ascii(name, "RightAlt") || ieq_ascii(name, "Alt"))
        return 18;
    if (ieq_ascii(name, "Escape") || ieq_ascii(name, "Esc")) return 27;
    if (ieq_ascii(name, "Tab")) return 9;
    if (ieq_ascii(name, "Return") || ieq_ascii(name, "Enter")) return 13;
    if (ieq_ascii(name, "Backspace")) return 8;
    if (ieq_ascii(name, "Left") || ieq_ascii(name, "LeftArrow")) return 37;
    if (ieq_ascii(name, "Up") || ieq_ascii(name, "UpArrow")) return 38;
    if (ieq_ascii(name, "Right") || ieq_ascii(name, "RightArrow")) return 39;
    if (ieq_ascii(name, "Down") || ieq_ascii(name, "DownArrow")) return 40;
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9') {
        int n = 0;
        for (int i = 1; name[i] >= '0' && name[i] <= '9'; i++)
            n = n * 10 + (name[i] - '0');
        if (n >= 1 && n <= 12) return 111 + n;
    }
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)(unsigned char)c;
    }
    return -1;
}

static const char* key_name_from_code(int code) {
    static char letter[2];
    if (code >= 65 && code <= 90) {
        letter[0] = (char)code;
        letter[1] = 0;
        return letter;
    }
    if (code >= 48 && code <= 57) {
        letter[0] = (char)code;
        letter[1] = 0;
        return letter;
    }
    switch (code) {
        case 32: return "Space";
        case 16: return "LeftShift";
        case 17: return "LeftControl";
        case 18: return "LeftAlt";
        case 27: return "Escape";
        case 9: return "Tab";
        case 13: return "Return";
        case 8: return "Backspace";
        case 37: return "Left";
        case 38: return "Up";
        case 39: return "Right";
        case 40: return "Down";
        case 112: return "F1";
        case 113: return "F2";
        case 114: return "F3";
        case 115: return "F4";
        case 116: return "F5";
        case 117: return "F6";
        case 118: return "F7";
        case 119: return "F8";
        case 120: return "F9";
        case 121: return "F10";
        case 122: return "F11";
        case 123: return "F12";
        default: return NULL;
    }
}

static void push_input_info(lua_State* L, const char* key, int button, float x, float y,
                            float dx, float dy, int touch) {
    lua_newtable(L);
    if (key) lua_pushstring(L, key); else lua_pushnil(L);
    lua_setfield(L, -2, "Key");
    if (button >= 0) lua_pushinteger(L, button); else lua_pushnil(L);
    lua_setfield(L, -2, "Button");
    push_vec3(L, x, y, 0);
    lua_setfield(L, -2, "Position");
    push_vec3(L, dx, dy, 0);
    lua_setfield(L, -2, "Delta");
    lua_pushboolean(L, touch);
    lua_setfield(L, -2, "Touch");
}

static int lua_input_is_key_down(lua_State* L) {
    int b = client_mod_base(L);
    const char* name = luaL_checkstring(L, b + 1);
    int code = key_code_from_name(name);
    lua_pushboolean(L, code >= 0 && input_key_held(code));
    return 1;
}

static int lua_input_is_mouse_down(lua_State* L) {
    int b = client_mod_base(L);
    int button = (int)luaL_checkinteger(L, b + 1);
    lua_pushboolean(L, input_mouse_button_held(button));
    return 1;
}

static int lua_input_get_builtin(lua_State* L) {
    int b = client_mod_base(L);
    const char* name = luaL_checkstring(L, b + 1);
    if (!ieq_ascii(name, "Jump")) { lua_pushnil(L); return 1; }
    const InputState* in = input_get_state();
    lua_newtable(L);
    lua_pushstring(L, "Jump"); lua_setfield(L, -2, "Name");
    push_vec3(L, 0, 0, 0); lua_setfield(L, -2, "Position");
    push_vec3(L, 0, 0, 0); lua_setfield(L, -2, "Size");
    lua_pushboolean(L, in && in->key_space_held);
    lua_setfield(L, -2, "IsDown");
    return 1;
}

static int lua_input_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    const InputState* in = input_get_state();
    if (strcmp(key, "MousePosition") == 0) {
        push_vec3(L, in ? in->mouse_x : 0, in ? in->mouse_y : 0, 0);
        return 1;
    }
    if (strcmp(key, "MouseDelta") == 0) {
        push_vec3(L, in ? in->mouse_dx : 0, in ? in->mouse_dy : 0, 0);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static void push_obj_entity(lua_State* L, ClientScriptEngine* e, EntityID entity) {
    if (!e || entity == ENTITY_INVALID) { lua_pushnil(L); return; }
    ObjProxy* proxy = (ObjProxy*)lua_newuserdata(L, sizeof(ObjProxy));
    proxy->scene = e->scene;
    proxy->entity = entity;
    luaL_setmetatable(L, "PW_Obj");
}

static int lua_ray_make_params(lua_State* L) {
    (void)client_mod_base(L);
    lua_newtable(L);
    lua_pushstring(L, "Exclude"); lua_setfield(L, -2, "FilterType");
    lua_newtable(L); lua_setfield(L, -2, "FilterDescendantsInstances");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "IgnoreWater");
    return 1;
}

static int lua_read_vec3_arg(lua_State* L, int idx, float* x, float* y, float* z) {
    if (!lua_istable(L, idx)) return 0;
    if (!read_vec3_field(L, idx, "x", x)) *x = 0;
    if (!read_vec3_field(L, idx, "y", y)) *y = 0;
    if (!read_vec3_field(L, idx, "z", z)) *z = 0;
    return 1;
}

static int lua_read_rgb_arg(lua_State* L, int idx, float* r, float* g, float* b) {
    if (!lua_istable(L, idx)) return 0;
    if (!read_vec3_field(L, idx, "r", r)) *r = 0;
    if (!read_vec3_field(L, idx, "g", g)) *g = 0;
    if (!read_vec3_field(L, idx, "b", b)) *b = 0;
    return 1;
}

static int lua_ray_cast(lua_State* L) {
    int b = client_mod_base(L);
    float ox, oy, oz, dx, dy, dz;
    if (!lua_read_vec3_arg(L, b + 1, &ox, &oy, &oz) ||
        !lua_read_vec3_arg(L, b + 2, &dx, &dy, &dz))
        return luaL_error(L, "RaycastModule:Raycast expects origin and direction Vector3");
    float max_dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (max_dist < 1e-6f) { lua_pushnil(L); return 1; }

    float hx, hy, hz, nx, ny, nz, dist;
    EntityID hit_ent = ENTITY_INVALID;
    if (!client_script_world_raycast(ox, oy, oz, dx, dy, dz, max_dist,
                                     &hx, &hy, &hz, &nx, &ny, &nz, &dist, &hit_ent)) {
        lua_pushnil(L);
        return 1;
    }

    int params = b + 3;
    if (lua_istable(L, params) && hit_ent != ENTITY_INVALID) {
        lua_getfield(L, params, "FilterType");
        const char* ftype = lua_tostring(L, -1);
        int include = ftype && ieq_ascii(ftype, "Include");
        lua_pop(L, 1);
        lua_getfield(L, params, "FilterDescendantsInstances");
        if (lua_istable(L, -1)) {
            int matched = 0;
            int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                ObjProxy* fp = (ObjProxy*)luaL_testudata(L, -1, "PW_Obj");
                if (fp && fp->entity == hit_ent) matched = 1;
                lua_pop(L, 1);
            }
            if ((include && !matched) || (!include && matched)) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return 1;
            }
        }
        lua_pop(L, 1);
    }

    ClientScriptEngine* e = get_engine_from_state(L);
    lua_newtable(L);
    push_obj_entity(L, e, hit_ent);
    lua_setfield(L, -2, "Instance");
    push_vec3(L, hx, hy, hz);
    lua_setfield(L, -2, "Position");
    push_vec3(L, nx, ny, nz);
    lua_setfield(L, -2, "Normal");
    lua_pushnumber(L, dist);
    lua_setfield(L, -2, "Distance");
    return 1;
}

static int lua_ray_screen_to_world(lua_State* L) {
    int b = client_mod_base(L);
    float sx = (float)luaL_checknumber(L, b + 1);
    float sy = (float)luaL_checknumber(L, b + 2);
    float depth = (float)luaL_optnumber(L, b + 3, 0);
    float ox, oy, oz, dx, dy, dz;
    client_script_screen_to_world(sx, sy, depth, &ox, &oy, &oz, &dx, &dy, &dz);
    push_vec3(L, ox, oy, oz);
    push_vec3(L, dx, dy, dz);
    return 2;
}

static int lua_ray_point_to_screen(lua_State* L) {
    int b = client_mod_base(L);
    float wx, wy, wz;
    if (!lua_read_vec3_arg(L, b + 1, &wx, &wy, &wz))
        return luaL_error(L, "RaycastModule:PointToScreen expects a Vector3");
    float sx = 0, sy = 0;
    int on = 0;
    client_script_point_to_screen(wx, wy, wz, &sx, &sy, &on);
    lua_pushnumber(L, sx);
    lua_pushnumber(L, sy);
    lua_pushboolean(L, on);
    return 3;
}

static size_t client_encode_remote_payload(lua_State* L, int idx, uint8_t* data, size_t cap) {
    if (!data || cap < 24) return 0;
    memset(data, 0, cap > 36 ? 36 : cap);
    if (lua_istable(L, idx)) {
        float vals[9] = {0};
        const char* keys[6] = { "dir_x", "dir_y", "dir_z", "origin_x", "origin_y", "origin_z" };
        const char* alt[6] = { "x", "y", "z", "ox", "oy", "oz" };
        int any = 0;
        for (int i = 0; i < 6; i++) {
            lua_getfield(L, idx, keys[i]);
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                lua_getfield(L, idx, alt[i]);
            }
            if (lua_isnumber(L, -1)) {
                vals[i] = (float)lua_tonumber(L, -1);
                any = 1;
            }
            lua_pop(L, 1);
        }
        const char* hit_keys[3] = { "hit_x", "hit_y", "hit_z" };
        int has_hit = 0;
        for (int i = 0; i < 3; i++) {
            lua_getfield(L, idx, hit_keys[i]);
            if (lua_isnumber(L, -1)) {
                vals[6 + i] = (float)lua_tonumber(L, -1);
                has_hit = 1;
            }
            lua_pop(L, 1);
        }
        if (!any && !has_hit) return 0;
        memcpy(data, vals, 24);
        if (has_hit && cap >= 36) {
            memcpy(data + 24, vals + 6, 12);
            return 36;
        }
        return 24;
    }
    if (lua_isnumber(L, idx)) {
        float vals[6] = {0};
        int n = lua_gettop(L) - idx + 1;
        if (n > 6) n = 6;
        for (int i = 0; i < n; i++)
            vals[i] = (float)luaL_optnumber(L, idx + i, 0);
        memcpy(data, vals, 24);
        return 24;
    }
    return 0;
}

static int client_mod_base(lua_State* L) {
    if (lua_istable(L, 1) && !lua_isuserdata(L, 1))
        return 1;
    return 0;
}

static int lua_message_to_server(lua_State* L) {
    int arg = client_mod_base(L);
    const char* name = luaL_checkstring(L, arg + 1);
    uint8_t data[36];
    size_t data_len = 0;
    if (lua_gettop(L) >= arg + 2)
        data_len = client_encode_remote_payload(L, arg + 2, data, sizeof(data));
    client_script_send_remote(name, data_len ? data : NULL, data_len);
    return 0;
}

static int lua_fire_server(lua_State* L) {

    const char* name = luaL_checkstring(L, 1);
    uint8_t data[36];
    size_t data_len = 0;
    if (lua_gettop(L) >= 2)
        data_len = client_encode_remote_payload(L, 2, data, sizeof(data));
    client_script_send_remote(name, data_len ? data : NULL, data_len);
    return 0;
}

static int lua_ui_notify(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    float secs = (float)luaL_optnumber(L, 2, 3.0);
    client_script_ui_notify(text, secs);
    return 0;
}

static int lua_ui_hud(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    client_script_ui_hud(text);
    return 0;
}

static float cam_ease(uint8_t style, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    switch (style) {
    case 1:
        return 0.5f * (1.0f - cosf(t * 3.14159265f));
    case 2:
        return (t < 0.5f) ? (2.0f * t * t)
                          : (1.0f - powf(-2.0f * t + 2.0f, 2.0f) * 0.5f);
    case 3:
        return (t < 0.5f) ? (4.0f * t * t * t)
                          : (1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f);
    default:
        return t;
    }
}

static float cam_lerp_angle(float a, float b, float u) {
    float d = b - a;
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return a + d * u;
}

static uint8_t cam_parse_easing(const char* s) {
    if (!s || !s[0]) return 0;
    if (ieq_ascii(s, "Sine") || ieq_ascii(s, "InOutSine")) return 1;
    if (ieq_ascii(s, "Quad") || ieq_ascii(s, "InOutQuad")) return 2;
    if (ieq_ascii(s, "Cubic") || ieq_ascii(s, "InOutCubic")) return 3;
    return 0;
}

static void cam_capture_from(ClientScriptEngine* e) {
    if (!e || e->cam_override) return;
    e->cam_pos = e->cam_live_pos;
    e->cam_yaw = e->cam_live_yaw;
    e->cam_pitch = e->cam_live_pitch;
    e->cam_roll = e->cam_live_roll;
    e->cam_override = true;
}

static void cam_tick(ClientScriptEngine* e, float dt) {
    if (!e || e->cam_tween != 1) return;
    e->cam_tween_t += dt;
    float u = (e->cam_tween_dur <= 0.0f) ? 1.0f : (e->cam_tween_t / e->cam_tween_dur);
    if (u > 1.0f) u = 1.0f;
    float k = cam_ease(e->cam_tween_ease, u);
    int bits = e->cam_tween_bits;
    if (bits & PW_CAM_TW_POS) {
        e->cam_pos.x = e->cam_tween_from_pos.x + (e->cam_tween_to_pos.x - e->cam_tween_from_pos.x) * k;
        e->cam_pos.y = e->cam_tween_from_pos.y + (e->cam_tween_to_pos.y - e->cam_tween_from_pos.y) * k;
        e->cam_pos.z = e->cam_tween_from_pos.z + (e->cam_tween_to_pos.z - e->cam_tween_from_pos.z) * k;
    }
    if (bits & PW_CAM_TW_YAW)
        e->cam_yaw = cam_lerp_angle(e->cam_tween_from_yaw, e->cam_tween_to_yaw, k);
    if (bits & PW_CAM_TW_PITCH)
        e->cam_pitch = e->cam_tween_from_pitch + (e->cam_tween_to_pitch - e->cam_tween_from_pitch) * k;
    if (bits & PW_CAM_TW_ROLL)
        e->cam_roll = cam_lerp_angle(e->cam_tween_from_roll, e->cam_tween_to_roll, k);
    if (bits & PW_CAM_TW_FOV)
        e->cam_fov = e->cam_tween_from_fov + (e->cam_tween_to_fov - e->cam_tween_from_fov) * k;
    if (u >= 1.0f) {
        e->cam_tween = 0;
        if (e->cam_tween_completed_ref != LUA_NOREF && e->cam_tween_completed_ref != 0) {
            lua_rawgeti(e->L, LUA_REGISTRYINDEX, e->cam_tween_completed_ref);
            if (luaL_testudata(e->L, -1, "PW_Event"))
                pw_event_fire(e->L, -1, 0);
            lua_pop(e->L, 1);
        }
    }
}

static void tween_free_slot(ClientScriptEngine* e, int slot) {
    if (!e || slot < 0 || slot >= PW_MAX_TWEENS) return;
    if (e->tweens[slot].completed_ref != LUA_NOREF &&
        e->tweens[slot].completed_ref != 0) {
        luaL_unref(e->L, LUA_REGISTRYINDEX, e->tweens[slot].completed_ref);
        e->tweens[slot].completed_ref = LUA_NOREF;
    }
    e->tweens[slot].active = false;
}

static void tween_cancel_entity(ClientScriptEngine* e, EntityID entity, uint8_t bits) {
    if (!e || bits == 0 || entity == ENTITY_INVALID) return;
    for (int i = 0; i < PW_MAX_TWEENS; i++) {
        if (!e->tweens[i].active) continue;
        if (e->tweens[i].entity != entity) continue;
        e->tweens[i].props = (uint8_t)(e->tweens[i].props & ~bits);
        if (e->tweens[i].props != 0) continue;
        e->tweens[i].active = false;
        e->tweens[i].paused = false;
        e->tweens[i].playback = PW_TW_CANCELLED;
        tween_free_slot(e, i);
    }
}

static void tween_apply(ClientScriptEngine* e, int slot, float u) {
    if (!e || slot < 0 || slot >= PW_MAX_TWEENS) return;
    uint8_t props = e->tweens[slot].props;
    EntityID eid = e->tweens[slot].entity;
    Entity* ent = scene_get_entity(e->scene, eid);
    if (!ent) {
        e->tweens[slot].active = false;
        e->tweens[slot].playback = PW_TW_CANCELLED;
        tween_free_slot(e, slot);
        return;
    }
    if (props & PW_TW_POS) {
        ent->transform.position.x = e->tweens[slot].from_pos[0] +
            (e->tweens[slot].to_pos[0] - e->tweens[slot].from_pos[0]) * u;
        ent->transform.position.y = e->tweens[slot].from_pos[1] +
            (e->tweens[slot].to_pos[1] - e->tweens[slot].from_pos[1]) * u;
        ent->transform.position.z = e->tweens[slot].from_pos[2] +
            (e->tweens[slot].to_pos[2] - e->tweens[slot].from_pos[2]) * u;
    }
    if (props & PW_TW_ROT) {
        ent->transform.rotation.x = cam_lerp_angle(e->tweens[slot].from_rot[0],
                                                   e->tweens[slot].to_rot[0], u);
        ent->transform.rotation.y = cam_lerp_angle(e->tweens[slot].from_rot[1],
                                                   e->tweens[slot].to_rot[1], u);
        ent->transform.rotation.z = cam_lerp_angle(e->tweens[slot].from_rot[2],
                                                   e->tweens[slot].to_rot[2], u);
    }
    if (props & PW_TW_SIZE) {
        ent->transform.scale.x = e->tweens[slot].from_size[0] +
            (e->tweens[slot].to_size[0] - e->tweens[slot].from_size[0]) * u;
        ent->transform.scale.y = e->tweens[slot].from_size[1] +
            (e->tweens[slot].to_size[1] - e->tweens[slot].from_size[1]) * u;
        ent->transform.scale.z = e->tweens[slot].from_size[2] +
            (e->tweens[slot].to_size[2] - e->tweens[slot].from_size[2]) * u;
    }
    if (props & PW_TW_COLOR) {
        ent->material.color.x = e->tweens[slot].from_color[0] +
            (e->tweens[slot].to_color[0] - e->tweens[slot].from_color[0]) * u;
        ent->material.color.y = e->tweens[slot].from_color[1] +
            (e->tweens[slot].to_color[1] - e->tweens[slot].from_color[1]) * u;
        ent->material.color.z = e->tweens[slot].from_color[2] +
            (e->tweens[slot].to_color[2] - e->tweens[slot].from_color[2]) * u;
    }
    if (props & PW_TW_ALPHA) {
        ent->material.alpha = e->tweens[slot].from_alpha +
            (e->tweens[slot].to_alpha - e->tweens[slot].from_alpha) * u;
        if (ent->material.alpha < 0.0f) ent->material.alpha = 0.0f;
        if (ent->material.alpha > 1.0f) ent->material.alpha = 1.0f;
    }
    if (props & (PW_TW_POS | PW_TW_ROT))
        client_script_part_commit_transform(eid);
    if (props & PW_TW_SIZE)
        client_script_part_commit_size(eid);
}

static void tween_complete(ClientScriptEngine* e, int slot) {
    if (!e || slot < 0 || slot >= PW_MAX_TWEENS) return;
    e->tweens[slot].t = e->tweens[slot].duration;
    e->tweens[slot].active = false;
    e->tweens[slot].paused = false;
    e->tweens[slot].playback = PW_TW_COMPLETED;
    int cref = e->tweens[slot].completed_ref;
    if (cref != LUA_NOREF && cref != 0) {
        lua_rawgeti(e->L, LUA_REGISTRYINDEX, cref);
        if (luaL_testudata(e->L, -1, "PW_Event"))
            pw_event_fire(e->L, -1, 0);
        lua_pop(e->L, 1);
    }
}

static void tween_tick(ClientScriptEngine* e, float dt) {
    if (!e || dt <= 0.0f) return;
    for (int i = 0; i < PW_MAX_TWEENS; i++) {
        if (!e->tweens[i].active || e->tweens[i].paused) continue;
        float dur = e->tweens[i].duration;
        if (dur <= 0.0f) {
            tween_apply(e, i, 1.0f);
            tween_complete(e, i);
            continue;
        }
        e->tweens[i].t += dt;
        float u = e->tweens[i].t / dur;
        if (u >= 1.0f) {
            tween_apply(e, i, 1.0f);
            tween_complete(e, i);
        } else {
            tween_apply(e, i, cam_ease(e->tweens[i].easing, u));
        }
    }
}

static int tween_alloc(ClientScriptEngine* e) {
    if (!e) return -1;
    for (int i = 0; i < PW_MAX_TWEENS; i++) {
        if (!e->tweens[i].active && e->tweens[i].playback != PW_TW_PLAYING)
            return i;
    }
    for (int i = 0; i < PW_MAX_TWEENS; i++) {
        if (!e->tweens[i].active)
            return i;
    }
    e->tweens[0].playback = PW_TW_CANCELLED;
    e->tweens[0].active = false;
    tween_free_slot(e, 0);
    return 0;
}

static uint8_t tween_prop_bit(const char* name) {
    if (!name) return 0;
    if (ieq_ascii(name, "Position")) return (uint8_t)PW_TW_POS;
    if (ieq_ascii(name, "Rotation")) return (uint8_t)PW_TW_ROT;
    if (ieq_ascii(name, "Size")) return (uint8_t)PW_TW_SIZE;
    if (ieq_ascii(name, "Color")) return (uint8_t)PW_TW_COLOR;
    if (ieq_ascii(name, "Transparency")) return (uint8_t)PW_TW_ALPHA;
    return 0;
}

static int tween_handle_slot(lua_State* L, ClientScriptEngine** out_e) {
    if (!lua_istable(L, 1)) return -1;
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e) return -1;
    lua_getfield(L, 1, "_slot");
    int slot = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_gen");
    uint32_t gen = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (slot < 0 || slot >= PW_MAX_TWEENS) return -1;
    if (e->tweens[slot].gen != gen) return -1;
    if (out_e) *out_e = e;
    return slot;
}

static int lua_tween_handle_cancel(lua_State* L) {
    ClientScriptEngine* e = NULL;
    int slot = tween_handle_slot(L, &e);
    if (slot < 0 || !e) return 0;
    if (e->tweens[slot].active) {
        e->tweens[slot].active = false;
        e->tweens[slot].paused = false;
        e->tweens[slot].playback = PW_TW_CANCELLED;
        tween_free_slot(e, slot);
    }
    return 0;
}

static int lua_tween_handle_pause(lua_State* L) {
    ClientScriptEngine* e = NULL;
    int slot = tween_handle_slot(L, &e);
    if (slot < 0 || !e || !e->tweens[slot].active) return 0;
    e->tweens[slot].paused = true;
    e->tweens[slot].playback = PW_TW_PAUSED;
    return 0;
}

static int lua_tween_handle_play(lua_State* L) {
    ClientScriptEngine* e = NULL;
    int slot = tween_handle_slot(L, &e);
    if (slot < 0 || !e) return 0;
    if (e->tweens[slot].playback == PW_TW_CANCELLED) return 0;
    if (e->tweens[slot].playback == PW_TW_COMPLETED) {
        e->tweens[slot].t = 0.0f;
        e->tweens[slot].active = true;
    }
    e->tweens[slot].paused = false;
    e->tweens[slot].playback = PW_TW_PLAYING;
    e->tweens[slot].active = true;
    return 0;
}

static int lua_tween_handle_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "PlaybackState") == 0) {
        ClientScriptEngine* e = NULL;
        int slot = tween_handle_slot(L, &e);
        const char* st = "Cancelled";
        if (slot >= 0 && e) {
            switch (e->tweens[slot].playback) {
            case PW_TW_PLAYING: st = "Playing"; break;
            case PW_TW_PAUSED: st = "Paused"; break;
            case PW_TW_COMPLETED: st = "Completed"; break;
            default: st = "Cancelled"; break;
            }
        }
        lua_pushstring(L, st);
        return 1;
    }
    if (strcmp(key, "Cancel") == 0) { lua_pushcfunction(L, lua_tween_handle_cancel); return 1; }
    if (strcmp(key, "Pause") == 0) { lua_pushcfunction(L, lua_tween_handle_pause); return 1; }
    if (strcmp(key, "Play") == 0) { lua_pushcfunction(L, lua_tween_handle_play); return 1; }
    lua_rawget(L, 1);
    return 1;
}

static void tween_push_handle(lua_State* L, ClientScriptEngine* e, int slot) {
    lua_newtable(L);
    lua_pushinteger(L, slot);
    lua_setfield(L, -2, "_slot");
    lua_pushinteger(L, (lua_Integer)e->tweens[slot].gen);
    lua_setfield(L, -2, "_gen");
    if (e->tweens[slot].completed_ref != LUA_NOREF &&
        e->tweens[slot].completed_ref != 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, e->tweens[slot].completed_ref);
        lua_setfield(L, -2, "Completed");
    }
    luaL_getmetatable(L, "PW_Tween");
    lua_setmetatable(L, -2);
}

static int lua_tween_module_tween(lua_State* L) {
    int arg = client_mod_base(L);
    ObjProxy* part = (ObjProxy*)luaL_checkudata(L, arg + 1, "PW_Obj");
    luaL_checktype(L, arg + 2, LUA_TTABLE);
    float duration = (float)luaL_checknumber(L, arg + 3);
    const char* easing_s = luaL_optstring(L, arg + 4, "Linear");
    if (duration < 0.0f) duration = 0.0f;

    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !part)
        return luaL_error(L, "TweenModule:Tween expects a Part");
    Entity* ent = scene_get_entity(e->scene, part->entity);
    if (!ent)
        return luaL_error(L, "TweenModule:Tween: invalid Part");

    int goals = arg + 2;
    uint8_t props = 0;
    float to_pos[3] = {0}, to_rot[3] = {0}, to_size[3] = {0}, to_color[3] = {0};
    float to_alpha = ent->material.alpha;

    lua_getfield(L, goals, "Position");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, goals, "position"); }
    if (!lua_isnil(L, -1) && lua_read_vec3_arg(L, lua_gettop(L), &to_pos[0], &to_pos[1], &to_pos[2]))
        props |= (uint8_t)PW_TW_POS;
    lua_pop(L, 1);

    lua_getfield(L, goals, "Rotation");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, goals, "rotation"); }
    if (!lua_isnil(L, -1) && lua_read_vec3_arg(L, lua_gettop(L), &to_rot[0], &to_rot[1], &to_rot[2]))
        props |= (uint8_t)PW_TW_ROT;
    lua_pop(L, 1);

    lua_getfield(L, goals, "Size");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, goals, "size"); }
    if (!lua_isnil(L, -1) && lua_read_vec3_arg(L, lua_gettop(L), &to_size[0], &to_size[1], &to_size[2]))
        props |= (uint8_t)PW_TW_SIZE;
    lua_pop(L, 1);

    lua_getfield(L, goals, "Color");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, goals, "color"); }
    if (!lua_isnil(L, -1) && lua_read_rgb_arg(L, lua_gettop(L), &to_color[0], &to_color[1], &to_color[2]))
        props |= (uint8_t)PW_TW_COLOR;
    lua_pop(L, 1);

    lua_getfield(L, goals, "Transparency");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, goals, "transparency"); }
    if (lua_isnumber(L, -1)) {
        float tr = (float)lua_tonumber(L, -1);
        if (tr < 0.0f) tr = 0.0f;
        if (tr > 1.0f) tr = 1.0f;
        to_alpha = 1.0f - tr;
        props |= (uint8_t)PW_TW_ALPHA;
    }
    lua_pop(L, 1);

    if (props == 0) {
        lua_pushnil(L);
        return 1;
    }

    tween_cancel_entity(e, part->entity, props);
    int slot = tween_alloc(e);
    if (slot < 0) {
        lua_pushnil(L);
        return 1;
    }

    tween_free_slot(e, slot);
    memset(&e->tweens[slot], 0, sizeof(e->tweens[slot]));
    e->tweens[slot].entity = part->entity;
    e->tweens[slot].props = props;
    e->tweens[slot].duration = duration;
    e->tweens[slot].t = 0.0f;
    e->tweens[slot].easing = cam_parse_easing(easing_s);
    e->tweens[slot].gen = e->next_tween_gen++;
    if (e->next_tween_gen == 0) e->next_tween_gen = 1;
    e->tweens[slot].completed_ref = LUA_NOREF;
    e->tweens[slot].from_pos[0] = ent->transform.position.x;
    e->tweens[slot].from_pos[1] = ent->transform.position.y;
    e->tweens[slot].from_pos[2] = ent->transform.position.z;
    memcpy(e->tweens[slot].to_pos, to_pos, sizeof(to_pos));
    e->tweens[slot].from_rot[0] = ent->transform.rotation.x;
    e->tweens[slot].from_rot[1] = ent->transform.rotation.y;
    e->tweens[slot].from_rot[2] = ent->transform.rotation.z;
    memcpy(e->tweens[slot].to_rot, to_rot, sizeof(to_rot));
    e->tweens[slot].from_size[0] = ent->transform.scale.x;
    e->tweens[slot].from_size[1] = ent->transform.scale.y;
    e->tweens[slot].from_size[2] = ent->transform.scale.z;
    memcpy(e->tweens[slot].to_size, to_size, sizeof(to_size));
    e->tweens[slot].from_color[0] = ent->material.color.x;
    e->tweens[slot].from_color[1] = ent->material.color.y;
    e->tweens[slot].from_color[2] = ent->material.color.z;
    memcpy(e->tweens[slot].to_color, to_color, sizeof(to_color));
    e->tweens[slot].from_alpha = ent->material.alpha;
    e->tweens[slot].to_alpha = to_alpha;

    pw_event_new(L);
    e->tweens[slot].completed_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (duration <= 0.0f) {
        e->tweens[slot].active = false;
        e->tweens[slot].playback = PW_TW_COMPLETED;
        tween_apply(e, slot, 1.0f);
        tween_push_handle(L, e, slot);
        tween_complete(e, slot);
        return 1;
    }

    e->tweens[slot].active = true;
    e->tweens[slot].playback = PW_TW_PLAYING;
    tween_push_handle(L, e, slot);
    return 1;
}

static int lua_tween_module_cancel(lua_State* L) {
    int arg = client_mod_base(L);
    ObjProxy* part = (ObjProxy*)luaL_checkudata(L, arg + 1, "PW_Obj");
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !part) return 0;
    uint8_t bits = (uint8_t)(PW_TW_POS | PW_TW_ROT | PW_TW_SIZE | PW_TW_COLOR | PW_TW_ALPHA);
    if (lua_gettop(L) >= arg + 2 && lua_isstring(L, arg + 2)) {
        uint8_t one = tween_prop_bit(lua_tostring(L, arg + 2));
        if (one) bits = one;
    }
    tween_cancel_entity(e, part->entity, bits);
    return 0;
}

static int lua_tween_module_is_tweening(lua_State* L) {
    int arg = client_mod_base(L);
    ObjProxy* part = (ObjProxy*)luaL_checkudata(L, arg + 1, "PW_Obj");
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e || !part) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint8_t mask = (uint8_t)(PW_TW_POS | PW_TW_ROT | PW_TW_SIZE | PW_TW_COLOR | PW_TW_ALPHA);
    if (lua_gettop(L) >= arg + 2 && lua_isstring(L, arg + 2)) {
        uint8_t one = tween_prop_bit(lua_tostring(L, arg + 2));
        if (one) mask = one;
    }
    for (int i = 0; i < PW_MAX_TWEENS; i++) {
        if (!e->tweens[i].active) continue;
        if (e->tweens[i].entity != part->entity) continue;
        if (e->tweens[i].props & mask) {
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

static int lua_instance_new(lua_State* L) {
    const char* cls = luaL_checkstring(L, 1);
    if (strcmp(cls, "Part") == 0 || strcmp(cls, "Brick") == 0) {
        EntityID eid = client_script_spawn_local_part();
        if (eid == ENTITY_INVALID)
            return luaL_error(L, "Instance.new: could not create Part");
        push_obj_entity(L, get_engine_from_state(L), eid);
        return 1;
    }
    return luaL_error(L, "Instance.new: unsupported class '%s' on the client", cls);
}

static int lua_move_index(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    const char* key = luaL_checkstring(L, 2);
    if (!e) { lua_pushnil(L); return 1; }
    if (strcmp(key, "Gravity") == 0) { lua_pushnumber(L, e->move_gravity); return 1; }
    if (strcmp(key, "WalkSpeed") == 0) { lua_pushnumber(L, e->move_walk_speed); return 1; }
    if (strcmp(key, "JumpPower") == 0 || strcmp(key, "JumpImpulse") == 0) {
        lua_pushnumber(L, e->move_jump_impulse); return 1;
    }
    if (strcmp(key, "MovementMode") == 0) {
        lua_pushstring(L, e->move_mode == 1 ? "Scripted" : "Default");
        return 1;
    }
    if (strcmp(key, "CameraDistanceMin") == 0) { lua_pushnumber(L, e->cam_dist_min); return 1; }
    if (strcmp(key, "CameraDistanceMax") == 0) { lua_pushnumber(L, e->cam_dist_max); return 1; }
    lua_pushnil(L);
    return 1;
}

static int lua_move_newindex(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e) return 0;
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Gravity") == 0) {
        e->move_gravity = (float)luaL_checknumber(L, 3);
        return 0;
    }
    if (strcmp(key, "WalkSpeed") == 0) {
        float v = (float)luaL_checknumber(L, 3);
        if (v < 0.0f) v = 0.0f;
        e->move_walk_speed = v;
        return 0;
    }
    if (strcmp(key, "JumpPower") == 0 || strcmp(key, "JumpImpulse") == 0) {
        float v = (float)luaL_checknumber(L, 3);
        if (v < 0.0f) v = 0.0f;
        e->move_jump_impulse = v;
        return 0;
    }
    if (strcmp(key, "MovementMode") == 0) {
        if (lua_type(L, 3) == LUA_TNUMBER)
            e->move_mode = lua_tointeger(L, 3) == 1 ? 1 : 0;
        else
            e->move_mode = ieq_ascii(luaL_checkstring(L, 3), "Scripted") ? 1 : 0;
        return 0;
    }
    if (strcmp(key, "CameraDistanceMin") == 0) {
        float v = (float)luaL_checknumber(L, 3);
        if (v < 0.0f) v = 0.0f;
        e->cam_dist_min = v;
        return 0;
    }
    if (strcmp(key, "CameraDistanceMax") == 0) {
        float v = (float)luaL_checknumber(L, 3);
        if (v < 0.0f) v = 0.0f;
        e->cam_dist_max = v;
        return 0;
    }
    return luaL_error(L, "PlayerMovementModule: unknown property '%s'", key);
}

static void cam_current(ClientScriptEngine* e, Vec3* pos, float* yaw, float* pitch, float* roll, float* fov) {
    if (e->cam_override) {
        *pos = e->cam_pos;
        *yaw = e->cam_yaw;
        *pitch = e->cam_pitch;
        *roll = e->cam_roll;
        *fov = e->cam_fov;
    } else {
        *pos = e->cam_live_pos;
        *yaw = e->cam_live_yaw;
        *pitch = e->cam_live_pitch;
        *roll = e->cam_live_roll;
        *fov = e->cam_fov > 1.0f ? e->cam_fov : 60.0f;
    }
}

static int lua_cam_index(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    const char* key = luaL_checkstring(L, 2);
    if (!e) { lua_pushnil(L); return 1; }
    Vec3 pos;
    float yaw, pitch, roll, fov;
    cam_current(e, &pos, &yaw, &pitch, &roll, &fov);
    if (strcmp(key, "Override") == 0) { lua_pushboolean(L, e->cam_override); return 1; }
    if (strcmp(key, "Position") == 0) { push_vec3(L, pos.x, pos.y, pos.z); return 1; }
    if (strcmp(key, "Yaw") == 0) { lua_pushnumber(L, yaw); return 1; }
    if (strcmp(key, "Pitch") == 0) { lua_pushnumber(L, pitch); return 1; }
    if (strcmp(key, "Roll") == 0) { lua_pushnumber(L, roll); return 1; }
    if (strcmp(key, "FOV") == 0 || strcmp(key, "FieldOfView") == 0) {
        lua_pushnumber(L, fov); return 1;
    }
    if (strcmp(key, "Rotation") == 0) { push_vec3(L, yaw, pitch, roll); return 1; }
    lua_pushnil(L);
    return 1;
}

static int lua_cam_newindex(lua_State* L) {
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e) return 0;
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Override") == 0) {
        if (lua_toboolean(L, 3))
            cam_capture_from(e);
        else {
            e->cam_override = false;
            e->cam_tween = 0;
        }
        return 0;
    }
    if (strcmp(key, "FOV") == 0 || strcmp(key, "FieldOfView") == 0) {
        float v = (float)luaL_checknumber(L, 3);
        if (v < 1.0f) v = 1.0f;
        if (v > 120.0f) v = 120.0f;
        e->cam_fov = v;
        return 0;
    }
    cam_capture_from(e);
    if (strcmp(key, "Position") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &e->cam_pos.x);
        read_vec3_field(L, 3, "y", &e->cam_pos.y);
        read_vec3_field(L, 3, "z", &e->cam_pos.z);
        return 0;
    }
    if (strcmp(key, "Yaw") == 0) { e->cam_yaw = (float)luaL_checknumber(L, 3); return 0; }
    if (strcmp(key, "Pitch") == 0) { e->cam_pitch = (float)luaL_checknumber(L, 3); return 0; }
    if (strcmp(key, "Roll") == 0) { e->cam_roll = (float)luaL_checknumber(L, 3); return 0; }
    if (strcmp(key, "Rotation") == 0) {
        luaL_checktype(L, 3, LUA_TTABLE);
        read_vec3_field(L, 3, "x", &e->cam_yaw);
        read_vec3_field(L, 3, "y", &e->cam_pitch);
        read_vec3_field(L, 3, "z", &e->cam_roll);
        return 0;
    }
    return luaL_error(L, "CameraModule: unknown property '%s'", key);
}

static int lua_cam_reset(lua_State* L) {
    (void)client_mod_base(L);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e) return 0;
    e->cam_override = false;
    e->cam_tween = 0;
    e->cam_fov = 60.0f;
    e->cam_roll = 0.0f;
    return 0;
}

static int lua_cam_tween_cancel(lua_State* L) {
    (void)client_mod_base(L);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (e) e->cam_tween = 0;
    return 0;
}

static int lua_cam_tween_pause(lua_State* L) {
    (void)client_mod_base(L);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (e && e->cam_tween == 1) e->cam_tween = 2;
    return 0;
}

static int lua_cam_tween_play(lua_State* L) {
    (void)client_mod_base(L);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (e && e->cam_tween == 2) e->cam_tween = 1;
    return 0;
}

static int lua_cam_tween_handle_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (strcmp(key, "Cancel") == 0) { lua_pushcfunction(L, lua_cam_tween_cancel); return 1; }
    if (strcmp(key, "Pause") == 0) { lua_pushcfunction(L, lua_cam_tween_pause); return 1; }
    if (strcmp(key, "Play") == 0) { lua_pushcfunction(L, lua_cam_tween_play); return 1; }
    if (strcmp(key, "Completed") == 0) {
        if (e && e->cam_tween_completed_ref != LUA_NOREF && e->cam_tween_completed_ref != 0)
            lua_rawgeti(L, LUA_REGISTRYINDEX, e->cam_tween_completed_ref);
        else
            lua_pushnil(L);
        return 1;
    }
    if (strcmp(key, "PlaybackState") == 0) {
        const char* st = "Cancelled";
        if (e) {
            if (e->cam_tween == 1) st = "Playing";
            else if (e->cam_tween == 2) st = "Paused";
        }
        lua_pushstring(L, st);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int lua_cam_tween(lua_State* L) {
    int b = client_mod_base(L);
    ClientScriptEngine* e = get_engine_from_state(L);
    if (!e) { lua_pushnil(L); return 1; }
    luaL_checktype(L, b + 1, LUA_TTABLE);
    float duration = (float)luaL_checknumber(L, b + 2);
    if (duration < 0.0f) duration = 0.0f;
    const char* easing = luaL_optstring(L, b + 3, "Linear");
    int bits = 0;
    Vec3 pos = e->cam_override ? e->cam_pos : e->cam_live_pos;
    float yaw = e->cam_override ? e->cam_yaw : e->cam_live_yaw;
    float pitch = e->cam_override ? e->cam_pitch : e->cam_live_pitch;
    float roll = e->cam_override ? e->cam_roll : e->cam_live_roll;
    float fov = e->cam_fov > 1.0f ? e->cam_fov : 60.0f;
    lua_getfield(L, b + 1, "Position");
    if (lua_istable(L, -1)) {
        bits |= PW_CAM_TW_POS;
        read_vec3_field(L, -1, "x", &pos.x);
        read_vec3_field(L, -1, "y", &pos.y);
        read_vec3_field(L, -1, "z", &pos.z);
    }
    lua_pop(L, 1);
    lua_getfield(L, b + 1, "Rotation");
    if (lua_istable(L, -1)) {
        bits |= PW_CAM_TW_YAW | PW_CAM_TW_PITCH | PW_CAM_TW_ROLL;
        read_vec3_field(L, -1, "x", &yaw);
        read_vec3_field(L, -1, "y", &pitch);
        read_vec3_field(L, -1, "z", &roll);
    }
    lua_pop(L, 1);
    lua_getfield(L, b + 1, "Yaw");
    if (lua_isnumber(L, -1)) { bits |= PW_CAM_TW_YAW; yaw = (float)lua_tonumber(L, -1); }
    lua_pop(L, 1);
    lua_getfield(L, b + 1, "Pitch");
    if (lua_isnumber(L, -1)) { bits |= PW_CAM_TW_PITCH; pitch = (float)lua_tonumber(L, -1); }
    lua_pop(L, 1);
    lua_getfield(L, b + 1, "Roll");
    if (lua_isnumber(L, -1)) { bits |= PW_CAM_TW_ROLL; roll = (float)lua_tonumber(L, -1); }
    lua_pop(L, 1);
    lua_getfield(L, b + 1, "FOV");
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, b + 1, "FieldOfView");
    }
    if (lua_isnumber(L, -1)) {
        bits |= PW_CAM_TW_FOV;
        fov = (float)lua_tonumber(L, -1);
        if (fov < 1.0f) fov = 1.0f;
        if (fov > 120.0f) fov = 120.0f;
    }
    lua_pop(L, 1);
    if (bits == 0)
        return luaL_error(L, "CameraModule:Tween: expected Position, Rotation, Yaw, Pitch, Roll, or FOV");
    if (bits & (PW_CAM_TW_POS | PW_CAM_TW_YAW | PW_CAM_TW_PITCH | PW_CAM_TW_ROLL))
        cam_capture_from(e);
    e->cam_tween_from_pos = e->cam_override ? e->cam_pos : e->cam_live_pos;
    e->cam_tween_to_pos = pos;
    e->cam_tween_from_yaw = e->cam_override ? e->cam_yaw : e->cam_live_yaw;
    e->cam_tween_to_yaw = yaw;
    e->cam_tween_from_pitch = e->cam_override ? e->cam_pitch : e->cam_live_pitch;
    e->cam_tween_to_pitch = pitch;
    e->cam_tween_from_roll = e->cam_override ? e->cam_roll : e->cam_live_roll;
    e->cam_tween_to_roll = roll;
    e->cam_tween_from_fov = e->cam_fov > 1.0f ? e->cam_fov : 60.0f;
    e->cam_tween_to_fov = fov;
    e->cam_tween_bits = bits;
    e->cam_tween_dur = duration;
    e->cam_tween_t = 0.0f;
    e->cam_tween_ease = cam_parse_easing(easing);
    e->cam_tween = 1;
    if (duration <= 0.0f)
        cam_tick(e, 1.0f);
    lua_newuserdata(L, sizeof(int));
    luaL_setmetatable(L, "PW_CamTween");
    return 1;
}

ClientScriptEngine* client_script_create(Scene* scene) {
    ClientScriptEngine* e = (ClientScriptEngine*)calloc(1, sizeof(ClientScriptEngine));
    e->scene = scene;
    e->player = NULL;
    e->player_name[0] = '\0';
    e->next_id = 1;
    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++)
        e->part_touch[i].entity = ENTITY_INVALID;
    e->move_gravity = -190.0f;
    e->move_walk_speed = 16.0f;
    e->move_jump_impulse = 60.0f;
    e->move_mode = 0;
    e->cam_dist_min = 0.0f;
    e->cam_dist_max = 20.0f;
    e->cam_fov = 60.0f;
    e->cam_live_fov = 60.0f;
    e->cam_tween_completed_ref = LUA_NOREF;
    e->L = luaL_newstate();
    luaL_openlibs(e->L);

    lua_pushlightuserdata(e->L, e);
    lua_setfield(e->L, LUA_REGISTRYINDEX, "_pw_engine");

    luaL_newmetatable(e->L, "PW_Obj");
    lua_pushcfunction(e->L, obj_index); lua_setfield(e->L, -2, "__index");
    lua_pushcfunction(e->L, obj_newindex); lua_setfield(e->L, -2, "__newindex");
    lua_pop(e->L, 1);

    luaL_newmetatable(e->L, "PW_CamTween");
    lua_pushcfunction(e->L, lua_cam_tween_handle_index); lua_setfield(e->L, -2, "__index");
    lua_pop(e->L, 1);

    if (luaL_newmetatable(e->L, "PW_Tween")) {
        lua_pushcfunction(e->L, lua_tween_handle_index);
        lua_setfield(e->L, -2, "__index");
    }
    lua_pop(e->L, 1);
    e->next_tween_gen = 1;

    lua_pushcfunction(e->L, lua_wait); lua_setglobal(e->L, "wait");
    lua_pushcfunction(e->L, lua_print_override); lua_setglobal(e->L, "print");
    lua_pushcfunction(e->L, lua_warn_override); lua_setglobal(e->L, "warn");
    lua_pushcfunction(e->L, lua_fire_server); lua_setglobal(e->L, "FireServer");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_rgbcolor_new); lua_setfield(e->L, -2, "new");
    lua_pushcfunction(e->L, lua_rgbcolor_random); lua_setfield(e->L, -2, "random");
    lua_pushvalue(e->L, -1);
    lua_setglobal(e->L, "RGBColor");
    lua_setglobal(e->L, "Color");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_vector3_new); lua_setfield(e->L, -2, "new");
    lua_setglobal(e->L, "Vector3");

    lua_newtable(e->L);
    for (int i = 0; i < PART_MATERIAL_COUNT; i++) {
        const char* n = part_material_name((uint8_t)i);
        lua_pushstring(e->L, n);
        lua_setfield(e->L, -2, n);
    }
    lua_setglobal(e->L, "PartMaterial");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_explosion_new); lua_setfield(e->L, -2, "new");
    lua_setglobal(e->L, "Explosion");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_ui_notify); lua_setfield(e->L, -2, "notify");
    lua_pushcfunction(e->L, lua_ui_hud); lua_setfield(e->L, -2, "hud");
    lua_setglobal(e->L, "UI");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_player_gethealth); lua_setfield(e->L, -2, "GetHealth");
    lua_pushcfunction(e->L, lua_player_getposition); lua_setfield(e->L, -2, "GetPosition");
    lua_pushcfunction(e->L, lua_player_setposition); lua_setfield(e->L, -2, "SetPosition");
    lua_pushcfunction(e->L, lua_player_notify); lua_setfield(e->L, -2, "Notify");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_player_index_full);
    lua_setfield(e->L, -2, "__index");
    lua_pushcfunction(e->L, lua_player_newindex);
    lua_setfield(e->L, -2, "__newindex");
    lua_setmetatable(e->L, -2);
    lua_setglobal(e->L, "player");

    lua_newtable(e->L);
    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_char_index);
    lua_setfield(e->L, -2, "__index");
    lua_pushcfunction(e->L, lua_char_newindex);
    lua_setfield(e->L, -2, "__newindex");
    lua_setmetatable(e->L, -2);
    lua_pushvalue(e->L, -1);
    lua_setglobal(e->L, "Character");
    lua_getglobal(e->L, "player");
    lua_pushvalue(e->L, -2);
    lua_setfield(e->L, -2, "Character");
    lua_pop(e->L, 2);

    pw_event_open(e->L);
    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_message_to_server);
    lua_setfield(e->L, -2, "MessageToServer");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->message_from_server_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "MessageFromServer");
    lua_setglobal(e->L, "MessageModule");

    lua_newtable(e->L);
    lua_pushboolean(e->L, 0);
    lua_setfield(e->L, -2, "IsServer");
    lua_pushboolean(e->L, 1);
    lua_setfield(e->L, -2, "IsClient");
    lua_pushboolean(e->L, e->is_playtest ? 1 : 0);
    lua_setfield(e->L, -2, "IsPlaytest");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->run_frame_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "Frame");
    lua_setglobal(e->L, "RunModule");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_input_is_key_down); lua_setfield(e->L, -2, "IsKeyDown");
    lua_pushcfunction(e->L, lua_input_is_mouse_down); lua_setfield(e->L, -2, "IsMouseButtonDown");
    lua_pushcfunction(e->L, lua_input_get_builtin); lua_setfield(e->L, -2, "GetBuiltinButton");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->input_began_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "InputBegan");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->input_ended_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "InputEnded");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->input_changed_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "InputChanged");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->builtin_pressed_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "BuiltinButtonPressed");
    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_input_index);
    lua_setfield(e->L, -2, "__index");
    lua_setmetatable(e->L, -2);
    lua_setglobal(e->L, "InputModule");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_ray_make_params); lua_setfield(e->L, -2, "MakeRaycastParams");
    lua_pushcfunction(e->L, lua_ray_cast); lua_setfield(e->L, -2, "Raycast");
    lua_pushcfunction(e->L, lua_ray_screen_to_world); lua_setfield(e->L, -2, "ScreenToWorld");
    lua_pushcfunction(e->L, lua_ray_point_to_screen); lua_setfield(e->L, -2, "PointToScreen");
    lua_setglobal(e->L, "RaycastModule");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_instance_new); lua_setfield(e->L, -2, "new");
    lua_setglobal(e->L, "Instance");

    lua_newtable(e->L);
    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_move_index); lua_setfield(e->L, -2, "__index");
    lua_pushcfunction(e->L, lua_move_newindex); lua_setfield(e->L, -2, "__newindex");
    lua_setmetatable(e->L, -2);
    lua_setglobal(e->L, "PlayerMovementModule");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_cam_tween); lua_setfield(e->L, -2, "Tween");
    lua_pushcfunction(e->L, lua_cam_reset); lua_setfield(e->L, -2, "Reset");
    pw_event_new(e->L);
    lua_pushvalue(e->L, -1);
    e->cam_tween_completed_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);
    lua_setfield(e->L, -2, "Completed");
    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_cam_index); lua_setfield(e->L, -2, "__index");
    lua_pushcfunction(e->L, lua_cam_newindex); lua_setfield(e->L, -2, "__newindex");
    lua_setmetatable(e->L, -2);
    lua_setglobal(e->L, "CameraModule");

    lua_newtable(e->L);
    lua_pushcfunction(e->L, lua_tween_module_tween); lua_setfield(e->L, -2, "Tween");
    lua_pushcfunction(e->L, lua_tween_module_cancel); lua_setfield(e->L, -2, "Cancel");
    lua_pushcfunction(e->L, lua_tween_module_is_tweening); lua_setfield(e->L, -2, "IsTweening");
    lua_setglobal(e->L, "TweenModule");

    pw_task_open(e->L);
    pw_task_push_lib(e->L);
    lua_setglobal(e->L, "task");

    return e;
}

void client_script_destroy(ClientScriptEngine* e) {
    if (!e) return;
    if (e->L) lua_close(e->L);
    free(e);
}

void client_script_set_player(ClientScriptEngine* e, Avatar* avatar) {
    if (e) e->player = avatar;
}

void client_script_set_local_name(ClientScriptEngine* e, const char* name) {
    if (!e) return;
    if (!name) { e->player_name[0] = '\0'; return; }
    strncpy(e->player_name, name, sizeof(e->player_name) - 1);
    e->player_name[sizeof(e->player_name) - 1] = '\0';
}

void client_script_set_playtest(ClientScriptEngine* e, bool playtest) {
    if (!e) return;
    e->is_playtest = playtest;
    if (!e->L) return;
    lua_getglobal(e->L, "RunModule");
    if (lua_istable(e->L, -1)) {
        lua_pushboolean(e->L, playtest ? 1 : 0);
        lua_setfield(e->L, -2, "IsPlaytest");
    }
    lua_pop(e->L, 1);
}

void client_script_fire_touched(ClientScriptEngine* e, EntityID entity) {
    if (!e) return;
    lua_State* L = e->L;

    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity != entity) continue;
        e->part_touch[i].touched_this_frame = true;
        if (!e->part_touch[i].is_touching) {
            e->part_touch[i].is_touching = true;
            lua_rawgeti(L, LUA_REGISTRYINDEX, e->part_touch[i].touched_ref);
            if (luaL_testudata(L, -1, "PW_Event")) {
                lua_getglobal(L, "player");
                pw_event_fire(L, -2, 1);
            }
            lua_pop(L, 1);
        }
        break;
    }

    for (int i = 0; i < MAX_CLIENT_SCRIPTS; i++) {
        ClientScript* s = &e->scripts[i];
        if (!s->co) continue;
        if (s->parent_entity != entity) continue;

        s->touched_this_frame = true;
        if (s->is_touching) continue;
        s->is_touching = true;

        lua_rawgeti(L, LUA_REGISTRYINDEX, s->env_ref);
        lua_pushstring(L, "Touched");
        lua_rawget(L, -2);
        lua_remove(L, -2);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        lua_getglobal(L, "player");

        if (pw_task_spawn_from_stack(L, 1) == 1)
            lua_pop(L, 1);
    }
}

void client_script_fire_clicked(ClientScriptEngine* e, EntityID entity) {
    if (!e || entity == ENTITY_INVALID) return;
    lua_State* L = e->L;

    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity != entity) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, e->part_touch[i].clicked_ref);
        if (luaL_testudata(L, -1, "PW_Event")) {
            lua_getglobal(L, "player");
            pw_event_fire(L, -2, 1);
        }
        lua_pop(L, 1);
        break;
    }

    for (int i = 0; i < MAX_CLIENT_SCRIPTS; i++) {
        ClientScript* s = &e->scripts[i];
        if (!s->co) continue;
        if (s->parent_entity != entity) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->env_ref);
        lua_pushstring(L, "Clicked");
        lua_rawget(L, -2);
        lua_remove(L, -2);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        lua_getglobal(L, "player");
        if (pw_task_spawn_from_stack(L, 1) == 1)
            lua_pop(L, 1);
    }
}

bool client_script_part_has_clicked(ClientScriptEngine* e, EntityID entity) {
    if (!e || entity == ENTITY_INVALID) return false;
    lua_State* L = e->L;
    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity != entity) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, e->part_touch[i].clicked_ref);
        int n = pw_event_listener_count(L, -1);
        lua_pop(L, 1);
        if (n > 0) return true;
        break;
    }
    for (int i = 0; i < MAX_CLIENT_SCRIPTS; i++) {
        ClientScript* s = &e->scripts[i];
        if (!s->co || s->parent_entity != entity) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->env_ref);
        lua_pushstring(L, "Clicked");
        lua_rawget(L, -2);
        bool fn = lua_isfunction(L, -1);
        lua_pop(L, 2);
        if (fn) return true;
    }
    return false;
}

uint32_t client_script_load(ClientScriptEngine* e, EntityID parent_entity, const char* source) {
    if (e->count >= MAX_CLIENT_SCRIPTS) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_CLIENT_SCRIPTS; i++) {
        if (!e->scripts[i].co) { slot = i; break; }
    }
    if (slot < 0) return 0;

    lua_State* co = lua_newthread(e->L);
    int co_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);

    lua_newtable(co);
    lua_newtable(co);
    lua_pushglobaltable(co);
    lua_setfield(co, -2, "__index");
    lua_pushcfunction(co, client_env_newindex);
    lua_setfield(co, -2, "__newindex");
    lua_setmetatable(co, -2);

    lua_newtable(co);
    ObjProxy* proxy = (ObjProxy*)lua_newuserdata(co, sizeof(ObjProxy));
    proxy->scene = e->scene;
    proxy->entity = parent_entity;
    luaL_setmetatable(co, "PW_Obj");
    lua_pushvalue(co, -1);
    lua_setfield(co, -3, "parent");
    lua_setfield(co, -2, "Parent");
    if (parent_entity != ENTITY_INVALID)
        client_part_touch_ensure(e, parent_entity);
    lua_setfield(co, -2, "script");

    int env_idx = lua_gettop(co);

    if (luaL_loadstring(co, source) != LUA_OK) {
        PW_ERR(ERR_GENERIC, "LocalScript compile failed: %s\n", lua_tostring(co, -1));
        lua_pop(co, 2);
        luaL_unref(e->L, LUA_REGISTRYINDEX, co_ref);
        return 0;
    }

    lua_pushvalue(co, env_idx);
    if (!lua_setupvalue(co, -2, 1)) {
        PW_ERR(ERR_GENERIC, "LocalScript _ENV setup failed\n");
        lua_pop(co, 2);
        luaL_unref(e->L, LUA_REGISTRYINDEX, co_ref);
        return 0;
    }

    lua_pushvalue(co, env_idx);
    lua_xmove(co, e->L, 1);
    int env_ref = luaL_ref(e->L, LUA_REGISTRYINDEX);

    lua_remove(co, env_idx);

    ClientScript* s = &e->scripts[slot];
    s->id = e->next_id++;
    s->parent_entity = parent_entity;
    s->co = co;
    s->co_ref = co_ref;
    s->env_ref = env_ref;
    s->wait_timer = 0;
    s->wait_duration = -1.0f;
    s->running = true;
    s->dead = false;
    s->is_touching = false;
    s->touched_this_frame = false;
    e->count++;
    return s->id;
}

void client_script_tick(ClientScriptEngine* e, float dt) {
    if (!e) return;
    lua_State* L = e->L;
    const InputState* in = input_get_state();

    if (dt > 0.0f) {
        cam_tick(e, dt);
        tween_tick(e, dt);
    }

    if (e->input_began_ref != 0 && e->input_began_ref != LUA_NOREF) {
        bool keys[256];
        bool mouse[3];
        for (int i = 0; i < 256; i++) keys[i] = input_key_held(i);
        mouse[0] = input_mouse_button_held(0);
        mouse[1] = input_mouse_button_held(1);
        mouse[2] = input_mouse_button_held(2);
        float mx = in ? in->mouse_x : 0, my = in ? in->mouse_y : 0;
        float mdx = in ? in->mouse_dx : 0, mdy = in ? in->mouse_dy : 0;
        if (!e->input_prev_inited) {
            memcpy(e->input_prev_keys, keys, sizeof(keys));
            memcpy(e->input_prev_mouse, mouse, sizeof(mouse));
            e->input_prev_mx = mx;
            e->input_prev_my = my;
            e->input_prev_inited = true;
        } else {
            for (int i = 0; i < 256; i++) {
                if (keys[i] == e->input_prev_keys[i]) continue;
                const char* kn = key_name_from_code(i);
                int ref = keys[i] ? e->input_began_ref : e->input_ended_ref;
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                if (luaL_testudata(L, -1, "PW_Event")) {
                    push_input_info(L, kn, -1, mx, my, 0, 0, 0);
                    pw_event_fire(L, -2, 1);
                }
                lua_pop(L, 1);
            }
            for (int b = 0; b < 3; b++) {
                if (mouse[b] == e->input_prev_mouse[b]) continue;
                int ref = mouse[b] ? e->input_began_ref : e->input_ended_ref;
                lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                if (luaL_testudata(L, -1, "PW_Event")) {
                    push_input_info(L, NULL, b, mx, my, 0, 0, 0);
                    pw_event_fire(L, -2, 1);
                }
                lua_pop(L, 1);
            }
            if (mdx != 0.0f || mdy != 0.0f || mx != e->input_prev_mx || my != e->input_prev_my) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, e->input_changed_ref);
                if (luaL_testudata(L, -1, "PW_Event")) {
                    push_input_info(L, NULL, -1, mx, my, mdx, mdy, 0);
                    pw_event_fire(L, -2, 1);
                }
                lua_pop(L, 1);
            }
            if (in && in->key_space && e->builtin_pressed_ref != 0) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, e->builtin_pressed_ref);
                if (luaL_testudata(L, -1, "PW_Event")) {
                    lua_pushstring(L, "Jump");
                    pw_event_fire(L, -2, 1);
                }
                lua_pop(L, 1);
            }
            memcpy(e->input_prev_keys, keys, sizeof(keys));
            memcpy(e->input_prev_mouse, mouse, sizeof(mouse));
            e->input_prev_mx = mx;
            e->input_prev_my = my;
        }
    }

    if (dt > 0.0f && e->run_frame_ref != LUA_NOREF && e->run_frame_ref != 0) {
        lua_State* L = e->L;
        lua_rawgeti(L, LUA_REGISTRYINDEX, e->run_frame_ref);
        if (luaL_testudata(L, -1, "PW_Event")) {
            lua_pushnumber(L, (double)dt);
            pw_event_fire(L, -2, 1);
        }
        lua_pop(L, 1);
    }

    pw_task_tick(e->L, dt);

    for (int i = 0; i < PW_MAX_CLIENT_PART_TOUCH; i++) {
        if (e->part_touch[i].entity == ENTITY_INVALID) continue;
        if (e->part_touch[i].is_touching && !e->part_touch[i].touched_this_frame) {
            e->part_touch[i].is_touching = false;
            lua_rawgeti(e->L, LUA_REGISTRYINDEX, e->part_touch[i].touchended_ref);
            if (luaL_testudata(e->L, -1, "PW_Event")) {
                lua_getglobal(e->L, "player");
                pw_event_fire(e->L, -2, 1);
            }
            lua_pop(e->L, 1);
        }
        e->part_touch[i].touched_this_frame = false;
    }

    for (int i = 0; i < MAX_CLIENT_SCRIPTS; i++) {
        ClientScript* s = &e->scripts[i];
        if (!s->co) continue;

        if (!s->touched_this_frame) s->is_touching = false;
        s->touched_this_frame = false;

        if (!s->running || s->dead) continue;

        if (s->wait_timer > 0.0f) { s->wait_timer -= dt; continue; }

        if (pw_event_co_waiting(e->L, s->co))
            continue;

        int nres = 0;
        int status;
        if (s->wait_duration >= 0.0f) {
            lua_pushnumber(s->co, (double)s->wait_duration);
            s->wait_duration = -1.0f;
            status = lua_resume(s->co, e->L, 1, &nres);
        } else {
            status = lua_resume(s->co, e->L, 0, &nres);
        }

        if (status == LUA_YIELD) {
            lua_pushlightuserdata(s->co, (void*)s->co);
            lua_gettable(s->co, LUA_REGISTRYINDEX);
            if (lua_isnumber(s->co, -1)) {
                s->wait_timer = (float)lua_tonumber(s->co, -1);
                s->wait_duration = s->wait_timer;
                lua_pushlightuserdata(s->co, (void*)s->co);
                lua_pushnil(s->co);
                lua_settable(s->co, LUA_REGISTRYINDEX);
            } else {
                s->wait_duration = -1.0f;
            }
            lua_pop(s->co, nres + 1);
        } else if (status == LUA_OK) {

            lua_rawgeti(e->L, LUA_REGISTRYINDEX, s->env_ref);
            lua_pushstring(e->L, "Touched");
            lua_rawget(e->L, -2);
            bool has_touched = lua_isfunction(e->L, -1);
            lua_pop(e->L, 1);
            lua_pushstring(e->L, "Clicked");
            lua_rawget(e->L, -2);
            bool has_clicked = lua_isfunction(e->L, -1);
            lua_pop(e->L, 2);

            s->dead = true; s->running = false; e->count--;
            if (!has_touched && !has_clicked) {
                luaL_unref(e->L, LUA_REGISTRYINDEX, s->co_ref);
                luaL_unref(e->L, LUA_REGISTRYINDEX, s->env_ref);
                s->co = NULL;
            }
        } else {
            PW_ERR(ERR_GENERIC, "LocalScript error: %s\n", lua_tostring(s->co, -1));
            lua_pop(s->co, 1);
            s->dead = true; s->running = false; e->count--;
            luaL_unref(e->L, LUA_REGISTRYINDEX, s->co_ref);
            luaL_unref(e->L, LUA_REGISTRYINDEX, s->env_ref);
            s->co = NULL;
        }
    }
}

void client_script_fire_message_from_server(ClientScriptEngine* e, const char* name,
                                            const uint8_t* data, size_t data_len) {
    if (!e || !e->L || !name || !name[0]) return;
    if (e->message_from_server_ref == LUA_NOREF || e->message_from_server_ref == 0)
        return;
    lua_State* L = e->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, e->message_from_server_ref);
    if (!luaL_testudata(L, -1, "PW_Event")) {
        lua_pop(L, 1);
        return;
    }
    lua_pushstring(L, name);
    lua_newtable(L);
    if (data && data_len >= 24) {
        float vals[6];
        memcpy(vals, data, 24);
        lua_pushnumber(L, vals[0]); lua_setfield(L, -2, "dir_x");
        lua_pushnumber(L, vals[1]); lua_setfield(L, -2, "dir_y");
        lua_pushnumber(L, vals[2]); lua_setfield(L, -2, "dir_z");
        lua_pushnumber(L, vals[3]); lua_setfield(L, -2, "origin_x");
        lua_pushnumber(L, vals[4]); lua_setfield(L, -2, "origin_y");
        lua_pushnumber(L, vals[5]); lua_setfield(L, -2, "origin_z");
    }
    if (data && data_len >= 36) {
        float hit[3];
        memcpy(hit, data + 24, 12);
        lua_pushnumber(L, hit[0]); lua_setfield(L, -2, "hit_x");
        lua_pushnumber(L, hit[1]); lua_setfield(L, -2, "hit_y");
        lua_pushnumber(L, hit[2]); lua_setfield(L, -2, "hit_z");
    }
    lua_pushinteger(L, (lua_Integer)data_len);
    lua_setfield(L, -2, "data_len");
    pw_event_fire(L, -3, 2);
    lua_pop(L, 1);
}

static int lua_explosion_new(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    float radius = (float)luaL_optnumber(L, 4, 5.0);

    extern void spawn_explosion(float, float, float, float);
    spawn_explosion(x, y, z, radius);
    return 0;
}

void client_script_apply_avatar(ClientScriptEngine* e, Avatar* av, Camera* cam) {
    if (!e) return;
    if (av) {
        av->gravity = e->move_gravity;
        av->walk_speed = e->move_walk_speed;
        av->jump_impulse = e->move_jump_impulse;
        av->scripted_movement = (e->move_mode == 1);
    }
    if (cam) {
        cam->distance_min = e->cam_dist_min;
        cam->distance_max = e->cam_dist_max;
    }
}

void client_script_set_camera_live(ClientScriptEngine* e,
                                   float x, float y, float z,
                                   float yaw, float pitch, float roll, float fov) {
    if (!e) return;
    e->cam_live_pos = (Vec3){ x, y, z };
    e->cam_live_yaw = yaw;
    e->cam_live_pitch = pitch;
    e->cam_live_roll = roll;
    if (fov > 1.0f)
        e->cam_live_fov = fov;
}

int client_script_camera_override(ClientScriptEngine* e,
                                  float* x, float* y, float* z,
                                  float* yaw, float* pitch, float* roll, float* fov) {
    if (!e || !e->cam_override) return 0;
    if (x) *x = e->cam_pos.x;
    if (y) *y = e->cam_pos.y;
    if (z) *z = e->cam_pos.z;
    if (yaw) *yaw = e->cam_yaw;
    if (pitch) *pitch = e->cam_pitch;
    if (roll) *roll = e->cam_roll;
    if (fov) *fov = e->cam_fov > 1.0f ? e->cam_fov : 60.0f;
    return 1;
}

float client_script_camera_fov(ClientScriptEngine* e) {
    if (!e) return 60.0f;
    float fov = e->cam_fov;
    if (fov < 1.0f) fov = 60.0f;
    if (fov > 120.0f) fov = 120.0f;
    return fov;
}
