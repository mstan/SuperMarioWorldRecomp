# SuperMarioWorldRecomp

Static recompilation of *Super Mario World* (SNES) into native C,
using the [snesrecomp](https://github.com/mstan/snesrecomp) framework.
This repo is the per-game side: the runtime, the recompiled C output,
the per-game `.cfg`, and the build glue.

> ## ⚠️ Heavily Work-In-Progress — NOT A PLAYABLE BUILD
>
> The recompiled binary boots and renders the title screen and attract
> demo, but **the game is not playable**. Active gameplay (entering a
> level, controlling Mario, completing a stage) has not been
> end-to-end verified and is **assumed broken**.
>
> Treat this repo as an in-progress engineering snapshot, not a
> release. Expect:
> - Branches that don't build.
> - Internal docs that assume context from active development.
> - APIs and recompiler output that change without notice.
> - Known visual and behavioral bugs even in the parts that "run."

## What works (sort of)

- Boot and title screen render.
- Attract-demo cinematic plays through and renders.

## Known visible bugs in the attract demo

Even the parts that render have moderate visible bugs:

- Berries render with the wrong palette (appear as `?` blocks).
- Some enemies are missing entirely.
- Some enemies are invisible but still interact (stompable, etc.).
- `?` blocks do not respond to being hit.
- Physics on sloped surfaces is incorrect (Mario sinks / mis-aligns).

This list is non-exhaustive — additional bugs almost certainly exist
in code paths the attract demo doesn't exercise.

## In-game gameplay

**Not verified.** Past the attract demo, no part of the game has been
manually played end-to-end. Anything beyond "the screen renders"
should be assumed to be broken.

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
`[GamepadMap]` section of `smw.ini` (auto-generated next to the exe
on first run); the recognized names and the full mapping table are
in [`CONTROLLER.md`](CONTROLLER.md).

System shortcuts (configured in `smw.ini`'s `[KeyMap]` section):

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

## License

Not yet declared. Code in this repo is original; vendored
dependencies under `third_party/` retain their own licenses.

The SMW ROM and any data extracted from it are **not** in this
repo and are not licensed for redistribution.
