-- This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
print('testing foreign tables')

local function fails(f, ...)
    local ok, err = pcall(f, ...)
    assert(not ok, "expected an error")
    return tostring(err)
end

local function collect(t)
    local keys, n = {}, 0
    for k, v in pairs(t) do
        keys[k] = v
        n += 1
    end
    return keys, n
end

local function eqarray(t, expected)
    assert(#t == #expected, "length " .. #t .. " ~= " .. #expected)
    for i = 1, #expected do
        assert(t[i] == expected[i], "element " .. i)
    end
end

local function fill(t, arr)
    for i = 1, #arr do
        t[i] = arr[i]
    end
    return t
end

local function run(minimal)
    local live0 = hostlive()

    -- the type is a table
    local t = newforeign(minimal)
    assert(type(t) == "table")
    assert(typeof(t) == "table")
    assert(select('#', type(t)) == 1)
    assert(rawequal(t, t) and t == t and t ~= newforeign(minimal))
    assert(getmetatable(t) == nil)
    assert(tostring(t):sub(1, 6) == "table:")

    -- reads and writes
    assert(t.x == nil)
    t.x = 1
    assert(t.x == 1)
    t["k"] = "v"
    assert(t.k == "v" and t["k"] == "v")
    t[1], t[2], t[3] = "a", "b", "c"
    assert(t[1] == "a" and t[2] == "b" and t[3] == "c")
    local i = 2
    assert(t[i] == "b")
    t[i] = "B"
    assert(t[2] == "B")
    t[1.5] = "frac"
    assert(t[1.5] == "frac")
    t.x = nil
    assert(t.x == nil)
    t.x = 7

    -- the storage is the host's: it sees the writes and the VM sees the host's
    local d = hostdump(t)
    assert(d.x == 7 and d.k == "v" and d[1] == "a" and d[2] == "B" and d[3] == "c")
    hostset(t, "fromhost", 42)
    assert(t.fromhost == 42)
    hostset(t, "fromhost", nil)
    assert(t.fromhost == nil)
    local calls = hostcalls()
    local _ = t.x
    assert(hostcalls() > calls)

    -- data types
    t.b = true
    t.f = false
    t.n = 1.5
    t.s = "str\0with zero"
    t.v = vector.create(1, 2, 3)
    local buf = buffer.create(8)
    buffer.writeu32(buf, 0, 0xdeadbeef)
    buffer.writeu32(buf, 4, 7)
    t.buf = buf
    assert(t.b == true and t.f == false and t.n == 1.5)
    assert(t.s == "str\0with zero" and #t.s == 13)
    assert(t.v == vector.create(1, 2, 3) and t.v.y == 2)
    assert(typeof(t.buf) == "buffer" and buffer.len(t.buf) == 8)
    assert(buffer.readu32(t.buf, 0) == 0xdeadbeef and buffer.readu32(t.buf, 4) == 7)
    buffer.writeu32(buf, 0, 0)
    assert(buffer.readu32(t.buf, 0) == 0xdeadbeef) -- the host holds a copy

    -- nested tables
    t.player = {hp = 100, name = "p", pos = vector.create(1, 2, 3), list = {10, 20, 30}}
    assert(type(t.player) == "table" and typeof(t.player) == "table")
    assert(t.player.hp == 100 and t.player.name == "p" and t.player.pos == vector.create(1, 2, 3))
    t.player.hp -= 5
    assert(t.player.hp == 95)
    t.player.inv = {}
    t.player.inv.gold = 10
    t.player.inv.gold += 5
    assert(t.player.inv.gold == 15)
    assert(#t.player.list == 3 and t.player.list[2] == 20)
    t.player.list[4] = 40
    assert(#t.player.list == 4)
    local p = t.player
    p.hp = 1
    assert(t.player.hp == 1)
    t.alias = t.player
    assert(t.alias.hp == 1)
    t.alias.hp = 2
    assert(t.player.hp == 2)
    assert(hostdump(t).player.hp == 2 and hostdump(t).player.inv.gold == 15)
    assert(t.player.missing == nil and t.player.inv.missing == nil)
    assert(fails(function() return t.player.missing.deeper end):find("index nil"))

    -- a method-call on a field reads the field and calls it
    assert(fails(function() t:nomethod() end):find("nomethod"))

    -- compound assignment in a loop
    t.count = 0
    for n = 1, 200 do
        t.count += 1
    end
    assert(t.count == 200)

    -- pairs, next, generalized iteration
    local e = fill(newforeign(minimal), {"a", "b", "c"})
    e.x, e.y = 10, 20
    local keys, n = collect(e)
    assert(n == 5 and keys[1] == "a" and keys[2] == "b" and keys[3] == "c" and keys.x == 10 and keys.y == 20)

    local seen = 0
    for k, v in e do
        assert(e[k] == v)
        seen += 1
    end
    assert(seen == 5)

    seen = 0
    for k, v in next, e do
        assert(e[k] == v)
        seen += 1
    end
    assert(seen == 5)

    local k1, v1 = next(e)
    assert(k1 ~= nil and e[k1] == v1)
    local count = 0
    local k = nil
    repeat
        k = next(e, k)
        if k ~= nil then count += 1 end
    until k == nil
    assert(count == 5)
    assert(next(newforeign(minimal)) == nil)
    assert(select('#', next(newforeign(minimal))) == 1)

    -- removing the key being visited while iterating
    local r = fill(newforeign(minimal), {1, 2, 3, 4, 5, 6})
    for k2, v2 in pairs(r) do
        r[k2] = nil
    end
    assert(next(r) == nil and #r == 0)

    -- ipairs and length
    local arr = fill(newforeign(minimal), {"p", "q", "r"})
    local acc = {}
    for idx, v in ipairs(arr) do
        acc[idx] = v
    end
    assert(#acc == 3 and acc[1] == "p" and acc[3] == "r")
    assert(#arr == 3)
    arr[4] = "s"
    assert(#arr == 4)
    arr[4] = nil
    assert(#arr == 3)
    arr[#arr + 1] = "t"
    assert(#arr == 4 and arr[4] == "t")
    assert(#newforeign(minimal) == 0)
    local holes = newforeign(minimal)
    holes[1] = 1
    holes[2] = 2
    holes[4] = 4
    assert(#holes == 2)
    local ipcount = 0
    for _ in ipairs(holes) do ipcount += 1 end
    assert(ipcount == 2)

    -- raw access
    local rt = newforeign(minimal)
    assert(rawset(rt, "a", 1) == rt)
    assert(rawget(rt, "a") == 1 and rawget(rt, "missing") == nil)
    rawset(rt, 1, "one")
    rawset(rt, 2, "two")
    assert(rawget(rt, 1) == "one" and rawlen(rt) == 2 and rawequal(rt, rt))
    assert(rawlen(newforeign(minimal)) == 0)
    assert(rawget(rt, 1.5) == nil)

    -- table library
    local tl = newforeign(minimal)
    table.insert(tl, "a")
    table.insert(tl, "b")
    table.insert(tl, "d")
    table.insert(tl, 3, "c")
    table.insert(tl, 1, "z")
    eqarray(tl, {"z", "a", "b", "c", "d"})
    assert(table.remove(tl) == "d")
    assert(table.remove(tl, 1) == "z")
    assert(table.remove(tl, 2) == "b")
    eqarray(tl, {"a", "c"})
    assert(table.remove(tl, 10) == nil)
    assert(table.remove(newforeign(minimal)) == nil)
    assert(table.concat(tl, ",") == "a,c")
    assert(table.concat(fill(newforeign(minimal), {1, 2, 3}), "-") == "1-2-3")
    assert(table.concat(fill(newforeign(minimal), {1, 2, 3}), "-", 2, 3) == "2-3")
    assert(table.find(tl, "c") == 2 and table.find(tl, "a") == 1 and table.find(tl, "nope") == nil)
    assert(table.find(tl, "c", 3) == nil)
    local u1, u2, u3 = table.unpack(fill(newforeign(minimal), {7, 8, 9}))
    assert(u1 == 7 and u2 == 8 and u3 == 9)
    assert(select('#', table.unpack(fill(newforeign(minimal), {7, 8, 9}))) == 3)
    assert(select('#', table.unpack(newforeign(minimal))) == 0)
    assert(select(2, table.unpack(fill(newforeign(minimal), {7, 8, 9}), 2, 3)) == 9)
    assert((table.unpack(fill(newforeign(minimal), {5, 6, 7}), 2)) == 6)

    local st = fill(newforeign(minimal), {5, 2, 8, 1, 9, 3, 7})
    table.sort(st)
    eqarray(st, {1, 2, 3, 5, 7, 8, 9})
    table.sort(st, function(a, b) return a > b end)
    eqarray(st, {9, 8, 7, 5, 3, 2, 1})
    local words = fill(newforeign(minimal), {"pear", "apple", "fig"})
    table.sort(words)
    eqarray(words, {"apple", "fig", "pear"})
    table.sort(newforeign(minimal))

    local ct = fill(newforeign(minimal), {1, 2, 3})
    ct.name = "n"
    local copy = table.clone(ct)
    assert(type(copy) == "table" and copy ~= ct and getmetatable(copy) == nil)
    assert(#copy == 3 and copy.name == "n" and copy[2] == 2)
    copy[1] = 100
    assert(ct[1] == 1)
    assert(select(2, collect(copy)) == 4)

    table.clear(ct)
    assert(next(ct) == nil and #ct == 0 and ct.name == nil and ct[1] == nil)
    assert(next(hostdump(ct)) == nil)
    ct.again = 1
    assert(ct.again == 1)

    local mv = fill(newforeign(minimal), {1, 2, 3, 4, 5})
    table.move(mv, 2, 4, 1)
    eqarray(mv, {2, 3, 4, 4, 5})
    local mv2 = newforeign(minimal)
    table.move({"a", "b", "c"}, 1, 3, 1, mv2)
    eqarray(mv2, {"a", "b", "c"})

    assert(table.isfrozen(t) == false)
    assert(fails(table.freeze, t):find("foreign"))
    assert(table.isfrozen(t) == false)
    assert(table.getn(fill(newforeign(minimal), {1, 2})) == 2)
    assert(table.maxn(fill(newforeign(minimal), {1, 2})) == 2)
    local fe = 0
    table.foreach(fill(newforeign(minimal), {1, 2}), function() fe += 1 end)
    assert(fe == 2)
    local fi = 0
    table.foreachi(fill(newforeign(minimal), {1, 2, 3}), function() fi += 1 end)
    assert(fi == 3)

    -- errors
    local msg = fails(function() t.fn = print end)
    assert(msg:find("fn"), msg)
    msg = fails(function() t.fn = function() end end)
    assert(msg:find("fn"), msg)
    msg = fails(function() t.ud = newproxy() end)
    assert(msg:find("ud"), msg)
    msg = fails(function() t.co = coroutine.create(print) end)
    assert(msg:find("co"), msg)
    msg = fails(function() t.nested = {deep = {fn = print}} end)
    assert(msg:find("fn"), msg)
    msg = fails(function() t[true] = 1 end)
    assert(msg:find("boolean"), msg)
    msg = fails(function() return t[true] end)
    assert(msg:find("boolean"), msg)
    msg = fails(function() t[0/0] = 1 end)
    assert(msg:find("NaN"), msg)
    assert(t.fn == nil and t.ud == nil and t.nested == nil)
    fails(setmetatable, t, {})
    fails(setmetatable, t, nil)
    fails(table.freeze, t)
    assert(getmetatable(t) == nil)
    assert(fails(function() return t < t end):find("compare"))
    assert(fails(function() return t + 1 end):find("arithmetic"))
    assert(fails(function() t() end):find("call"))
    assert(fails(function() return t .. "x" end):find("concatenate"))

    -- a foreign table is not a plain table: plain-table code paths see it through the same syntax
    local function readx(tt) return tt.x end
    local function writex(tt, v) tt.x = v end
    local function getidx(tt, ii) return tt[ii] end
    local function setidx(tt, ii, v) tt[ii] = v end
    local function len(tt) return #tt end
    local function sumpairs(tt)
        local s = 0
        for _, v in pairs(tt) do s += v end
        return s
    end
    local function sumipairs(tt)
        local s = 0
        for _, v in ipairs(tt) do s += v end
        return s
    end
    local function sumgen(tt)
        local s = 0
        for _, v in tt do s += v end
        return s
    end
    local function ins(tt, v) table.insert(tt, v) end
    local function unp(tt) return table.unpack(tt) end
    local function rlen(tt) return rawlen(tt) end
    local function rget(tt, kk) return rawget(tt, kk) end
    local function meta(tt) return getmetatable(tt) end

    local function exercise(mk, expectforeign)
        local tt = mk()
        writex(tt, 5)
        assert(readx(tt) == 5)
        for ii = 1, 20 do setidx(tt, ii, ii) end
        for ii = 1, 20 do assert(getidx(tt, ii) == ii) end
        assert(len(tt) == 20)
        assert(sumipairs(tt) == 210)
        assert(sumgen(tt) == 215)
        assert(sumpairs(tt) == 215)
        ins(tt, 21)
        assert(len(tt) == 21 and rlen(tt) == 21)
        assert(select('#', unp(tt)) == 21)
        assert(rget(tt, "x") == 5 and rget(tt, 3) == 3)
        assert(meta(tt) == nil)
        assert(readx(tt) == 5)
    end

    local function plain() return {} end
    local function foreign() return newforeign(minimal) end
    local function framed() return setmetatable({}, {__index = function() return nil end}) end

    -- warm the functions on plain tables first, then switch to foreign ones, then back
    for round = 1, 3 do
        for rep = 1, 40 do exercise(plain) end
        for rep = 1, 40 do exercise(foreign) end
        for rep = 1, 40 do exercise(plain) end
        for rep = 1, 10 do exercise(framed) end
    end

    -- and the other way round: warm on foreign tables first
    do
        local function readf(tt) return tt.field end
        local function writef(tt, v) tt.field = v end
        local ft, pt = newforeign(minimal), {}
        for rep = 1, 200 do writef(ft, rep); assert(readf(ft) == rep) end
        for rep = 1, 200 do writef(pt, rep); assert(readf(pt) == rep) end
        for rep = 1, 200 do writef(ft, rep); assert(readf(ft) == rep) end
        assert(rawget(pt, "field") == 200 and rawget(ft, "field") == 200)
        pt = table.freeze(pt)
        assert(fails(writef, pt, 1):find("readonly"))
        for rep = 1, 50 do assert(readf(ft) == 200) end
    end

    -- larger data
    local big = newforeign(minimal)
    for ii = 1, 2000 do big[ii] = ii * 2 end
    assert(#big == 2000)
    local total = 0
    for ii, v in ipairs(big) do total += v end
    assert(total == 2000 * 2001)
    total = 0
    for _, v in pairs(big) do total += v end
    assert(total == 2000 * 2001)
    for ii = 1, 2000 do table.remove(big) end
    assert(#big == 0 and next(big) == nil)
    for ii = 1, 100 do table.insert(big, 1, ii) end
    assert(#big == 100 and big[1] == 100 and big[100] == 1)

    -- strings of keys that look like numbers stay strings
    local sk = newforeign(minimal)
    sk["1"] = "str"
    sk[1] = "num"
    assert(sk["1"] == "str" and sk[1] == "num")

    -- collection releases the host storage
    do
        local x = newforeign(minimal)
        x.a = {b = {c = 1}}
        local y = x.a
        local z = y.b
        z.c = 2
        assert(x.a.b.c == 2)
    end
    t, copy, big, arr, e, r, tl, st, words, ct, mv, mv2 = nil, nil, nil, nil, nil, nil, nil, nil, nil, nil, nil, nil
    collectgarbage()
    collectgarbage()
    assert(hostlive() == live0, "leaked " .. (hostlive() - live0) .. " host tables")
end

run(false)
run(true)

-- a coroutine can use a foreign table, including across a yield
do
    local t = newforeign(false)
    local co = coroutine.wrap(function()
        t.a = 1
        coroutine.yield()
        t.a += 1
        for i = 1, 3 do t[i] = i end
        coroutine.yield()
        return t.a + #t
    end)
    co()
    assert(t.a == 1)
    co()
    assert(t.a == 2)
    assert(co() == 5)
end

-- pcall around a failing callback leaves the table usable
do
    local t = newforeign(false)
    for i = 1, 20 do
        assert(not pcall(function() t.bad = print end))
        t.good = i
        assert(t.good == i)
    end
end

-- a foreign table passed to a function expecting a plain table gets table behavior
do
    local function sorted(t)
        local keys = {}
        for k in pairs(t) do table.insert(keys, k) end
        table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
        return keys
    end
    local t = newforeign(false)
    t.b, t.a, t.c = 1, 2, 3
    local keys = sorted(t)
    assert(keys[1] == "a" and keys[2] == "b" and keys[3] == "c")
end

return 'OK'
