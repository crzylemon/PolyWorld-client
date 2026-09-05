/*-------------------------------------------------------------------------------------------*\
|   PolyWorld! Client                                                                         |
|   File: lua_pw_task.c                                                                       |
|   Purpose: Lua tasks and stuff more stuff AAAGHGHHHHHH                                      |
\*-------------------------------------------------------------------------------------------*/

#include "lua_pw_task.h"
#include "lua_pw_event.h"

#include "lauxlib.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define PW_TASK_MAX     128
#define PW_TASK_REG     "_pw_task_sched"
#define PW_TASK_CO_MAP  "_pw_task_co_map"

#define PW_LUA_SLICE_MS    80.0
#define PW_LUA_HOOK_EVERY  500
#define PW_LUA_MAX_HOOKS   1000

static int g_pw_lua_wd_depth = 0;
static int g_pw_lua_wd_hooks = 0;
static double g_pw_lua_wd_start_ms = 0.0;

static double pw_lua_now_ms(void) {
#if defined(_WIN32)
    return (double)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}

static void pw_lua_timeout_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    if (g_pw_lua_wd_depth <= 0) return;
    g_pw_lua_wd_hooks++;
    double elapsed = pw_lua_now_ms() - g_pw_lua_wd_start_ms;
    if (g_pw_lua_wd_hooks < PW_LUA_MAX_HOOKS && elapsed < PW_LUA_SLICE_MS)
        return;
    lua_sethook(L, NULL, 0, 0);
    g_pw_lua_wd_depth = 0;
    fprintf(stderr, "[Lua] watchdog: script timed out (%.0f ms, %d hook samples)\n",
            elapsed, g_pw_lua_wd_hooks);
    luaL_error(L, "script timed out after %.0f ms (infinite loop? use task.wait())",
               elapsed > 1.0 ? elapsed : PW_LUA_SLICE_MS);
}

void pw_lua_watchdog_arm(lua_State* L) {
    if (!L) return;
    if (g_pw_lua_wd_depth++ == 0) {
        g_pw_lua_wd_start_ms = pw_lua_now_ms();
        g_pw_lua_wd_hooks = 0;
    }
    lua_sethook(L, pw_lua_timeout_hook, LUA_MASKCOUNT, PW_LUA_HOOK_EVERY);
}

void pw_lua_watchdog_disarm(lua_State* L) {
    if (g_pw_lua_wd_depth > 0)
        g_pw_lua_wd_depth--;
    if (!L) return;
    if (g_pw_lua_wd_depth <= 0) {
        g_pw_lua_wd_depth = 0;
        lua_sethook(L, NULL, 0, 0);
    } else {
        lua_sethook(L, pw_lua_timeout_hook, LUA_MASKCOUNT, PW_LUA_HOOK_EVERY);
    }
}

int pw_lua_resume(lua_State* co, lua_State* from, int nargs, int* nresults) {
    int nres = 0;
    if (!nresults) nresults = &nres;
    if (!co) {
        *nresults = 0;
        return LUA_ERRRUN;
    }
    pw_lua_watchdog_arm(co);
    int status = lua_resume(co, from, nargs, nresults);
    pw_lua_watchdog_disarm(co);
    return status;
}

int pw_lua_pcall(lua_State* L, int nargs, int nresults, int msgh) {
    if (!L) return LUA_ERRRUN;
    pw_lua_watchdog_arm(L);
    int status = lua_pcall(L, nargs, nresults, msgh);
    pw_lua_watchdog_disarm(L);
    return status;
}

typedef struct {
    int co_ref;
    float wait_left;
    float wait_total;
    int cancelled;
    int defer_pending;
    int first_nargs;
} PwTaskSlot;

typedef struct {
    PwTaskSlot slots[PW_TASK_MAX];
} PwTaskSched;

static PwTaskSched* get_sched(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, PW_TASK_REG);
    PwTaskSched* s = (PwTaskSched*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return s;
}

static void ensure_co_map(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, PW_TASK_CO_MAP);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, PW_TASK_CO_MAP);
    }
}

static void map_co(lua_State* L, lua_State* co, int slot) {
    ensure_co_map(L);
    lua_pushlightuserdata(L, (void*)co);
    if (slot < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, slot);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

static int find_slot_for_co(lua_State* L, lua_State* co) {
    ensure_co_map(L);
    lua_pushlightuserdata(L, (void*)co);
    lua_gettable(L, -2);
    int slot = -1;
    if (lua_isinteger(L, -1))
        slot = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
    return slot;
}

static int alloc_slot(PwTaskSched* s) {
    for (int i = 0; i < PW_TASK_MAX; i++) {
        if (s->slots[i].co_ref == LUA_NOREF)
            return i;
    }
    return -1;
}

static void free_slot(lua_State* L, PwTaskSched* s, int slot) {
    if (slot < 0 || slot >= PW_TASK_MAX) return;
    PwTaskSlot* t = &s->slots[slot];
    if (t->co_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, t->co_ref);
        if (lua_isthread(L, -1))
            map_co(L, lua_tothread(L, -1), -1);
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, t->co_ref);
    }
    t->co_ref = LUA_NOREF;
    t->wait_left = 0;
    t->wait_total = 0;
    t->cancelled = 0;
    t->defer_pending = 0;
    t->first_nargs = -1;
}

static void read_legacy_wait(lua_State* L, lua_State* co, PwTaskSlot* t) {
    lua_pushlightuserdata(L, (void*)co);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_isnumber(L, -1)) {
        t->wait_total = (float)lua_tonumber(L, -1);
        t->wait_left = t->wait_total;
        lua_pushlightuserdata(L, (void*)co);
        lua_pushnil(L);
        lua_settable(L, LUA_REGISTRYINDEX);
    }
    lua_pop(L, 1);
}

static void sync_yield_state(lua_State* L, lua_State* co, PwTaskSlot* t) {
    float saved_left = t->wait_left;
    float saved_total = t->wait_total;
    t->wait_left = 0.0f;
    t->wait_total = 0.0f;
    read_legacy_wait(L, co, t);
    if (t->wait_left == 0.0f && t->wait_total == 0.0f) {
        if (saved_left > 0.0f || saved_total > 0.0f) {
            t->wait_left = saved_left;
            t->wait_total = saved_total;
        } else if (pw_event_co_waiting(L, co)) {
            t->wait_left = -1.0f;
        }
    }
}

void pw_task_sync_yield(lua_State* L, lua_State* co) {
    if (!L || !co) return;
    int slot = find_slot_for_co(L, co);
    if (slot < 0) return;
    PwTaskSched* s = get_sched(L);
    if (!s) return;
    sync_yield_state(L, co, &s->slots[slot]);
}

static void resume_slot(lua_State* L, PwTaskSched* s, int slot, int with_elapsed) {
    PwTaskSlot* t = &s->slots[slot];
    if (t->co_ref == LUA_NOREF || t->cancelled) {
        free_slot(L, s, slot);
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, t->co_ref);
    if (!lua_isthread(L, -1)) {
        lua_pop(L, 1);
        free_slot(L, s, slot);
        return;
    }
    lua_State* co = lua_tothread(L, -1);
    lua_pop(L, 1);

    int nargs = 0;
    if (t->first_nargs >= 0) {
        nargs = t->first_nargs;
        t->first_nargs = -1;
    } else if (with_elapsed) {
        if (!lua_checkstack(co, 1)) {
            free_slot(L, s, slot);
            return;
        }
        lua_pushnumber(co, (double)(t->wait_total > 0.0f ? t->wait_total : 0.0f));
        nargs = 1;
    }

    int nres = 0;
    int status = pw_lua_resume(co, L, nargs, &nres);
    if (status == LUA_YIELD) {
        if (nres > 0) lua_pop(co, nres);
        sync_yield_state(L, co, t);
        return;
    }
    if (status != LUA_OK) {
        fprintf(stderr, "[task] error: %s\n", lua_tostring(co, -1));
        lua_pop(co, 1);
    } else if (nres > 0) {
        lua_pop(co, nres);
    }
    free_slot(L, s, slot);
}

int pw_task_owns_co(lua_State* L, lua_State* co) {
    return find_slot_for_co(L, co) >= 0;
}

static int start_thread(lua_State* L, PwTaskSched* s, int slot, int nargs, int mode) {
    PwTaskSlot* t = &s->slots[slot];
    lua_State* co = lua_newthread(L);
    lua_pushvalue(L, -1);
    t->co_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    map_co(L, co, slot);
    t->cancelled = 0;
    t->defer_pending = (mode == 2);
    if (mode != 1) {
        t->wait_left = 0;
        t->wait_total = 0;
    }
    t->first_nargs = nargs;

    lua_insert(L, -(1 + nargs + 1));
    lua_xmove(L, co, 1 + nargs);

    if (mode == 0)
        resume_slot(L, s, slot, 0);
    return 1;
}

int pw_task_spawn_from_stack(lua_State* L, int nargs) {
    pw_task_open(L);
    PwTaskSched* s = get_sched(L);
    if (!s) return luaL_error(L, "task scheduler missing");
    int slot = alloc_slot(s);
    if (slot < 0) return luaL_error(L, "task.spawn: too many tasks (max %d)", PW_TASK_MAX);
    return start_thread(L, s, slot, nargs, 0);
}

static int lua_task_wait(lua_State* L) {
    float seconds = (float)luaL_optnumber(L, 1, 0.0);
    if (seconds < 0.0f) seconds = 0.0f;
    if (lua_pushthread(L) == 1) {
        lua_pop(L, 1);
        return luaL_error(L, "task.wait cannot be called on the main thread");
    }
    lua_pop(L, 1);

    int slot = find_slot_for_co(L, L);
    if (slot >= 0) {
        PwTaskSched* s = get_sched(L);
        if (s) {
            s->slots[slot].wait_total = seconds;
            s->slots[slot].wait_left = seconds;
        }
    } else {
        lua_pushlightuserdata(L, (void*)L);
        lua_pushnumber(L, seconds);
        lua_settable(L, LUA_REGISTRYINDEX);
    }
    return lua_yield(L, 0);
}

static int lua_task_spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int nargs = lua_gettop(L) - 1;
    return pw_task_spawn_from_stack(L, nargs);
}

static int lua_task_delay(lua_State* L) {
    float seconds = (float)luaL_checknumber(L, 1);
    if (seconds < 0.0f) seconds = 0.0f;
    luaL_checktype(L, 2, LUA_TFUNCTION);
    pw_task_open(L);
    PwTaskSched* s = get_sched(L);
    if (!s) return luaL_error(L, "task scheduler missing");
    int slot = alloc_slot(s);
    if (slot < 0) return luaL_error(L, "task.delay: too many tasks (max %d)", PW_TASK_MAX);

    lua_remove(L, 1);
    int nargs = lua_gettop(L) - 1;
    s->slots[slot].wait_left = seconds;
    s->slots[slot].wait_total = seconds;
    return start_thread(L, s, slot, nargs, 1);
}

static int lua_task_defer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    pw_task_open(L);
    PwTaskSched* s = get_sched(L);
    if (!s) return luaL_error(L, "task scheduler missing");
    int slot = alloc_slot(s);
    if (slot < 0) return luaL_error(L, "task.defer: too many tasks (max %d)", PW_TASK_MAX);

    int nargs = lua_gettop(L) - 1;
    return start_thread(L, s, slot, nargs, 2);
}

static int lua_task_cancel(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTHREAD);
    lua_State* co = lua_tothread(L, 1);
    int slot = find_slot_for_co(L, co);
    if (slot < 0) return 0;
    PwTaskSched* s = get_sched(L);
    if (!s) return 0;
    s->slots[slot].cancelled = 1;
    free_slot(L, s, slot);
    return 0;
}

void pw_task_open(lua_State* L) {
    if (!L) return;
    lua_getfield(L, LUA_REGISTRYINDEX, PW_TASK_REG);
    if (lua_isuserdata(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pop(L, 1);

    PwTaskSched* s = (PwTaskSched*)lua_newuserdatauv(L, sizeof(PwTaskSched), 0);
    memset(s, 0, sizeof(PwTaskSched));
    for (int i = 0; i < PW_TASK_MAX; i++)
        s->slots[i].co_ref = LUA_NOREF;
    lua_setfield(L, LUA_REGISTRYINDEX, PW_TASK_REG);

    ensure_co_map(L);
    lua_pop(L, 1);
}

int pw_task_push_lib(lua_State* L) {
    pw_task_open(L);
    lua_newtable(L);
    lua_pushcfunction(L, lua_task_wait);   lua_setfield(L, -2, "wait");
    lua_pushcfunction(L, lua_task_spawn);  lua_setfield(L, -2, "spawn");
    lua_pushcfunction(L, lua_task_delay);  lua_setfield(L, -2, "delay");
    lua_pushcfunction(L, lua_task_defer);  lua_setfield(L, -2, "defer");
    lua_pushcfunction(L, lua_task_cancel); lua_setfield(L, -2, "cancel");
    return 1;
}

void pw_task_tick(lua_State* L, float dt) {
    if (!L) return;
    pw_task_open(L);
    PwTaskSched* s = get_sched(L);
    if (!s) return;

    int defer_list[PW_TASK_MAX];
    int ndefer = 0;
    for (int i = 0; i < PW_TASK_MAX; i++) {
        if (s->slots[i].co_ref != LUA_NOREF && s->slots[i].defer_pending &&
            !s->slots[i].cancelled)
            defer_list[ndefer++] = i;
    }
    for (int i = 0; i < ndefer; i++) {
        s->slots[defer_list[i]].defer_pending = 0;
        resume_slot(L, s, defer_list[i], 0);
    }

    int ready[PW_TASK_MAX];
    int nready = 0;
    for (int i = 0; i < PW_TASK_MAX; i++) {
        PwTaskSlot* t = &s->slots[i];
        if (t->co_ref == LUA_NOREF || t->cancelled || t->defer_pending) continue;
        if (t->wait_left < 0.0f) continue;

        if (t->first_nargs >= 0) {
            if (t->wait_left > 0.0f) {
                t->wait_left -= dt;
                if (t->wait_left > 0.0f) continue;
                t->wait_left = 0.0f;
            }
            ready[nready++] = i;
            continue;
        }

        if (t->wait_left > 0.0f) {
            t->wait_left -= dt;
            if (t->wait_left > 0.0f) continue;
            t->wait_left = 0.0f;
            ready[nready++] = i;
        } else {
            ready[nready++] = i;
        }
    }

    for (int i = 0; i < nready; i++) {
        PwTaskSlot* t = &s->slots[ready[i]];
        if (t->co_ref == LUA_NOREF) continue;
        int with_elapsed = (t->first_nargs < 0);
        resume_slot(L, s, ready[i], with_elapsed);
    }
}
