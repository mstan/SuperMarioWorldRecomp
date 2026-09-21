# Release procedure

Per-version release notes live on the GitHub release itself (the old
v0.3.0 notes that used to fill this file are at
https://github.com/mstan/SuperMarioWorldRecomp/releases/tag/v0.3.0).
This file is the canonical *how to cut a release*.

## Asset convention: one archive per platform, only archives

| asset | built by |
|---|---|
| `SuperMarioWorldRecomp-windows-x64-v<Version>.zip` | `tools/make_release.ps1` |
| `SuperMarioWorldRecomp-linux-<Version>-x86_64.AppImage` | `tools/build-linux.sh` |

Never publish a bare `SuperMarioWorldSNESRecomp.exe` — it is broken
without `SDL3.dll` and the recomp-ui `assets/` tree, and redundant next
to the zip.

Widescreen is **no longer a separate zip**. Enable **SMW Adaptive Widescreen**
in the launcher's Mods page. Its defaults are **Fit to screen** and
**Screen-based** enemy spawning, with fixed aspect ratios and Original 4:3
spawning available. CMake's `smw_renderer_hooks` stamp installs the adaptive
renderer and the separate Falcon hooks identically on Windows and Linux.
Mod settings persist in `mods/state.toml`; legacy Widescreen INI fields are inactive.

The co-op executable is additive and opt-in
(`-DSMW_BUILD_COOP=ON` / `--coop`); ship it only when the release notes
call for it.

For releases that include Lua (v0.12.0 onward), configure the stock Windows
build with `-DSNESRECOMP_ENABLE_LUA=ON` and pass `--lua` to `build-linux.sh`.
The listener remains runtime opt-in through `SNESRECOMP_LUA_PORT`. Both
packages must include `lua/100_fireballs.lua`, its README, the client/helpers
and Lua's license. AppImage first launch seeds missing examples beside itself
without overwriting user edits. Co-op currently rejects Lua builds.

## Windows

```powershell
# 1. configure. NOTE the quoting: PowerShell rewrites an unquoted
#    -DSNESRECOMP_BUILD_VERSION=0.13.0 into "0", which would ship crash
#    reports that cannot be tied to this release. make_release.ps1 now
#    refuses to package an exe that is not stamped with -Version, but
#    quote it here and the guard never has to fire.
cmake -S . -B build-recompui -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe `
  -DSNESRECOMP_ENABLE_TRACE=OFF -DSNESRECOMP_SDL_BACKEND=SDL3 `
  -DSNESRECOMP_ENABLE_LUA=ON `
  "-DSNESRECOMP_BUILD_VERSION:STRING=0.13.2"

# 2. build. Keep -j modest: the generated banks are multi-MB TUs at -O3,
#    and an over-subscribed build kills the compiler with NO diagnostic
#    (empty output, exit -1). That is memory pressure, not a source bug.
cmake --build build-recompui --target SuperMarioWorldSNESRecomp -j 4

# 3. package
powershell -File tools\make_release.ps1 -Version 0.13.2 `
  -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin
```

### Runtime DLLs are derived, not listed

Step 1 deliberately does **not** pin `-DSDL3_DIR`. It used to point at a
locally built SDL3 3.4.12; in practice the build resolved MSYS2's SDL3
instead, and nobody noticed, because the two are interchangeable at the
source level. They are not interchangeable at package level: MSYS2's
3.4.14 links `libiconv-2.dll` and 3.4.12 does not.

v0.13.1 shipped 3.4.14 without `libiconv-2.dll`, because
`make_release.ps1` staged a hardcoded list of runtime DLLs. Players saw
`0xC0000135` if they had no `libiconv-2.dll` at all, and `0xC000007B`
("The application was unable to start correctly") if their PATH happened
to carry a **32-bit** one - 32-bit Git for Windows, GTK, PHP and GIMP all
ship one, the loader binds it, and the process dies before `main()`. The
gap is invisible on a dev box because MSYS2 and Git for Windows both put a
64-bit copy on PATH.

`make_release.ps1` no longer enumerates DLLs. It stages the SDL backend
and then calls `Copy-RuntimeDllClosure` / `Assert-RuntimeDllClosure` from
`snesrecomp/tools/release/RuntimeDllClosure.ps1`, which walks the real PE
import graph, pulls in the transitive closure of non-OS dependencies, and
then re-reads the stage to assert that every import resolves inside the
package and every staged binary is x64. An unresolvable dependency or a
wrong-architecture DLL **fails the release** instead of shipping.

So: whichever SDL3 the build resolves, its dependencies ship with it. To
sanity-check a package by hand, extract it and launch the exe with `PATH`
scrubbed to `C:\Windows\system32;C:\Windows` - a package that starts
there will start anywhere.

The zip lands in `release-stage\` (gitignored). `make_release.ps1`
writes **portable (`/`) ZIP entry names** and then re-reads the archive
to reject any Windows-only name. This is what lets Linux/Steam Deck
extractors rebuild the nested `assets/` (and `mods/packages/`)
hierarchy — with `Compress-Archive`'s backslash entry names a Proton
user's extractor produces files literally called
`assets\fonts\LatoLatin-Regular.ttf` and the ImGui launcher finds no
fonts at all.

## Linux

```bash
bash tools/build-linux.sh --version 0.13.0 --lua --jobs 4
```

The AppImage lands in `release-linux/`. The script:

* fetches `linuxdeploy` + `appimagetool` into the build tree and
  verifies them against pinned SHA-256s, so packaging is reproducible
  and does not depend on whatever happens to live in `~/recomp-tools`;
* stages the recomp-ui launcher `assets/` into the AppDir (the ImGui
  pre-boot launcher loads fonts/images from `assets/` next to the exe,
  which resolves to `usr/bin` inside the mount);
* writes an AppRun that keeps `$APPIMAGE` exported so `host_paths.c`
  anchors state **next to the .AppImage**, not inside the read-only
  squashfs — same policy as the Windows zip keeping state next to the
  exe;
* auto-finds a ROM sitting beside the .AppImage and exports the SDL
  hints that make the Steam Deck pad read as a real gamepad;
* runs `tools/test_appimage_layout.sh` and **fails the build** if state
  leaks into the payload, a user `config.ini` edit does not survive a
  relaunch, or a moved .AppImage does not re-anchor its state.

## Steps

1. Make sure the tree is the release commit on `main`, with
   `snesrecomp/` and `recomp-ui/` at their intended pins
   (`git submodule update --init --recursive`). Note `src/gen/` is
   untracked — it must be the current regen output for the pinned
   recompiler (if in doubt, `bash tools/regen.sh --stock`).
2. Build and package both platforms (above).
3. Smoke-test both from a scratch directory: extract/copy, drop a ROM
   beside the exe/AppImage, run, reach a level. Confirm the launcher
   renders (proves `assets/` resolved) and that Fit to screen fills in-level
   with the anchored HUD and Screen-based spawning enabled.
4. Write the release notes (what changed, what's verified, caveats) and
   publish — only after the user has signed off on the artifacts:

   ```powershell
   gh release create vX.Y.Z `
       release-stage\SuperMarioWorldRecomp-windows-x64-vX.Y.Z.zip `
       release-linux\SuperMarioWorldRecomp-linux-X.Y.Z-x86_64.AppImage `
       --title "vX.Y.Z — <headline>" --notes-file <notes.md>
   ```

## Install (for the notes' boilerplate)

1. Extract the zip (Windows) or `chmod +x` the `.AppImage` (Linux).
2. Run `SuperMarioWorldSNESRecomp.exe` / the `.AppImage`; first launch
   prompts for a legally-obtained Super Mario World (USA) ROM and caches
   the path in `rom.cfg`. On Linux you can instead just drop the ROM
   beside the `.AppImage` and it is picked up automatically.
3. Saves land in `saves/`; controller mapping in `keybinds.ini`; options
   in `config.ini` and mod choices in `mods/state.toml` — all next to the exe, or
   next to the `.AppImage` on Linux.
