-- From a fresh boot/title, navigate to Yoshi's Island 1 using real inputs.
-- Use TCP `run`; this script advances itself even when Lua pause is active.
local function wait(n)
    for _ = 1, n do emu.frameadvance() end
end
local function press(key, hold, settle)
    for _ = 1, hold do joypad.set({[key]=true},1); emu.frameadvance() end
    wait(settle)
end
-- Bound every wait so a wrong menu state produces a useful error.
for _ = 1, 900 do
    if mainmemory.read_u8(0x100) == 7 then break end
    emu.frameadvance()
end
assert(mainmemory.read_u8(0x100) == 7, "title screen not reached")
press("Start",1,40)
press("A",1,40) -- MARIO A
press("A",1,600) -- one player, opening message
for i = 1, 1200 do
    if mainmemory.read_u8(0x100) == 0x0e then break end
    if i % 60 == 1 then joypad.set({B=true},1) end -- dismiss opening text
    emu.frameadvance()
end
assert(mainmemory.read_u8(0x100) == 0x0e, "overworld not reached")
wait(60)
press("Left",1,90) -- Yoshi's Island 1
press("A",1,180)
assert(mainmemory.read_u8(0x100) == 0x14, "level not reached; inspect smw.status()")
assert(mainmemory.read_u8(0x109) == 0, "still in opening message")
print("Entered Yoshi's Island 1")
