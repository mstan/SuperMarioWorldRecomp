# Falcon owner-cache runtime contract

The Captain Falcon package requires a committed external ROM resource for the canonical Super Smash Bros. US v1.0 image (normalized SHA-1 `e2929e10fccc0aa84e5776227e798abc07cedabf`, 16 MiB). The mod runtime exposes the path only after final Play-time verification; pending launcher choices are never visible to the game plugin.

The game does not generate proprietary cache data in-process. It accepts an immutable `falcon-final-r1-<sha1>-<manifest-prefix>` directory outside the installation/source tree and pins `falcon_runtime.bin` to its approved SHA-256 before parsing it. Missing, malformed, or unapproved caches fail closed: stock Mario OBJ rendering remains present and no owner bytes are drawn.

Normal activation obtains the verified resource from the mod runtime provider,
including catalogs under `mods/preloaded`. Enable Captain Falcon in Mods, select
the owner ROM, then press Play. Releases include `smw-falcon-cache[.exe]` beside
the game executable (inside the AppImage on Linux); no Python installation or
environment variables are needed. First use builds the cache beneath
`%LOCALAPPDATA%/SuperMarioWorldRecomp/smash64` on Windows, or
`${XDG_CACHE_HOME:-$HOME/.cache}/SuperMarioWorldRecomp/smash64` on Linux.
Only an absolute XDG path is accepted. Subsequent runs verify and reuse it.

Development overrides remain available through
`SNESRECOMP_FALCON_CACHE_HELPER=<absolute helper executable>` and
`SNESRECOMP_FALCON_CACHE_ROOT=<absolute external cache root>`.
The helper is invoked exactly as:

```text
<helper> --rom <committed-owner-rom> --cache-root <root> --result-file <root>/.smw-falcon-cache-result-<pid>.txt
```

It must implement the strict `tools/owner_ssb64/build_final_cache.py` recipe,
atomically write only the final cache basename to the result file, and exit zero
only after complete cache verification. The game uses `CreateProcessW` with CRT
argument escaping on Windows and `fork`/`execv` on Linux, never a shell. Spaces
and shell metacharacters stay literal path arguments. The game rejects relative
paths, separators in the returned basename, non-content-addressed names, and a
runtime-blob hash mismatch. A verified cache may instead be supplied directly
through `SNESRECOMP_FALCON_CACHE=<absolute final-cache directory>`.

Successful activation enables the generated gameplay bodies containing Falcon's
hooks. Failure displays an error and keeps Mario active. Loading a save preserves
the current mod selection: native saves acquire Falcon when enabled, and Falcon
saves cannot activate him when the mod is disabled.

During ordinary foreign-owned level control, PPU OBJ slots 64–75 (`$0300` to `$032f`, written by SMW `PlayerGFXRt`) are captured and removed before PPU composition; no other OBJ slots are affected. Falcon presentation is the current sole owner of the PPU overlay-capture policy, so it clears and re-publishes that policy once per frame; a future overlay user must compose this policy rather than independently calling `PpuClearOverlayCaptures`. The approved mesh is composited into the PPU-owned frame at Mario's screen-foot anchor before widescreen/display presentation, so display and TCP screenshots agree. Native death state `$09` keeps the same narrow suppression active and replaces Mario with the latched Smash `DamageFall` pose, rotating 18 degrees per frame while following the mature NES port's accelerating downward presentation. It hides after leaving the viewport and resets only on genuine ordinary Falcon control. Pipes, goals, inactive presentation, and failed cache validation keep native Mario visible.

The camera transform exactly matches the mature NES port: authored owner-model
coordinates use `screen_x = x*cos(yaw) - z*sin(yaw)` and
`depth = x*sin(yaw) + z*cos(yaw)` at an 88-degree yaw. The source `+LR`
convention is mirrored afterward. This ordering is required for unmistakable
left/right side profiles; applying the inverse yaw makes Falcon appear to face
into or away from the screen.

The final cache's 11 PCM WAV files are SHA-256-pinned from the approved manifest and activated transactionally from that exact cache's `audio/` directory. An unreadable, malformed, or tampered cue rolls back all cue registration and disables Falcon audio without weakening or disabling the already-verified visual presentation. Cues are stopped/unregistered on Falcon reset, reactivation, and state reset; live host audio is deliberately not savestate data.

For a coordinated TCP run, set `SNESRECOMP_FALCON_PRESENTATION_TRACE` to an
external writable text file. It records only gate transitions (`approved runtime
cache loaded`, `active`, handoff reasons, OBJ suppression, and mesh compositor
state), never an owner-ROM or cache path.
