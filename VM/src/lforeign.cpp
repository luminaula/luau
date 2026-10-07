// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lforeign.h"

#include "lapi.h"
#include "lgc.h"
#include "lmem.h"
#include "lstate.h"
#include "ltable.h"
#include "lualib.h"

static char foreignmetakey;

static ForeignTableData* checkforeign(lua_State* L, int idx)
{
    ForeignTableData* d = luaFT_of(luaA_toobject(L, idx));
    if (!d)
        luaL_typeerror(L, idx, "foreign table");
    return d;
}

static int ft_index(lua_State* L)
{
    ForeignTableData* d = checkforeign(L, 1);
    lua_settop(L, 2);
    luaFT_get(L, d);
    return 1;
}

static int ft_newindex(lua_State* L)
{
    ForeignTableData* d = checkforeign(L, 1);
    lua_settop(L, 3);
    lua_remove(L, 1);
    luaFT_set(L, d);
    return 0;
}

static int ft_len(lua_State* L)
{
    ForeignTableData* d = checkforeign(L, 1);
    lua_pushinteger(L, luaFT_len(L, d));
    return 1;
}

static int ft_next(lua_State* L)
{
    ForeignTableData* d = checkforeign(L, 1);
    lua_settop(L, 2);
    lua_remove(L, 1);
    if (luaFT_next(L, d))
        return 2;
    lua_pushnil(L);
    return 1;
}

static int ft_iter(lua_State* L)
{
    checkforeign(L, 1);
    lua_pushcfunction(L, ft_next, "next");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static LuaTable* getmeta(lua_State* L)
{
    lua_rawgetptagged(L, LUA_REGISTRYINDEX, &foreignmetakey, 0);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_createtable(L, 0, 4);
        lua_pushcfunction(L, ft_index, "foreign.__index");
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, ft_newindex, "foreign.__newindex");
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, ft_len, "foreign.__len");
        lua_setfield(L, -2, "__len");
        lua_pushcfunction(L, ft_iter, "foreign.__iter");
        lua_setfield(L, -2, "__iter");
        lua_pushvalue(L, -1);
        lua_rawsetptagged(L, LUA_REGISTRYINDEX, &foreignmetakey, 0);
    }
    LuaTable* mt = hvalue(L->top - 1);
    lua_pop(L, 1);
    return mt;
}

LuaTable* luaFT_new(lua_State* L, const lua_ForeignTableCallbacks* cb, void* ctx)
{
    LuaTable* mt = getmeta(L);

    LuaTable* t = luaM_newgco(L, LuaTable, sizeforeigntable, L->activememcat, LUA_TTABLE);
    luaC_init(L, t, LUA_TTABLE);
    t->metatable = mt;
    t->tmcache = cast_byte(~0);
    t->array = NULL;
    t->sizearray = 0;
    t->lastfree = 0;
    t->lsizenode = 0;
    t->readonly = FOREIGN_TABLE_FLAG;
    t->safeenv = 0;
    t->nodemask8 = 0;
    t->node = cast_to(LuaNode*, &luaH_dummynode);

    ForeignTableData* d = foreigndata(t);
    d->cb = cb;
    d->ctx = ctx;
    return t;
}

void luaFT_free(lua_State* L, LuaTable* t, lua_Page* page)
{
    ForeignTableData* d = foreigndata(t);
    if (d->cb->release)
        d->cb->release(d->ctx);
    luaM_freegco(L, t, sizeforeigntable, t->memcat, page);
}

void luaFT_get(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    d->cb->get(L, d->ctx);
    lua_replace(L, -2);
}

void luaFT_set(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    d->cb->set(L, d->ctx);
    lua_pop(L, 2);
}

int luaFT_len(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    return d->cb->len(L, d->ctx);
}

int luaFT_next(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    if (d->cb->next(L, d->ctx))
    {
        lua_remove(L, -3);
        return 1;
    }
    lua_pop(L, 1);
    return 0;
}

int luaFT_rawiter(lua_State* L, ForeignTableData* d, int iter)
{
    lua_checkstack(L, LUA_MINSTACK);

    // the position of an entry is the number of entries before it
    lua_pushnil(L);
    for (int i = 0; i <= iter; i++)
    {
        if (!d->cb->next(L, d->ctx))
        {
            lua_pop(L, 1);
            return -1;
        }

        if (i < iter)
        {
            lua_pop(L, 1); // the value; the key is the cursor of the next step
            lua_remove(L, -2);
        }
    }

    lua_remove(L, -3);
    return iter + 1;
}

void luaFT_clear(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    if (d->cb->clear)
    {
        d->cb->clear(L, d->ctx);
        return;
    }

    for (;;)
    {
        lua_pushnil(L);
        if (!d->cb->next(L, d->ctx))
        {
            lua_pop(L, 1);
            return;
        }
        lua_pop(L, 1); // the value
        lua_pushnil(L);
        d->cb->set(L, d->ctx);
        lua_pop(L, 3);
    }
}

void luaFT_clone(lua_State* L, ForeignTableData* d)
{
    lua_checkstack(L, LUA_MINSTACK);
    lua_createtable(L, 0, 0);
    lua_pushnil(L);
    while (d->cb->next(L, d->ctx))
    {
        // stack: clone, cursor, key, value
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, -6);
        lua_pop(L, 1);     // the value
        lua_remove(L, -2); // the old cursor; the key is the new one
    }
    lua_pop(L, 1);
}

void luaFT_insert(lua_State* L, ForeignTableData* d, int pos)
{
    lua_checkstack(L, LUA_MINSTACK);
    d->cb->insert(L, d->ctx, pos);
    lua_pop(L, 1);
}

void luaFT_remove(lua_State* L, ForeignTableData* d, int pos)
{
    lua_checkstack(L, LUA_MINSTACK);
    d->cb->remove(L, d->ctx, pos);
}
