# SMW Lua / TCP playground

This spike runs BizHawk-style Lua inside the native game and accepts commands
over a local TCP server. Both worktrees use branch `codex/bizhawk-lua-tcp-spike`:

- Framework: `F:\Projects\snesrecomp\_wt-bizhawk-lua-snesrecomp`, based on
  freshly fetched `origin/main` `43e857d`.
- Game: `F:\Projects\snesrecomp\_wt-bizhawk-lua-smw`, based on freshly fetched
  `origin/main` `703786e`.

The game CMake cache explicitly selects the paired framework worktree via
`SNESRECOMP_ROOT`; the existing source checkouts and their local edits are
untouched. The framework's `docs/LUA_TCP.md` lists the supported API and wire
protocol. Reference: [BizHawk Lua functions](https://tasvideos.org/Bizhawk/LuaFunctions).

## Try the built spike

From the game worktree in PowerShell (native Python 3 required):

```powershell
.\tools\lua\start.ps1 -Paused
$python = 'C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe'
$client = '..\_wt-bizhawk-lua-snesrecomp\tools\lua_tcp.py'
# Wait for the game window/server to finish booting, then:
& $python $client load tools/lua/smw.lua
& $python $client run tools/lua/enter_level.lua
& $python $client status
# Wait for running=false: the script navigates from title to Yoshi's Island 1.
& $python $client eval 'return smw.status()'
& $python $client eval 'smw.powerup(3); smw.invincible(true)'
& $python $client eval 'return smw.spawn(0x0f)'  # Goomba, 64 pixels ahead
& $python $client eval 'return smw.spawn(0x04)'  # green Koopa
& $python $client eval 'smw.autofire(4)'         # attempt a shot every 4 frames
& $python $client resume
& $python $client eval 'smw.autofire(24)'        # slower cadence, live
& $python $client eval 'smw.teleport(128,352)'
& $python $client eval 'smw.stop()'
```

The helper definitions persist across CLI connections. `pause` and `step 60`
let you inspect state deterministically. `reset` removes all Lua globals,
callbacks and input overrides, while leaving the game's RAM intact. Reload
`smw.lua` after a reset. Use `stop` to stop only the running frame-loop script;
`smw.stop()` removes the gameplay helper callbacks. Close the game window to
stop the server. `start.ps1 -Port 4382` and client `--port 4382` support a
second independent instance.

Powerups use 0=small, 1=big, 2=cape, 3=fire. Teleport coordinates are level
pixels; choose valid terrain. Spawning accepts stock sprite IDs 0..0xC8, but
sprite graphics and behavior still depend on the level's loaded graphics and
object context. The demonstrated IDs are Goomba and green Koopa. Spawn
initializes the slot's tables from the ROM and lets the normal game INIT run.

Autofire creates genuine type-5 extended sprites using SMW's own fireball
state layout; the game handles movement, rendering and collision. This is a
scripted firing mode, not a patch to the stock Y-button cooldown. It uses only
the two supported player-fireball slots (8/9), never overwrites occupied slots,
and reports skipped attempts in `smw.missed_shots`. Interval
0 disables it. `smw.fireball()` fires once. Helpers reject the opening message
and require a playable level; player transitions and sprite-lock frames
suspend autofire.

The earlier ten-fireball version was incorrect: the stock player-fireball
renderer maps slots 0..7 to unaligned OAM offsets, producing garbled objects.
Having ten extended-sprite simulation entries does not mean all ten can draw
player fireballs. Additional simultaneous fireballs would require deliberate
renderer/OAM allocation changes. Ordinary spawned enemies now use slots 0..9,
which the stock fireball collision loop checks, rather than special slots
10/11. Both restrictions have regression coverage.

Arbitrary Lua is supported, for example:

```powershell
& $python $client eval 'return memory.read_u16_le(0x94,"WRAM")'
& $python $client eval 'mainmemory.write_u8(0x0dbf,99)' # coins
& $python $client eval 'event.onframestart(function() joypad.set({Right=true,B=true},1) end,"runjump")'
& $python $client eval 'event.unregisterbyname("runjump")'
```

This is a compatibility subset: memory, input, frame events and coroutines;
GUI drawing, savestates, movies and instruction hooks are not implemented.
The server binds localhost and is a trusted development interface. It runs on
the game thread without enabling the heavyweight TCP trace debugger.

## Rebuild

`recomp-ui` is initialized at the game's tracked pin. Supply your own verified
stock USA `smw.sfc`. Generated game code is untracked. Native Windows tools
must be invoked explicitly on this machine because PATH contains MSYS shims.

```powershell
$env:SNESRECOMP_ROOT='../_wt-bizhawk-lua-snesrecomp'
$env:SNESRECOMP_ANALYSIS_BACKEND='python'
$env:PYTHON='C:/Users/Matthew/AppData/Local/Programs/Python/Python312/python.exe'
& C:\msys64\usr\bin\bash.exe tools/regen.sh --stock --no-tests
& C:\msys64\mingw64\bin\cmake.exe -S . -B build-lua -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 `
  -DPython3_EXECUTABLE=C:/Users/Matthew/AppData/Local/Programs/Python/Python312/python.exe `
  -DSNESRECOMP_ROOT=F:/Projects/snesrecomp/_wt-bizhawk-lua-snesrecomp `
  -DSNESRECOMP_ENABLE_LUA=ON
& C:\msys64\mingw64\bin\cmake.exe --build build-lua -j 6
Copy-Item -LiteralPath config.ini -Destination build-lua/config.ini
```

The build fetches pinned MIT Lua 5.4.9 from lua.org when enabled. Without the
option there is no Lua dependency/listener. A small build compatibility fix
selects the framework audio helper when present, because latest SMW and latest
framework otherwise define the same functions twice. Co-op plus Lua is
explicitly rejected for this spike.

## Reproduce validation

```powershell
$env:PATH='C:\msys64\mingw64\bin;' + $env:PATH
& $python tools/lua/validate.py `
  --engine ../_wt-bizhawk-lua-snesrecomp `
  --exe build-lua/SuperMarioWorldSNESRecomp.exe --rom smw.sfc
```

The test launches a separate game on port 4381, navigates via Lua input, runs
live assertions, then terminates only its own process. `--keep-running` leaves
the successful instance paused for inspection (its test process uses turbo).
Evidence is written to `build-lua/lua-validation.json` and
`build-lua/lua-validation-game.log`.

Validated September 11, 2026 with the stock 524288-byte USA ROM:

- WRAM signed/endian operations, ROM reads, domain/boundary rejection, Lua
  instruction/memory limit recovery, JSON escaping, TCP fragmentation and
  pipelined requests passed.
- Exact stepping, frame callback removal/error isolation, coroutine
  `emu.frameadvance`, input-driven navigation, and VM reset passed.
- Reached Yoshi's Island 1 (translevel 41); changed Mario's powerup and moved
  him from x=16 to x=32.
- Goomba in slot 9 transitioned from INIT to active and moved x=96 to x=91
  after 24 further frames. A fireball moved x=40 to x=49 in three frames.
- With both player-fireball slots occupied, a new shot is skipped without
  writing a type-5 projectile into any of slots 0..7. Fireballs can hit the
  spawned enemy and convert it into a coin.
- Over 96 frames, interval 48 produced 2 shots; interval 4 produced 4 shots
  plus 20 attempts skipped because the two-slot pool was occupied. The earlier
  16-shot measurement used the invalid OAM slots and is not a valid result.

The validation is a spike demonstration, not a full-game or full-BizHawk
compatibility certification.
