# SuperMarioWorldRecomp

> _This recompilation is a **byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp)** — the games are the proving ground, the framework is the goal.
> **These are in-development previews, not finished ports — expect rough
> edges**, and depth will keep landing over months, not days. My time for any
> one title is limited, so I ask for your patience. Contributions are welcome —
> testing, issues, and PRs to the game or framework all help and will
> accelerate this game's polish. More on the why at:
> [Recomp + AI: 5 Months Later »](https://1379.tech/recomp-ai-5-months-later/)_

Static recompilation of *Super Mario World* (SNES) into native C,
using the [snesrecomp](https://github.com/mstan/snesrecomp) framework.
This repo is the per-game side: the runtime, the recompiled C output,
the per-game `.cfg`, and the build glue.

## What "static recompilation" means here

The 65816 CPU code from the ROM is statically translated to C — every
function the game runs on the SNES's main CPU is a real generated C
function in `src/gen/`. **The rest of the SNES is not recompiled** —
it's hardware. PPU rendering, the APU / SPC700 audio coprocessor, DMA
and HDMA channels, hardware register I/O, and bank-mapping all run
through a C reimplementation of the SNES hardware in
`snesrecomp/runner/src/snes/` — a [LakeSnes](https://github.com/elzo-d/LakeSnes)-derived
core (the same emulator family the [snesrev](https://github.com/snesrev)
reverse-engineered ports use), with individual algorithms credited to
snes9x. This is the same model other static-recomp projects (N64Recomp,
snesrev/zelda3, etc.) use: recompile the CPU, emulate the silicon. If
you expected the PPU and APU to be recompiled
too, they aren't — and a static recompiler can't recompile them
because the SNES PPU has no instruction stream and the SPC700 is a
separate processor with its own firmware that the cartridge uploads
to a separate chip.

## Current status: believed fully playable

Hand-verified end-to-end through:
- **Yoshi's Island (World 1)** including Iggy's castle boss.
- **Donut Plains (World 2)** end-to-end.
- **Vanilla Dome (World 3)** in progress at time of writing.

No catastrophic visible regressions surfaced through these worlds.
Two runtime tripwires (the M/X claim verifier and the
async-cpu->m_flag/x_flag-write detector) are armed at boot and have
not latched on the verified worlds. An off-rails event was captured
in `BufferScrollingTiles_Layer1_VerticalLevel_M1X1` during Donut
Plains castle (bank-out-of-range pointer read; runtime mirrors the
read to a safe location and gameplay continues) — see
[`ISSUES.md`](ISSUES.md) for the bucketed capture and the
`offrails_get` TCP query.

Worlds 4–7 and special content (Star Road, Special World) are not
yet hand-verified but are expected to play similarly. If you hit a
visible regression, please open an issue with a savestate and the
`offrails_get` / `mx_async_check_get` JSON snapshots.

Active development; expect:
- Some branches don't build; only `main` is guaranteed to build.
- Internal docs (`ISSUES.md`, `ENHANCEMENTS.md`) assume context.
- APIs and recompiler output change without notice.

See [`RELEASE.md`](RELEASE.md) for the latest release notes.

## Quick start (pre-built release)

1. Download the latest `SuperMarioWorldRecomp-windows-x64.zip` from
   [Releases](../../releases) and extract it.
2. Run `smw.exe`. On first launch a file picker asks for your
   **legally-obtained** Super Mario World (USA) ROM (`.sfc` / `.smc`).
   The path is remembered in `rom.cfg` next to the exe.
3. Edit `keybinds.ini` (auto-generated next to the exe on first run)
   to remap keys, then restart.

The ROM is **never** redistributed — supply your own dump.

## Widescreen

The one-player launcher's **Aspect ratio** setting offers three view modes: **Standard
(4:3)**, **16:9 fixed**, and **Adaptive**. The same selection can be set under
`[Graphics]` in `config.ini` with `Widescreen = Standard`, `Fixed16x9`, or
`Adaptive`.

Both widescreen modes retain the native 224-pixel logical height. Fixed mode
always renders a roughly 398-pixel-wide 16:9 view. Adaptive derives the logical
width from the live window or fullscreen aspect ratio, so resizing a window
wider reveals more of the level; at the native aspect it returns to the
authentic 256-pixel view.

The in-level status bar follows the wider view, and SMW's spawn and culling
logic expands with the visible margins. Screens without valid extended level
terrain, including the title screen, overworld, and transitions, remain
centered and pillarboxed. The maximum logical width is 446 pixels because wider
views cannot represent every sprite safely in the SNES's 9-bit OAM coordinate
space.

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

## Controls (default `keybinds.ini`)

| SNES button | Default key |
|-------------|-------------|
| D-Pad       | Arrow keys |
| A           | X |
| B           | Z |
| X           | S |
| Y           | A |
| L           | C |
| R           | V |
| Start       | Enter |
| Select      | Right Shift |

Player 2 is unbound by default — fill in keys in `keybinds.ini` to enable a
second keyboard player. In the co-op build, the two bindings drive Mario and
Luigi at the same time rather than taking turns.

**Xbox / PlayStation / Switch Pro controllers** are auto-detected via
SDL_GameController (XInput on Windows). Plug it in before launching,
or hot-plug after. Default Xbox mapping is **position-true**: the
physical button position matches a SNES pad — so Xbox A (south face)
sends SNES B, Xbox B (east face) sends SNES A. To rebind, edit the
`[GamepadMap]` section of `config.ini` (auto-generated next to the exe
on first run); the recognized names and the full mapping table are
in [`CONTROLLER.md`](CONTROLLER.md).

System shortcuts (configured in `config.ini`'s `[KeyMap]` section):

| Action          | Default     |
|-----------------|-------------|
| Save state 1-10 | Shift+F1..F10 |
| Load state 1-10 | F1..F10 |
| Toggle pause    | P |
| Reset           | Ctrl+R |
| Toggle fullscreen | Alt+Enter |
| Turbo (fast-forward) | Tab |
| Toggle renderer | R |
| Display perf    | F |

## Building from source

Prerequisites: Windows 10+, Visual Studio 2022 (with C++ desktop
workload), Python 3.9+ on PATH, and `rustup` for regeneration.

```bash
git clone --recurse-submodules https://github.com/mstan/SuperMarioWorldRecomp
cd SuperMarioWorldRecomp
```

Regenerate the desired variant from your legally obtained stock ROM, then
build it. The generated C is intentionally untracked:

```bash
# Normal 1P/MSU build (the default)
bash tools/regen.sh --stock
msbuild smw.sln /p:Configuration=Production /p:Platform=x64 /m

# Simultaneous co-op build
bash tools/regen.sh --coop
msbuild smw.sln /p:Configuration=CoopProduction /p:Platform=x64 /m
```

The normal configuration remains `smw.exe`; the additive configuration creates
`SuperMarioWorldCoopSNESRecomp.exe` in its own output directory and copies
`smw_coop.ips` beside it. CMake builds the normal target by default; configure
with `-DSMW_BUILD_COOP=ON` to make both targets available in one build tree.

The netplay-enabled co-op target uses CMake so it can link recomp-net. A sibling
engine worktree can be selected without replacing the checked-in submodule:

```powershell
$engine = 'F:\path\to\snesrecomp-worktree'
$sdl = '.\packages\sdl2.nuget.2.26.3\build\native'
$env:SNESRECOMP_ROOT = $engine
bash tools/regen.sh --coop --no-tests
cmake -S . -B build-netplay -DSMW_BUILD_COOP=ON `
  -DSNESRECOMP_ROOT="$engine" -DSMW_SDL2_ROOT="$sdl"
cmake --build build-netplay --config Release `
  --target SuperMarioWorldCoopSNESRecomp --parallel
```

### Regenerating the recompiled C (contributors)

If you change anything under `recomp/bank_*.cfg`, the snesrecomp
framework, or otherwise need to re-run the recompiler:

1. Drop a legally-obtained `smw.sfc` at the repo root (`.gitignore`
   excludes it).
2. Run `bash tools/regen.sh --stock` for the normal build and/or
   `bash tools/regen.sh --coop` for co-op. Stock emits to `src/gen/` from the
   existing MSU-capable analysis image. Co-op applies the bundled IPS to a
   throwaway verified ROM, layers the small CFG fragments in `recomp/coop/`,
   and emits independently to `src/gen-coop/`. It builds and requires the fast
   native analyzer by default; set
   `SNESRECOMP_ANALYSIS_BACKEND=python` only to use the slower reference path.
3. Rebuild as above.

(Build and run instructions are not yet stable — see scripts under
`tools/` and notes in `docs/` for the current shape, but expect them
to drift.)

## MSU-1 audio

The normal 1P build supports CD-quality MSU-1 streaming music using a stock
SMW (USA) ROM. Regeneration applies Conn's audio-only "SMW MSU-1" patch to a
throwaway copy and compiles the driver into the executable. At runtime, no
pack means authentic SPC audio; a matching PCM pack plus MSU-1 enabled in the
launcher means streamed music. Packs for SMW MSU+ or SMW MSU-1 Plus Ultra are
not interchangeable with this audio-only patch. Full credit and pack details
are in [`recomp/msu1/ATTRIBUTION.md`](recomp/msu1/ATTRIBUTION.md).

MSU-1 is disabled only in the simultaneous co-op build. Both patches alter
expansion ROM data and cannot be layered into one byte-exact analysis image.

## Repo layout

- `src/` — runtime C (CPU state, runtime helpers, hand-written
  bodies for things the framework doesn't yet recompile).
- `src/gen/` — recompiler output (do not hand-edit).
- `src/gen-coop/` — separate co-op recompiler output (do not hand-edit).
- `recomp/` — per-bank `.cfg` files describing what the framework
  cannot yet derive from the ROM (data regions, calling conventions,
  rare hints).
- `recomp/coop/` — co-op CFG overlays, distributed IPS, and attribution.
- `snesrecomp/` — pinned framework submodule.
- `tools/` — build, regen, audit, and triage scripts.
- `docs/` — design / debugging notes (internal-facing, may be stale).
- `third_party/` — vendored deps with their own licenses.

## Acknowledgements

This port did not start from scratch. It stands on prior
reverse-engineering and emulation work, and we're grateful for it:

- **[IsoFrieze/SMWDisX](https://github.com/IsoFrieze/SMWDisX)** — the
  Super Mario World disassembly used as the basis for this port. SMWDisX
  is the source of the symbol names, the RAM/variable map, and the
  per-bank function boundaries, and it serves as the differential
  conformance oracle for the recompiled output (see
  [`tools/smwdisx_compare.py`](tools/smwdisx_compare.py) and the vendored
  `SMWDisX/` clone). SMWDisX in turn credits mikeyk's original 2013
  disassembly and loveemu's SPC700 sound-engine work.
- **[snesrev](https://github.com/snesrev)** (`snesrev/smw`,
  `snesrev/zelda3`) — the runner and surrounding ecosystem were heavily
  based on the snesrev reverse-engineered ports: the "port the CPU code
  to C, emulate the rest of the silicon, verify against a reference
  emulator" model, the C runtime structure, and the SPC-image audio path.
- The C SNES hardware core under `snesrecomp/runner/src/snes/` derives
  from **[LakeSnes](https://github.com/elzo-d/LakeSnes)** (elzo-d), the
  emulator snesrev's projects vendor, with algorithms credited inline to
  **snes9x**.

See the [snesrecomp framework](https://github.com/mstan/snesrecomp)
README for the full framework-side attribution.

## Discord

https://discord.gg/S4MvUGQFwd

## License

Not yet declared. Code in this repo is original except where noted in
**Acknowledgements** above; vendored dependencies under `third_party/`
(and the `SMWDisX/` disassembly clone) retain their own licenses.

The SMW ROM and any data extracted from it are **not** in this
repo and are not licensed for redistribution.

---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
