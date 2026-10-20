# Legacy IPS co-op build

This documents the older, separate `SMW_BUILD_COOP=ON` executable.
The experimental native co-op mod in the normal executable is the current
prototype; see [the README](../README.md#experimental-native-simultaneous-co-op).
The historical IPS build and its network setup below do not describe the
native mod.

## Simultaneous co-op build

The repository also produces a separate `SuperMarioWorldCoopSNESRecomp` build. It
uses the simultaneous co-op gameplay patch while keeping the normal legal ROM
flow: select an untouched Super Mario World (USA) ROM in the launcher or pass
it on the command line.

On first use, the executable verifies the stock ROM and applies the bundled IPS
delta. The IPS contains the co-op hack's changed bytes, but not a complete ROM.
The generated file is written beside the executable as
`<stock-rom-name>.coop.sfc`; that generated co-op ROM is what the runtime loads.
Subsequent launches reuse it after verifying its CRC. Both headerless `.sfc`
dumps and 512-byte-headered `.smc` dumps are accepted as input. To force a clean
repatch, delete the generated `.coop.sfc` file.

Player 1 and Player 2 are active simultaneously in levels. Connect two SDL
game controllers, or configure the `[Player1]` and `[Player2]` keyboard
bindings in `keybinds.ini`. The stock ROM and generated `.coop.sfc` remain
local and must not be redistributed.

Widescreen is currently disabled in the co-op build, and its launcher omits the
aspect-ratio control. Co-op always runs in **Standard (4:3)** even if an older
configuration or `SNESRECOMP_WIDESCREEN` requests another mode. The experimental
IPS-specific hooks remain in the source for future work, but extended terrain
streaming is not yet reliable during normal scrolling.

### Network co-op

The co-op executable supports two-player delay-sync netplay through
`snesrecomp`'s `recomp-net` integration and recomp-ui. Both players must use the
same build and a matching verified SMW (USA) ROM.

1. Start `SuperMarioWorldCoopSNESRecomp` and open **Netplay** in the launcher.
   The first visit asks for a player name and saves it for later launches.
2. Choose **Host Lobby**. **Online** publishes the room through the configured
   lobby server; **LAN / Direct IP** advertises it directly on the local
   network. **Create** immediately opens the waiting room.
3. The guest joins the matching **Super Mario World Co-op** room. Once both
   seats are occupied, the host selects **Play**; the guest does not need a
   separate ready action.
4. The host controls Mario (network slot 1) and the guest controls Luigi
   (network slot 2). Each machine uses its Player 1 keyboard/controller by
   default, so the guest does not need separate Player 2 bindings.

Closing the game or pressing Escape during a network match returns both
players to the room for a rematch. Use **Leave Lobby** to disconnect instead.

The simulation waits for confirmed inputs before every frame. Turbo, pause,
and local reset are disabled during a session; save/load synchronization is
host-authoritative. LAN uses UDP directly. Internet games may require a build
configured with `-DSNESRECOMP_NET_ICE=ON`, which includes libjuice-based NAT
traversal, when the peers cannot reach each other directly.

For a launcher-free loopback or CI smoke test, start two instances with the
same session ID and opposite ports:

```powershell
# Host
$env:SNES_NETPLAY='1'; $env:SNES_NET_SLOT='0'
$env:SNES_NET_SESSION_ID='4242'; $env:SNES_NET_TRANSPORT='lan'
$env:SNES_NET_BIND='0.0.0.0:7777'; $env:SNES_NET_PEER='127.0.0.1:7778'
$env:SNES_NET_TEST_TICKS='600'
.\SuperMarioWorldCoopSNESRecomp.exe .\smw.sfc

# Guest (in another terminal)
$env:SNES_NETPLAY='1'; $env:SNES_NET_SLOT='1'
$env:SNES_NET_SESSION_ID='4242'; $env:SNES_NET_TRANSPORT='lan'
$env:SNES_NET_BIND='0.0.0.0:7778'; $env:SNES_NET_PEER='127.0.0.1:7777'
$env:SNES_NET_TEST_TICKS='600'
.\SuperMarioWorldCoopSNESRecomp.exe .\smw.sfc
```

### Co-op hack attribution

This build distributes an IPS delta for **Super Mario World - 2 Player
Simultaneous Co-op Hack**. Original hack credits:

- **Noobish Noobsicle** - creator of the original SMW co-op hack
- **Bloony Fox** and **NesDraug** - developed it into the full hack used here

Source and original credits:
[Romhack Plaza - Super Mario World - 2 Player Simultaneous Co-op Hack](https://romhackplaza.org/romhacks/super-mario-world-2-player-co-op-hack-snes/).
This recompilation project is not affiliated with or endorsed by the hack
authors.
