/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: lua_pw_event.h                                                                      |
|   Purpose: Lua events (BindableEvent-ish)                                                   |
\*-------------------------------------------------------------------------------------------*/

#ifndef LUA_PW_EVENT_H
#define LUA_PW_EVENT_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

void pw_event_open(lua_State* L);
int pw_event_new(lua_State* L);
int pw_event_fire(lua_State* L, int event_idx, int nargs);
int pw_event_co_waiting(lua_State* L, lua_State* co);
int pw_event_listener_count(lua_State* L, int event_idx);

#ifdef __cplusplus
}
#endif

#endif
