# Release procedure

Per-version release notes live on the GitHub release itself (the old
v0.3.0 notes that used to fill this file are at
https://github.com/mstan/SuperMarioWorldRecomp/releases/tag/v0.3.0).
This file is the canonical *how to cut a release*.

## Asset convention: two zips, only zips

Every release ships exactly two assets:

| asset | gen state | config |
|---|---|---|
| `SuperMarioWorldRecomp-windows-x64.zip` | pristine (zero injected overrides) | `Widescreen = 0` |
| `SuperMarioWorldRecomp-widescreen-windows-x64.zip` | `apply_overrides.py` patched (WS-FLAG / WS-DESPAWN / WS-SPAWN) | `Widescreen = 1` |

Never publish a bare `smw.exe` — it is broken without `SDL2.dll` and
redundant next to the zip.

The widescreen machinery is runtime-gated and default-off, but the
standard zip is built from gen that never contained it at all: the
release script restores pristine gen (`apply_overrides.py --restore`,
asserts zero `/*WS-*/` markers), builds, stages, then re-applies the
overrides (asserts the expected injection count, currently 58),
rebuilds, and stages the widescreen zip.

## Steps

1. Make sure the tree is the release commit: game repo `main`, and the
   `snesrecomp/` junction checked out at the commit named in
   `snesrecomp.pin`. Note `src/gen/` is untracked — it must be the
   current regen output for the pinned recompiler (if in doubt, regen).
2. Build both assets:

   ```powershell
   powershell -File tools\make_release.ps1          # both zips
   powershell -File tools\make_release.ps1 -Variant widescreen   # just one
   ```

   Zips land in `release\` (gitignored). The script forces the
   `Widescreen` value into each zip's `config.ini` and writes a
   variant-specific `README.txt`; everything else (`SDL2.dll`,
   `keybinds.ini`) is staged from `build\bin-x64-Release\`.
3. Smoke-test both zips from a scratch directory (extract, run, reach a
   level). The standard zip must look byte-authentic; the widescreen
   zip must fill 16:9 in-level with the split HUD.
4. Write the release notes (what changed, what's verified, caveats) and
   publish — only after the user has signed off on the zips:

   ```powershell
   gh release create vX.Y.Z `
       release\SuperMarioWorldRecomp-windows-x64.zip `
       release\SuperMarioWorldRecomp-widescreen-windows-x64.zip `
       --title "vX.Y.Z — <headline>" --notes-file <notes.md>
   ```

## Install (for the notes' boilerplate)

1. Extract the zip.
2. Run `smw.exe`; first launch prompts for a legally-obtained Super
   Mario World (USA) ROM and caches the path in `rom.cfg`.
3. Saves land in `saves/smw.srm`; controller mapping in
   `keybinds.ini`; options (including `Widescreen` / `WidescreenHud`)
   in `config.ini` next to the exe.
