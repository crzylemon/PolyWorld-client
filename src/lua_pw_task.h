/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: lua_pw_task.h                                                                       |
|   Purpose: Lua tasks and stuff more stuff AAAGHGHHHHHH                                      |
\*-------------------------------------------------------------------------------------------*/

#ifndef LUA_PW_TASK_H
#define LUA_PW_TASK_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

void pw_task_open(lua_State* L);
int pw_task_push_lib(lua_State* L);
void pw_task_tick(lua_State* L, float dt);
int pw_task_spawn_from_stack(lua_State* L, int nargs);
void pw_task_sync_yield(lua_State* L, lua_State* co);
int pw_task_owns_co(lua_State* L, lua_State* co);
void pw_lua_watchdog_arm(lua_State* L);
void pw_lua_watchdog_disarm(lua_State* L);
int pw_lua_resume(lua_State* co, lua_State* from, int nargs, int* nresults);
int pw_lua_pcall(lua_State* L, int nargs, int nresults, int msgh);

#ifdef __cplusplus
}
#endif

#endif
