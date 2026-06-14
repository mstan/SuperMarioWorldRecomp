# SuperMarioWorldRecomp

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

Player 2 is unbound by default — fill in keys in `keybinds.ini` to
enable a second keyboard player.

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
workload), Python 3.9+ on PATH.

```bash
git clone --recurse-submodules https://github.com/mstan/SuperMarioWorldRecomp
cd SuperMarioWorldRecomp
```

The `snesrecomp/` directory is a [sibling repo](https://github.com/mstan/snesrecomp)
accessed via a junction/symlink. If you don't already have it checked
out next to this repo, clone it:

```bash
git clone https://github.com/mstan/snesrecomp ../snesrecomp
```

Then build:

```bash
# From a Developer Command Prompt for VS 2022, or with MSBuild on PATH:
msbuild smw.sln /p:Configuration=Release /p:Platform=x64 /m
```

The recompiled C in `src/gen/` and the `recomp/funcs.h` declarations
are committed and built directly — no ROM is required at build time.
Run the exe and the runtime ROM-picker handles the rest.

### Regenerating the recompiled C (contributors)

If you change anything under `recomp/bank_*.cfg`, the snesrecomp
framework, or otherwise need to re-run the recompiler:

1. Drop a legally-obtained `smw.sfc` at the repo root (`.gitignore`
   excludes it).
2. Run `bash tools/regen.sh`. This drives `snesrecomp/recompiler/`
   over the ROM and rewrites `src/gen/*.c`, `recomp/funcs.h`, and
   the per-bank registry.
3. Rebuild as above.

(Build and run instructions are not yet stable — see scripts under
`tools/` and notes in `docs/` for the current shape, but expect them
to drift.)

## MSU-1 audio

This build supports [MSU-1](https://sneslab.net/wiki/MSU1) — CD-quality
streaming music in place of the SPC soundtrack — using your **stock** SMW
(USA) ROM.

**You don't patch anything.** The MSU-1 driver is **Conn's "Super Mario
World MSU-1"** patch (the *audio-only* music-replacement patch — no gameplay
changes). The regen step applies the bundled patch
([`recomp/msu1/`](recomp/msu1/)) to a throwaway copy of your stock ROM and
recompiles the driver into the binary, so at runtime you just load your stock
ROM: no pack → authentic SPC audio; matching pack + MSU-1 enabled → streamed
music. Sound effects always stay on the SPC.

**Enable it** in the launcher (Settings → Audio → MSU-1), then drop a pack in
the MSU-1 folder (defaults to `msu/` next to the exe). Or headless:

```sh
SNESRECOMP_MSU1=/path/to/smw_msu_pack  smw.exe smw.sfc
```

> ⚠ **There are three different SMW MSU-1 patches, and their PCM packs are NOT
> interchangeable.** We use **"SMW MSU-1"** (Conn, audio-only —
> [zeldix t1436](https://www.zeldix.net/t1436-super-mario-world-native)). A pack
> built for **SMW MSU+** ([t1437](https://www.zeldix.net/t1437-super-mario-world-msu))
> or **SMW MSU-1 Plus Ultra** ([t2535](https://www.zeldix.net/t2535-super-mario-world-msu-1-plus-ultra-130-tracks-total))
> will play the wrong tracks. Use a pack made for the audio-only "SMW MSU-1"
> patch.

### Thanks

The MSU-1 driver is **not** ours — it's **Conn's** Super Mario World MSU-1
patch (with thanks to Ikari_01, EmuandCo, Kiddo and the SMW Central / Zeldix
MSU-1 community), shared freely. We bundle it with gratitude; full credit and
the which-patch / pack-matching details are in
[`recomp/msu1/ATTRIBUTION.md`](recomp/msu1/ATTRIBUTION.md).

## Repo layout

- `src/` — runtime C (CPU state, runtime helpers, hand-written
  bodies for things the framework doesn't yet recompile).
- `src/gen/` — recompiler output (do not hand-edit).
- `recomp/` — per-bank `.cfg` files describing what the framework
  cannot yet derive from the ROM (data regions, calling conventions,
  rare hints).
- `snesrecomp/` — symlink to a sibling clone of the
  [snesrecomp framework](https://github.com/mstan/snesrecomp).
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
