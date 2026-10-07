// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lualib.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>

// A host whose foreign tables are backed by a std::map. Values are limited to nil, boolean, number, string, vector,
// buffer and nested tables of those; keys to numbers and strings.
namespace ForeignTableHost
{

struct Table;

struct Value
{
    enum Kind
    {
        Nil,
        Bool,
        Number,
        String,
        Vector,
        Buffer,
        Node
    };

    Kind kind = Nil;
    bool b = false;
    double n = 0;
    float v[4] = {0, 0, 0, 0};
    std::string s;
    std::shared_ptr<Table> t;
};

struct Key
{
    bool isString = false;
    double n = 0;
    std::string s;

    bool operator<(const Key& o) const
    {
        if (isString != o.isString)
            return !isString;
        return isString ? s < o.s : n < o.n;
    }
};

struct Table
{
    std::map<Key, Value> m;
};

inline int& liveTables()
{
    static int count = 0;
    return count;
}

inline long& callbackCount()
{
    static long count = 0;
    return count;
}

struct Ctx
{
    std::shared_ptr<Table> t;
    explicit Ctx(std::shared_ptr<Table> table)
        : t(std::move(table))
    {
        liveTables()++;
    }
    ~Ctx()
    {
        liveTables()--;
    }
};

inline const lua_ForeignTableCallbacks& hostCallbacks();
inline const lua_ForeignTableCallbacks& minimalHostCallbacks();

inline void pushTable(lua_State* L, std::shared_ptr<Table> t)
{
    lua_newforeigntable(L, &hostCallbacks(), new Ctx(std::move(t)));
}

inline Key toKey(lua_State* L, int idx)
{
    Key k;
    int t = lua_type(L, idx);
    if (t == LUA_TNUMBER)
    {
        k.n = lua_tonumber(L, idx);
        if (k.n != k.n)
            luaL_error(L, "commons: a NaN key is not storable");
    }
    else if (t == LUA_TSTRING)
    {
        k.isString = true;
        size_t len;
        const char* s = lua_tolstring(L, idx, &len);
        k.s.assign(s, len);
    }
    else
        luaL_error(L, "commons: a key of type %s is not storable (strings and numbers only)", luaL_typename(L, idx));
    return k;
}

inline std::string describeKey(const Key& k)
{
    if (k.isString)
        return k.s;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.14g", k.n);
    return buf;
}

inline Value toValue(lua_State* L, int idx, const Key& key, int depth)
{
    idx = lua_absindex(L, idx);
    Value v;
    switch (lua_type(L, idx))
    {
    case LUA_TNIL:
        break;
    case LUA_TBOOLEAN:
        v.kind = Value::Bool;
        v.b = lua_toboolean(L, idx) != 0;
        break;
    case LUA_TNUMBER:
        v.kind = Value::Number;
        v.n = lua_tonumber(L, idx);
        break;
    case LUA_TSTRING:
    {
        v.kind = Value::String;
        size_t len;
        const char* s = lua_tolstring(L, idx, &len);
        v.s.assign(s, len);
        break;
    }
    case LUA_TVECTOR:
    {
        v.kind = Value::Vector;
        const float* f = lua_tovector(L, idx);
        for (int i = 0; i < LUA_VECTOR_SIZE; i++)
            v.v[i] = f[i];
        break;
    }
    case LUA_TBUFFER:
    {
        v.kind = Value::Buffer;
        size_t len;
        void* p = lua_tobuffer(L, idx, &len);
        v.s.assign(static_cast<const char*>(p), len);
        break;
    }
    case LUA_TTABLE:
    {
        if (void* ctx = lua_toforeigntable(L, idx, &hostCallbacks()))
        {
            v.kind = Value::Node;
            v.t = static_cast<Ctx*>(ctx)->t;
            break;
        }
        if (lua_isforeigntable(L, idx))
            luaL_error(L, "commons: the table stored under '%s' belongs to another host", describeKey(key).c_str());
        if (depth >= 16)
            luaL_error(L, "commons: the table stored under '%s' is nested too deeply", describeKey(key).c_str());

        // a plain table is copied by value
        v.kind = Value::Node;
        v.t = std::make_shared<Table>();
        lua_pushnil(L);
        while (lua_next(L, idx))
        {
            Key ck = toKey(L, -2);
            Value cv = toValue(L, -1, ck, depth + 1);
            if (cv.kind != Value::Nil)
                v.t->m[ck] = std::move(cv);
            lua_pop(L, 1);
        }
        break;
    }
    default:
        luaL_error(L, "commons: a %s is not storable (key '%s')", luaL_typename(L, idx), describeKey(key).c_str());
    }
    return v;
}

inline void pushValue(lua_State* L, const Value& v)
{
    switch (v.kind)
    {
    case Value::Nil:
        lua_pushnil(L);
        break;
    case Value::Bool:
        lua_pushboolean(L, v.b);
        break;
    case Value::Number:
        lua_pushnumber(L, v.n);
        break;
    case Value::String:
        lua_pushlstring(L, v.s.data(), v.s.size());
        break;
    case Value::Vector:
#if LUA_VECTOR_SIZE == 4
        lua_pushvector(L, v.v[0], v.v[1], v.v[2], v.v[3]);
#else
        lua_pushvector(L, v.v[0], v.v[1], v.v[2]);
#endif
        break;
    case Value::Buffer:
    {
        void* p = lua_newbuffer(L, v.s.size());
        if (!v.s.empty())
            memcpy(p, v.s.data(), v.s.size());
        break;
    }
    case Value::Node:
        pushTable(L, v.t);
        break;
    }
}

inline Table& self(void* ctx)
{
    return *static_cast<Ctx*>(ctx)->t;
}

inline bool has(Table& t, int i)
{
    Key k;
    k.n = i;
    return t.m.count(k) != 0;
}

inline void pushKey(lua_State* L, const Key& k)
{
    if (k.isString)
        lua_pushlstring(L, k.s.data(), k.s.size());
    else
        lua_pushnumber(L, k.n);
}

inline Key numKey(int i)
{
    Key k;
    k.n = i;
    return k;
}

inline void cbGet(lua_State* L, void* ctx)
{
    callbackCount()++;
    Table& t = self(ctx);
    Key k = toKey(L, -1);
    auto it = t.m.find(k);
    if (it == t.m.end())
        lua_pushnil(L);
    else
        pushValue(L, it->second);
}

inline void cbSet(lua_State* L, void* ctx)
{
    callbackCount()++;
    Table& t = self(ctx);
    Key k = toKey(L, -2);
    Value v = toValue(L, -1, k, 0);
    if (v.kind == Value::Nil)
        t.m.erase(k);
    else
        t.m[k] = std::move(v);
}

inline int cbLen(lua_State* L, void* ctx)
{
    callbackCount()++;
    Table& t = self(ctx);
    if (!has(t, 1))
        return 0;
    int i = 1, j = 2;
    while (has(t, j))
    {
        i = j;
        if (j > (1 << 29))
            return j;
        j *= 2;
    }
    while (j - i > 1)
    {
        int m = (i + j) / 2;
        if (has(t, m))
            i = m;
        else
            j = m;
    }
    return i;
}

inline int cbNext(lua_State* L, void* ctx)
{
    callbackCount()++;
    Table& t = self(ctx);
    std::map<Key, Value>::iterator it;
    if (lua_isnil(L, -1))
        it = t.m.begin();
    else
        it = t.m.upper_bound(toKey(L, -1));
    if (it == t.m.end())
        return 0;
    pushKey(L, it->first);
    pushValue(L, it->second);
    return 1;
}

inline void cbInsert(lua_State* L, void* ctx, int pos)
{
    callbackCount()++;
    Table& t = self(ctx);
    Key vk = numKey(pos);
    Value v = toValue(L, -1, vk, 0);
    int n = cbLen(L, ctx);
    for (int i = n; i >= pos; i--)
        t.m[numKey(i + 1)] = t.m[numKey(i)];
    if (v.kind == Value::Nil)
        t.m.erase(numKey(pos));
    else
        t.m[numKey(pos)] = std::move(v);
}

inline void cbRemove(lua_State* L, void* ctx, int pos)
{
    callbackCount()++;
    Table& t = self(ctx);
    int n = cbLen(L, ctx);
    auto it = t.m.find(numKey(pos));
    if (it == t.m.end())
        lua_pushnil(L);
    else
        pushValue(L, it->second);
    for (int i = pos; i < n; i++)
        t.m[numKey(i)] = t.m[numKey(i + 1)];
    t.m.erase(numKey(n));
}

inline void cbClear(lua_State* L, void* ctx)
{
    callbackCount()++;
    self(ctx).m.clear();
}

inline void cbRelease(void* ctx)
{
    delete static_cast<Ctx*>(ctx);
}

inline const lua_ForeignTableCallbacks& hostCallbacks()
{
    static const lua_ForeignTableCallbacks c = {cbGet, cbSet, cbLen, cbNext, cbInsert, cbRemove, cbClear, cbRelease};
    return c;
}

// the same host without the optional table operations, so the library works from get/set/len alone
inline const lua_ForeignTableCallbacks& minimalHostCallbacks()
{
    static const lua_ForeignTableCallbacks c = {cbGet, cbSet, cbLen, cbNext, nullptr, nullptr, nullptr, cbRelease};
    return c;
}

inline int newforeign(lua_State* L)
{
    bool minimal = lua_toboolean(L, 1) != 0;
    auto* ctx = new Ctx(std::make_shared<Table>());
    lua_newforeigntable(L, minimal ? &minimalHostCallbacks() : &hostCallbacks(), ctx);
    return 1;
}

// reads the host storage directly, without going through the VM: returns a plain table copy of the entries
inline void pushPlainCopy(lua_State* L, const Table& t, int depth)
{
    lua_createtable(L, 0, int(t.m.size()));
    for (auto& e : t.m)
    {
        pushKey(L, e.first);
        if (e.second.kind == Value::Node)
        {
            if (depth > 8)
                lua_pushnil(L);
            else
                pushPlainCopy(L, *e.second.t, depth + 1);
        }
        else
            pushValue(L, e.second);
        lua_rawset(L, -3);
    }
}

inline int hostdump(lua_State* L)
{
    void* ctx = lua_toforeigntable(L, 1, &hostCallbacks());
    if (!ctx)
        ctx = lua_toforeigntable(L, 1, &minimalHostCallbacks());
    if (!ctx)
        luaL_error(L, "hostdump expects a foreign table");
    pushPlainCopy(L, self(ctx), 0);
    return 1;
}

// writes the host storage directly, without going through the VM
inline int hostset(lua_State* L)
{
    void* ctx = lua_toforeigntable(L, 1, &hostCallbacks());
    if (!ctx)
        ctx = lua_toforeigntable(L, 1, &minimalHostCallbacks());
    if (!ctx)
        luaL_error(L, "hostset expects a foreign table");
    Key k = toKey(L, 2);
    Value v = toValue(L, 3, k, 0);
    if (v.kind == Value::Nil)
        self(ctx).m.erase(k);
    else
        self(ctx).m[k] = std::move(v);
    return 0;
}

inline int hostlive(lua_State* L)
{
    lua_pushinteger(L, liveTables());
    return 1;
}

inline int hostcalls(lua_State* L)
{
    lua_pushnumber(L, double(callbackCount()));
    return 1;
}

// a plain C function doing the same map lookup as the get callback, for measuring dispatch overhead
inline int hostget(lua_State* L)
{
    void* ctx = lua_toforeigntable(L, 1, &hostCallbacks());
    if (!ctx)
        luaL_error(L, "hostget expects a foreign table");
    lua_settop(L, 2);
    cbGet(L, ctx);
    return 1;
}

inline int hostset2(lua_State* L)
{
    void* ctx = lua_toforeigntable(L, 1, &hostCallbacks());
    if (!ctx)
        luaL_error(L, "hostset2 expects a foreign table");
    lua_settop(L, 3);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    cbSet(L, ctx);
    return 0;
}

inline void registerGlobals(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"newforeign", newforeign},
        {"hostdump", hostdump},
        {"hostset", hostset},
        {"hostlive", hostlive},
        {"hostcalls", hostcalls},
        {"hostget", hostget},
        {"hostset2", hostset2},
        {nullptr, nullptr},
    };

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    luaL_register(L, nullptr, funcs);
    lua_pop(L, 1);
}

} // namespace ForeignTableHost
