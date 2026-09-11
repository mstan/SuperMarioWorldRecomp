-- SMW USA / stock single-player helpers for the snesrecomp Lua TCP spike.
-- Addresses are WRAM offsets from src/variables.h. Sprite initialization and
-- fireball fields follow SMW's own status-1 dispatcher / $00:FEA6 routine.
-- Load with lua_tcp.py load tools/lua/smw.lua, then call smw.* via eval.
local r, w = mainmemory.read_u8, mainmemory.write_u8
local r16, w16 = mainmemory.read_u16_le, mainmemory.write_u16_le
event.unregisterbyname("smw.autofire")
event.unregisterbyname("smw.holdfire")
event.unregisterbyname("smw.invincible")
if game and game.command then game.command("fire_stream", "0") end
smw = {shots = 0, missed_shots = 0, fire_interval = 0}

local function integer(value, lo, hi, label)
    assert(math.type(value) == "integer" and value >= lo and value <= hi,
        label .. " must be an integer in " .. lo .. ".." .. hi)
    return value
end
local function in_level()
    assert(r(0x100) == 0x14, "enter a playable level first (game mode $14)")
    assert(r(0x109) == 0, "dismiss the opening message and enter a regular level first")
end
function smw.status()
    local sprites, fireballs = 0, 0
    for s = 0, 11 do if r(0x14c8+s) ~= 0 then sprites = sprites+1 end end
    for s = 0, 9 do if r(0x170b+s) == 5 then fireballs = fireballs+1 end end
    return string.format("mode=%02X x=%d y=%d powerup=%d sprites=%d fireballs=%d interval=%d shots=%d missed=%d",
        r(0x100), r16(0x94), r16(0x96), r(0x19), sprites, fireballs,
        smw.fire_interval, smw.shots, smw.missed_shots)
end
function smw.powerup(value)
    in_level()
    w(0x19, integer(value, 0, 3, "powerup")) -- small, big, cape, fire
end
function smw.teleport(x, y)
    in_level()
    integer(x, 0, 65535, "x"); integer(y, 0, 65535, "y")
    w16(0x94, x); w16(0x96, y); w(0x7b, 0); w(0x7d, 0)
end
function smw.spawn(id, x, y)
    in_level()
    integer(id, 0, 0xc8, "stock sprite id")
    x = integer(x or (r16(0x94)+64), 0, 65535, "x")
    y = integer(y or r16(0x96), 0, 65535, "y")
    -- The stock fireball collision loop checks normal slots 0..9. Slots 10/11
    -- exist in RAM but are special slots, so don't allocate ordinary enemies
    -- there: they can render/move while being skipped by projectile collision.
    for s = 9, 0, -1 do
        if r(0x14c8+s) == 0 then
            -- Mirror ZeroSpriteTables and LoadSpriteTables before status 1
            -- invokes the type-specific INIT. Status 1 alone leaves stale
            -- collision/tweaker bytes from the previous occupant of the slot.
            for _, base in ipairs({0x164a,0x1632,0xc2,0x151c,0x1528,0x1534,
                0x157c,0x1588,0x15c4,0x1602,0x1540,0x154c,0x1558,0x1564,
                0x1fe2,0x1626,0x1570,0xb6,0x14f8,0xaa,0x14ec,0x15dc,
                0x15d0,0x163e,0x187b,0x160e,0x1594,0x1504,0x1fd6}) do w(base+s,0) end
            local destinations = {0x1656,0x1662,0x166e,0x167a,0x1686,0x190f}
            local sources = {0x3f26c,0x3f335,0x3f3fe,0x3f4c7,0x3f590,0x3f659}
            for i, base in ipairs(destinations) do
                w(base+s,memory.read_u8(sources[i]+id,"CARTROM"))
            end
            w(0x15f6+s,r(0x166e+s) & 15); w(0x15a0+s,1)
            -- Dynamic spawn: no level-list entry to erase.
            w(0x9e+s, id); w(0xe4+s, x & 255); w(0x14e0+s, x >> 8)
            w(0xd8+s, y & 255); w(0x14d4+s, y >> 8)
            w(0x161a+s, 0xff); w(0x14c8+s, 1)
            return s
        end
    end
    error("all 10 ordinary sprite slots are occupied")
end
function smw.fireball()
    in_level()
    if r(0x9d) ~= 0 or r(0x71) ~= 0 then return nil end
    -- Only slots 8/9 have valid player-fireball OAM mappings. The stock draw
    -- path indexes $02:9FA3: slots 0..7 yield $05,$03,$02,... (unaligned OAM
    -- offsets), corrupting unrelated sprites. A ten-entry simulation table
    -- does not imply ten drawable player fireballs. Never steal other slots.
    for s = 9, 8, -1 do
        if r(0x170b+s) == 0 then
            local right = r(0x76) ~= 0
            local x, y = r16(0x94) + (right and 8 or 0), r16(0x96)+8
            for _, base in ipairs({0x1751,0x175b,0x1765,0x176f,0x1779}) do w(base+s,0) end
            w(0x171f+s,x & 255); w(0x1733+s,(x >> 8) & 255)
            w(0x1715+s,y & 255); w(0x1729+s,(y >> 8) & 255)
            w(0x173d+s,0x30); w(0x1747+s,right and 3 or 0xfd)
            w(0x1779+s,r(0x13f9)); w(0x170b+s,5)
            w(0x149c,10); w(0x1dfc,6)
            smw.shots = smw.shots+1
            return s
        end
    end
    smw.missed_shots = smw.missed_shots+1
    return nil
end
function smw.autofire(interval)
    integer(interval, 0, 600, "interval in frames (0 disables)")
    event.unregisterbyname("smw.autofire")
    event.unregisterbyname("smw.holdfire")
    if game and game.command then game.command("fire_stream", "0") end
    smw.fire_interval = interval
    if interval == 0 then return end
    local next_frame = emu.framecount()
    event.onframestart(function()
        if r(0x100) ~= 0x14 or r(0x109) ~= 0 or r(0x9d) ~= 0 or r(0x71) ~= 0 then return end
        w(0x19,3)
        if emu.framecount() >= next_frame then
            smw.fireball()
            next_frame = emu.framecount()+smw.fire_interval
        end
    end, "smw.autofire")
end
function smw.invincible(enabled)
    assert(type(enabled) == "boolean", "enabled must be boolean")
    event.unregisterbyname("smw.invincible")
    if enabled then
        event.onframestart(function()
            if r(0x100) == 0x14 then w(0x1497,2) end
        end, "smw.invincible")
    end
end
function smw.stop()
    smw.autofire(0)
    event.unregisterbyname("smw.invincible")
    if game and game.command then game.command("fire_stream_reset") end
end
function smw.fire_stream(rate)
    in_level()
    integer(rate,0,1000,"fireballs per second")
    smw.autofire(0)
    return game.command("fire_stream",tostring(rate))
end
function smw.stream_status()
    return game.command("fire_stream_status")
end
function smw.holdfire(rate)
    in_level()
    integer(rate,0,1000,"fireballs per second (0 disables)")
    smw.autofire(0)
    if rate == 0 then return end
    local previous = 0
    event.onframestart(function()
        local buttons = joypad.get(1)
        local firing = (buttons.Y or buttons.X) and r(0x100) == 0x14
            and r(0x109) == 0 and r(0x71) == 0
        local requested = firing and rate or 0
        if requested ~= previous then
            game.command("fire_stream",tostring(requested))
            previous = requested
        end
    end,"smw.holdfire")
end
return "SMW helpers loaded"
