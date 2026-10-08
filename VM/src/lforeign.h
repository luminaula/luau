// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lobject.h"
#include "lstate.h"

#include "lua.h"

// A foreign table is a LuaTable whose `readonly` byte holds FOREIGN_TABLE_FLAG. It has no array part and no hash part, and
// the memory right behind the LuaTable header holds a ForeignTableData that names the host's callbacks.
// The metatable of a foreign table is a shared table that holds __index, __newindex, __len and __iter as C functions
// running the callbacks, so every read and write that misses the empty storage ends up in the host.
// The flag is bit 2 of the byte: bit 0 is the frozen state and bit 1 marks an array that holds a metamethod cache, and a
// foreign table has neither, so the byte of a foreign table is exactly FOREIGN_TABLE_FLAG and is nonzero like a frozen one.
#define FOREIGN_TABLE_FLAG 4

struct ForeignTableData
{
    const lua_ForeignTableCallbacks* cb;
    void* ctx;
};

#define isforeigntable(t) ((t)->readonly == FOREIGN_TABLE_FLAG)
#define foreigndata(t) (reinterpret_cast<ForeignTableData*>((t) + 1))
#define sizeforeigntable (sizeof(LuaTable) + sizeof(ForeignTableData))

// the bytes a table holds behind its LuaTable header that the array and hash parts do not account for
#define sizeforeigntail(t) (isforeigntable(t) ? sizeof(ForeignTableData) : 0)

// the ForeignTableData of a value that is a foreign table, NULL for any other value
inline ForeignTableData* luaFT_of(const TValue* o)
{
    return (ttistable(o) && isforeigntable(hvalue(o))) ? foreigndata(hvalue(o)) : NULL;
}

LUAI_FUNC LuaTable* luaFT_new(lua_State* L, const lua_ForeignTableCallbacks* cb, void* ctx);
LUAI_FUNC void luaFT_free(lua_State* L, LuaTable* t, struct lua_Page* page);

// Operations on the stack of the calling C function. `d` is the data of the foreign table.
// key at -1 is replaced by the value
LUAI_FUNC void luaFT_get(lua_State* L, ForeignTableData* d);
// key at -2 and value at -1 are consumed
LUAI_FUNC void luaFT_set(lua_State* L, ForeignTableData* d);
LUAI_FUNC int luaFT_len(lua_State* L, ForeignTableData* d);
// same contract as lua_next
LUAI_FUNC int luaFT_next(lua_State* L, ForeignTableData* d);
// same contract as lua_rawiter
LUAI_FUNC int luaFT_rawiter(lua_State* L, ForeignTableData* d, int iter);
LUAI_FUNC void luaFT_clear(lua_State* L, ForeignTableData* d);
// pushes a plain table holding the entries of the foreign table
LUAI_FUNC void luaFT_clone(lua_State* L, ForeignTableData* d);
// value at -1 is consumed
LUAI_FUNC void luaFT_insert(lua_State* L, ForeignTableData* d, int pos);
// pushes the removed value
LUAI_FUNC void luaFT_remove(lua_State* L, ForeignTableData* d, int pos);
