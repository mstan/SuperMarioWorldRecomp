# macOS build

Native macOS build of Super Mario World Recomp, producing a self-contained
`SuperMarioWorld.app` (Apple Silicon or Intel). Tested on a MacBook Air M5
(arm64) with macOS 27 and SDL 3.4.

The ROM is never redistributed: supply your own legally obtained
Super Mario World (USA) dump (SHA-256 `0838e531…7f5b`, 524,288 bytes, no header).

## Quick start

Prerequisites: Xcode Command Line Tools (`xcode-select --install`), Python 3,
and [Homebrew](https://brew.sh). The script installs the remaining tools
(`cmake`, `sdl3`, `dylibbundler`, and `rust` for the one-time code generation).

```bash
git clone --recurse-submodules -b macos-port https://github.com/Jp4Real/SuperMarioWorldRecomp
cd SuperMarioWorldRecomp
cp "/path/to/Super Mario World (USA).sfc" smw.sfc
bash macos/build-on-mac.sh
```

The script:

1. installs the missing Homebrew packages;
2. on the first run, generates `src/gen/` from `smw.sfc` (`tools/regen.sh --stock`);
3. applies `macos/patches/snesrecomp-macos-data-dir.patch` to the engine
   submodule (idempotent);
4. builds with CMake/AppleClang and bundles `release-macos/SuperMarioWorld.app`
   (SDL3, FreeType/HarfBuzz and other non-system dylibs are copied into
   `Contents/Frameworks`, then the bundle is ad-hoc signed);
5. logs everything to `macos/build.log`.

For a `.dmg` or other options, call `tools/build-macos.sh` directly
(`--config debug`, `--arch universal`, `--regen`, `--no-dmg`).

Launching the app opens the usual launcher; pick the ROM there, or drop a
`.sfc`/`.smc` next to the `.app` beforehand.

## Where things live

| What | Location |
|------|----------|
| Program, launcher assets, built-in mod catalog | inside `SuperMarioWorld.app` (read-only) |
| `config.ini`, `keybinds.ini`, `rom.cfg`, `saves/`, `logs/`, `mods/` | `~/Library/Application Support/SuperMarioWorld` |

The bundle's launcher script exports `SNESRECOMP_DATA_DIR`; on macOS the engine
(`host_paths.c`, `keybinds.c`, via the patch above) anchors all user state there,
so nothing is ever written inside the signed bundle (which may also be
read-only under App Translocation). Assets are exposed to that folder through a
symlink and the release-owned mod catalog is refreshed on each launch, as the
Linux AppImage does.

## Changes in this port

- `tools/build-macos.sh`: rewritten bundling (assets in `Resources`, mod catalog,
  Application Support state, app icon generated from the box art, version from
  `CMakeLists.txt`, dylib bundling only when needed, engine patch, optional
  Falcon helper when Pillow + PyInstaller are available).
- `macos/build-on-mac.sh`: one-shot setup + build with logging.
- `macos/patches/snesrecomp-macos-data-dir.patch`: `SNESRECOMP_DATA_DIR`
  support on macOS in the engine (previously Android-only).
- `src/main.c`, `src/opengl.c`: **display-rate-aware vsync** (all platforms).
  The game paces itself to 60 Hz with a frame timer; also waiting on vsync on a
  non-60 Hz monitor made the two clocks fight. On a 100 Hz display macOS's
  Metal presenter blocked ~20 ms per frame (50 fps: slow motion and audio
  crackle). Vsync is now enabled only while the window's display runs at
  ~60 Hz, re-checked every ~0.5 s so moving the window between displays follows.
- `src/mods/falcon/smw_falcon_presentation_runtime.c`: locate the bundled cache
  helper with `_NSGetExecutablePath` (no `/proc` on macOS).
- `tools/regen.sh`: portable in-place `sed` (BSD sed).

Measured on the M5 after the fix: uncapped throughput ≈100 fps (limited by the
100 Hz panel's presentation, not the CPU; guest frame ≈0.7 ms), audio-paced
60.0 fps with zero underflows.

## Diagnostics

Marker files next to the `.app` (no change to the signed bundle needed):

- `diag.flag` — console output to `run.log`, frames slower than 20 ms to `stall.csv`.
- `bench.flag` — one launch runs a headless throughput and an audio-paced
  benchmark into `bench.log`, then exits (the flag is consumed).

## Known limitations

- Ad-hoc signed, not notarized: on another Mac, right-click → Open the first
  time, or `xattr -dr com.apple.quarantine SuperMarioWorld.app`.
- The Captain Falcon mod needs its PyInstaller helper; it is bundled only when
  the build Python has Pillow and PyInstaller
  (`pip install -r tools/owner_ssb64/requirements-build.txt`).
- Legacy IPS co-op target (`SMW_BUILD_COOP`) not tested on macOS.
- On a 100 Hz panel, 60 fps cannot divide the refresh evenly, so scrolling can
  show slight judder; speed and audio are correct.
