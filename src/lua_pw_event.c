/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: lua_pw_event.c                                                                      |
|   Purpose: Lua events (BindableEvent-ish)                                                   |
\*-------------------------------------------------------------------------------------------*/

#include "lua_pw_event.h"
#include "lua_pw_task.h"

#include "lauxlib.h"

#include <stdio.h>
#include <string.h>

#define PW_EVENT_MAX_CONN 64
#define PW_EVENT_MAX_WAIT 32
#define PW_EVENT_MT       "PW_Event"
#define PW_CONN_MT        "PW_Connection"
#define PW_WAIT_REG       "_pw_event_waits"

typedef struct {
    int fn_ref;
    int once;
    int connected;
} PwConnSlot;

typedef struct {
    int thread_ref;
} PwWaitSlot;

typedef struct {
    PwConnSlot conns[PW_EVENT_MAX_CONN];
    PwWaitSlot waits[PW_EVENT_MAX_WAIT];
} PwEvent;

typedef struct {
    int event_ref;
    int slot;
} PwConnection;

static PwEvent* check_event(lua_State* L, int idx) {
    return (PwEvent*)luaL_checkudata(L, idx, PW_EVENT_MT);
}

static void ensure_wait_reg(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, PW_WAIT_REG);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, PW_WAIT_REG);
    }
}

static void mark_waiting(lua_State* L, lua_State* co, int waiting) {
    ensure_wait_reg(L);
    lua_pushlightuserdata(L, (void*)co);
    if (waiting)
        lua_pushboolean(L, 1);
    else
        lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

int pw_event_listener_count(lua_State* L, int event_idx) {
    if (!L) return 0;
    PwEvent* ev = (PwEvent*)luaL_testudata(L, event_idx, PW_EVENT_MT);
    if (!ev) return 0;
    int n = 0;
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++) {
        if (ev->conns[i].connected && ev->conns[i].fn_ref != LUA_NOREF)
            n++;
    }
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++) {
        if (ev->waits[i].thread_ref != LUA_NOREF)
            n++;
    }
    return n;
}

int pw_event_co_waiting(lua_State* L, lua_State* co) {
    if (!L || !co) return 0;
    lua_getfield(L, LUA_REGISTRYINDEX, PW_WAIT_REG);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushlightuserdata(L, (void*)co);
    lua_gettable(L, -2);
    int waiting = lua_toboolean(L, -1);
    lua_pop(L, 2);
    return waiting;
}

static int conn_disconnect(lua_State* L) {
    PwConnection* c = (PwConnection*)luaL_checkudata(L, 1, PW_CONN_MT);
    if (c->event_ref == LUA_NOREF || c->slot < 0 || c->slot >= PW_EVENT_MAX_CONN)
        return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->event_ref);
    PwEvent* ev = (PwEvent*)luaL_testudata(L, -1, PW_EVENT_MT);
    if (ev) {
        PwConnSlot* slot = &ev->conns[c->slot];
        if (slot->connected && slot->fn_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, slot->fn_ref);
            slot->fn_ref = LUA_NOREF;
            slot->connected = 0;
            slot->once = 0;
        }
    }
    lua_pop(L, 1);
    c->slot = -1;
    return 0;
}

static int conn_index(lua_State* L) {
    PwConnection* c = (PwConnection*)luaL_checkudata(L, 1, PW_CONN_MT);
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Disconnect") == 0) {
        lua_pushcfunction(L, conn_disconnect);
        return 1;
    }
    if (strcmp(key, "Connected") == 0) {
        int connected = 0;
        if (c->event_ref != LUA_NOREF && c->slot >= 0 && c->slot < PW_EVENT_MAX_CONN) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, c->event_ref);
            PwEvent* ev = (PwEvent*)luaL_testudata(L, -1, PW_EVENT_MT);
            if (ev)
                connected = ev->conns[c->slot].connected &&
                            ev->conns[c->slot].fn_ref != LUA_NOREF;
            lua_pop(L, 1);
        }
        lua_pushboolean(L, connected);
        return 1;
    }
    return 0;
}

static int push_connection(lua_State* L, int event_ref, int slot) {
    PwConnection* c = (PwConnection*)lua_newuserdatauv(L, sizeof(PwConnection), 0);
    c->event_ref = event_ref;
    c->slot = slot;
    luaL_getmetatable(L, PW_CONN_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int event_connect_impl(lua_State* L, int once) {
    PwEvent* ev = check_event(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    int slot = -1;
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++) {
        if (ev->conns[i].fn_ref == LUA_NOREF) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return luaL_error(L, "Event:Connect -- too many connections (max %d)", PW_EVENT_MAX_CONN);

    lua_pushvalue(L, 2);
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ev->conns[slot].fn_ref = fn_ref;
    ev->conns[slot].once = once;
    ev->conns[slot].connected = 1;

    lua_pushvalue(L, 1);
    int event_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return push_connection(L, event_ref, slot);
}

static int event_connect(lua_State* L) {
    return event_connect_impl(L, 0);
}

static int event_once(lua_State* L) {
    return event_connect_impl(L, 1);
}

static int event_disconnect_all(lua_State* L) {
    PwEvent* ev = check_event(L, 1);
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++) {
        if (ev->conns[i].fn_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ev->conns[i].fn_ref);
            ev->conns[i].fn_ref = LUA_NOREF;
            ev->conns[i].connected = 0;
            ev->conns[i].once = 0;
        }
    }
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++) {
        if (ev->waits[i].thread_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ev->waits[i].thread_ref);
            if (lua_isthread(L, -1)) {
                lua_State* co = lua_tothread(L, -1);
                mark_waiting(L, co, 0);
            }
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, ev->waits[i].thread_ref);
            ev->waits[i].thread_ref = LUA_NOREF;
        }
    }
    return 0;
}

static int event_wait(lua_State* L) {
    PwEvent* ev = check_event(L, 1);
    if (lua_pushthread(L) == 1) {
        lua_pop(L, 1);
        return luaL_error(L, "Event:Wait cannot be called on the main thread");
    }

    int slot = -1;
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++) {
        if (ev->waits[i].thread_ref == LUA_NOREF) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        lua_pop(L, 1);
        return luaL_error(L, "Event:Wait: too many waiters (max %d)", PW_EVENT_MAX_WAIT);
    }

    ev->waits[slot].thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    mark_waiting(L, L, 1);
    return lua_yield(L, 0);
}

static int event_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (strcmp(key, "Connect") == 0) {
        lua_pushcfunction(L, event_connect);
        return 1;
    }
    if (strcmp(key, "Once") == 0) {
        lua_pushcfunction(L, event_once);
        return 1;
    }
    if (strcmp(key, "Wait") == 0) {
        lua_pushcfunction(L, event_wait);
        return 1;
    }
    if (strcmp(key, "DisconnectAll") == 0) {
        lua_pushcfunction(L, event_disconnect_all);
        return 1;
    }
    return 0;
}

static int event_gc(lua_State* L) {
    PwEvent* ev = check_event(L, 1);
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++) {
        if (ev->conns[i].fn_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ev->conns[i].fn_ref);
            ev->conns[i].fn_ref = LUA_NOREF;
        }
    }
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++) {
        if (ev->waits[i].thread_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ev->waits[i].thread_ref);
            if (lua_isthread(L, -1))
                mark_waiting(L, lua_tothread(L, -1), 0);
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, ev->waits[i].thread_ref);
            ev->waits[i].thread_ref = LUA_NOREF;
        }
    }
    return 0;
}

static int conn_gc(lua_State* L) {
    PwConnection* c = (PwConnection*)luaL_checkudata(L, 1, PW_CONN_MT);
    if (c->event_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, c->event_ref);
        c->event_ref = LUA_NOREF;
    }
    return 0;
}

void pw_event_open(lua_State* L) {
    if (!L) return;
    if (luaL_newmetatable(L, PW_EVENT_MT)) {
        lua_pushcfunction(L, event_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, event_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, PW_CONN_MT)) {
        lua_pushcfunction(L, conn_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, conn_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    ensure_wait_reg(L);
    lua_pop(L, 1);
}

int pw_event_new(lua_State* L) {
    pw_event_open(L);
    PwEvent* ev = (PwEvent*)lua_newuserdatauv(L, sizeof(PwEvent), 0);
    memset(ev, 0, sizeof(PwEvent));
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++)
        ev->conns[i].fn_ref = LUA_NOREF;
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++)
        ev->waits[i].thread_ref = LUA_NOREF;
    luaL_getmetatable(L, PW_EVENT_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static void copy_args(lua_State* L, int first_arg, int nargs) {
    for (int i = 0; i < nargs; i++)
        lua_pushvalue(L, first_arg + i);
}

int pw_event_fire(lua_State* L, int event_idx, int nargs) {
    if (!L || nargs < 0) return 0;
    event_idx = lua_absindex(L, event_idx);
    PwEvent* ev = (PwEvent*)luaL_testudata(L, event_idx, PW_EVENT_MT);
    if (!ev) {
        if (nargs > 0)
            lua_pop(L, nargs);
        return 0;
    }

    int first_arg = event_idx + 1;
    int fn_refs[PW_EVENT_MAX_CONN];
    int once_flags[PW_EVENT_MAX_CONN];
    int nfire = 0;
    for (int i = 0; i < PW_EVENT_MAX_CONN; i++) {
        if (ev->conns[i].connected && ev->conns[i].fn_ref != LUA_NOREF) {
            fn_refs[nfire] = ev->conns[i].fn_ref;
            once_flags[nfire] = ev->conns[i].once;
            if (ev->conns[i].once) {
                ev->conns[i].connected = 0;
                ev->conns[i].fn_ref = LUA_NOREF;
                ev->conns[i].once = 0;
            }
            nfire++;
        }
    }

    for (int i = 0; i < nfire; i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, fn_refs[i]);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            if (once_flags[i])
                luaL_unref(L, LUA_REGISTRYINDEX, fn_refs[i]);
            continue;
        }
        copy_args(L, first_arg, nargs);

        if (pw_task_spawn_from_stack(L, nargs) == 1)
            lua_pop(L, 1);
        if (once_flags[i])
            luaL_unref(L, LUA_REGISTRYINDEX, fn_refs[i]);
    }

    int wait_refs[PW_EVENT_MAX_WAIT];
    int nwait = 0;
    for (int i = 0; i < PW_EVENT_MAX_WAIT; i++) {
        if (ev->waits[i].thread_ref != LUA_NOREF) {
            wait_refs[nwait++] = ev->waits[i].thread_ref;
            ev->waits[i].thread_ref = LUA_NOREF;
        }
    }
    for (int i = 0; i < nwait; i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, wait_refs[i]);
        if (!lua_isthread(L, -1)) {
            lua_pop(L, 1);
            luaL_unref(L, LUA_REGISTRYINDEX, wait_refs[i]);
            continue;
        }
        lua_State* co = lua_tothread(L, -1);
        mark_waiting(L, co, 0);

        lua_pushlightuserdata(L, (void*)co);
        lua_pushnil(L);
        lua_settable(L, LUA_REGISTRYINDEX);

        copy_args(L, first_arg, nargs);
        lua_xmove(L, co, nargs);
        int nres = 0;
        int status = pw_lua_resume(co, L, nargs, &nres);
        if (status != LUA_OK && status != LUA_YIELD) {
            fprintf(stderr, "[Event] Wait resume error: %s\n", lua_tostring(co, -1));
            lua_pop(co, 1);
        } else if (status == LUA_YIELD) {
            if (nres > 0) lua_pop(co, nres);
            pw_task_sync_yield(L, co);
        } else if (nres > 0) {
            lua_pop(co, nres);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, wait_refs[i]);
    }

    if (nargs > 0)
        lua_pop(L, nargs);
    return 0;
}
