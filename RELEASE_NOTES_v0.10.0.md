## What's new

This release moves Super Mario World onto the current shared engine and
launcher, and brings its release packaging up to the same standard as the
Mega Man X builds — a Proton-safe Windows zip and a proper Linux AppImage.

### Packaging

- Fixed the Windows release archive so nested files use portable ZIP
  paths. Linux and Steam Deck extractors now rebuild the bundled
  `assets/` hierarchy correctly, so a Windows build running through
  **Proton** finds the launcher's fonts and images instead of coming up
  blank. Previously 11 of 18 archive entries carried literal Windows
  backslashes.
- Added release-packaging validation that rejects Windows-only ZIP entry
  names, and that refuses to package an executable which is not stamped
  with the version being released.
- The **Linux AppImage** is now built and packaged the same way as Mega
  Man X's:
  - `config.ini`, `keybinds.ini`, `rom.cfg`, `saves/` and `mods/` live
    **next to the `.AppImage`**, never inside the read-only mount, so
    settings and saves survive an AppImage update and re-anchor if you
    move the file;
  - the recomp-ui launcher's `assets/` are staged into the image, so the
    pre-boot launcher actually renders (previously missing);
  - drop your ROM beside the `.AppImage` and it is picked up
    automatically;
  - Steam Deck controller hints are exported so the built-in pad reads as
    a real gamepad rather than Steam's keyboard remap;
  - `linuxdeploy`/`appimagetool` are fetched and SHA-256 verified into the
    build tree, making packaging reproducible;
  - packaging **fails the build** unless a layout test proves state never
    leaks into the payload, a user `config.ini` edit survives a relaunch,
    and a moved `.AppImage` re-anchors its state.

### Fixes

- Fixed a Linux link failure: the MSU-1 environment export used the
  Windows-only `_putenv` on one code path. All environment exports now
  route through a single portable helper.
- Crash diagnostics from CMake-built releases are now stamped with the
  real release version instead of `dev`.

### Under the hood

- Engine (`snesrecomp`) advanced 65 commits; launcher (`recomp-ui`)
  advanced 62 commits. Notable inherited work: SA-1 and DSP-1 support,
  hardened coverage-guided AOT promotion, Mode 7 widescreen margins, HUD
  anchor bands, production builds that omit developer observability rings,
  and a large launcher pass (setup wizard, video filters, controller gyro,
  netplay lobby work).
- The launcher now stages only SNES controller art rather than every
  console's, and the widescreen override hooks are injected by CMake on
  both platforms, so the Windows and Linux builds are generated from an
  identical tree by construction.

## Notes

- A verified Super Mario World (USA) ROM is required and is **not**
  included.
- Windows: extract the **full** archive, then run
  `SuperMarioWorldSNESRecomp.exe`. The bare `.exe` will not run without
  the bundled `SDL3.dll`, MinGW runtime DLLs and `assets/`.
- Linux: `chmod +x` the `.AppImage` and run it. Keep it in a writable
  folder — that folder is where your config and saves live.
- Widescreen (16:9) is a runtime toggle in the launcher, default off.
