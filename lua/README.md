# Optional Lua playground

Lua is inactive during normal play. These release builds include the feature;
source builds enable it with `-DSNESRECOMP_ENABLE_LUA=ON` (default OFF).
The server binds only `127.0.0.1` and starts only when `SNESRECOMP_LUA_PORT`
is set. This is a trusted local development interface with a BizHawk-style API
subset, not full BizHawk compatibility.

## Windows

From the extracted release directory in PowerShell, start the game:

```powershell
$env:SNESRECOMP_LUA_PORT = '4380'
Start-Process -FilePath .\SuperMarioWorldSNESRecomp.exe
```

Select your own Super Mario World (USA) ROM in the launcher and start playing.
With Python 3 installed, run in that same directory:

```powershell
python lua/lua_tcp.py load lua/100_fireballs.lua
```

## Linux / Steam Deck

On first launch the AppImage copies its bundled `lua/` examples beside itself
without replacing existing files. From that directory:

```bash
SNESRECOMP_LUA_PORT=4380 ./SuperMarioWorldRecomp-linux-0.12.0-x86_64.AppImage
# In a second terminal, after starting the game through the launcher:
python3 lua/lua_tcp.py load lua/100_fireballs.lua
```

## Play and experiment

Hold **A or S on the default keyboard layout** (SNES Y/X) to emit 100 fireballs
per second at normal speed. Release to stop new shots. Mario gains fire power
while firing; movement and jumping still work. The script can be loaded at
the title screen and waits for a playable level.

```bash
python lua/lua_tcp.py eval 'return game.command("fire_stream_status")'
python lua/lua_tcp.py reset
```

`reset` removes the script and extra projectiles. Close the game and launch
without `SNESRECOMP_LUA_PORT` for normal play with no listener. In PowerShell,
remove the setting with `Remove-Item Env:SNESRECOMP_LUA_PORT` before relaunching.

The included `smw.lua` provides additional helpers: load it with the client,
then try `smw.holdfire(100)`, `smw.fire_stream(100)` for continuous emission,
`smw.spawn(0x04)` for a green Koopa, or `smw.stop()`. Reset before switching
between the standalone example and the helper callbacks.

The 256-entry stream pool runs stock projectile movement/collisions in private
working memory and draws separately from SNES OAM. It uses the native
256-pixel camera bounds and draws over the finished frame; foreground priority,
window effects and color math are not reproduced for extra projectiles.
Sprite locks/transitions suspend emission. Very high rates can fill the pool;
status reports dropped shots. Extra pool/Lua state is not saved in savestates.
This example supports the stock single-player USA game, not co-op.

Lua 5.4.9 is distributed under the included MIT license.
