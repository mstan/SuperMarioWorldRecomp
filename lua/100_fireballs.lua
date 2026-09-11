-- Load through lua_tcp.py after starting SMW with SNESRECOMP_LUA_PORT=4380.
-- Safe to load at the title screen. No ROM patch or other helper is needed.
-- Hold the normal fire/run buttons: keyboard A / S by default, SNES Y / X.
local RATE = 100 -- fireballs per 60 active gameplay frames
local r = mainmemory.read_u8
event.unregisterbyname("example.fire_stream")
game.command("fire_stream_reset")
local previous = 0
event.onframestart(function()
    local buttons = joypad.get(1)
    local firing = (buttons.Y or buttons.X) and r(0x100) == 0x14
        and r(0x109) == 0 and r(0x71) == 0
    local requested = firing and RATE or 0
    if requested ~= previous then
        game.command("fire_stream", tostring(requested))
        previous = requested
    end
end, "example.fire_stream")
print("Hold A / S (SNES Y / X) for 100 fireballs/second. TCP reset disables it.")
