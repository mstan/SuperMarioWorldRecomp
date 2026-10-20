# Native co-op October integration

Tracking: `beads-8wg.3.26` (feature), `beads-8wg.3.31` (integration),
`beads-8wg.3.29` (audio).

This is an integration checkpoint, not full two-player acceptance.
The approved behavior and outstanding feature work remain in
[NATIVE_COOP.md](../NATIVE_COOP.md).

## Preserved checkpoint and upstream

- Original tested game commit: `27677aa0227c07f13a3a1ad6b6e2a0ac3a7a0ddc`,
  retained locally as `checkpoint/native-coop-20261019`.
- Original framework commit: `84177dff249eea5b8331f2e2354d78b6d6050f50`,
  retained as `checkpoint/native-coop-runtime-20261019`.
- The tested executable and owner saves/settings were copied into
  `build-adaptive/playtest/goal-20261019/preserved`. All 28 original and copied
  files matched the SHA256 manifest before building. Private data stays local.
- Rebased all 19 game commits onto game main `8dedb28`. The rebased feature
  tip before this integration fix was `4d0517b`. Conflict resolution retained
  main's Falcon activation/cache builder, shared generation-directory option,
  presented-frame captures, and UI pin. Co-op simulation, presentation and
  renderer code were unchanged by conflict resolution.
- Framework: `7a67b85724a8b3be5b592c5a7578e35f739c81fb`.
  It includes merged [framework PR #95](https://github.com/RetroPortingToolKit/snesrecomp/pull/95)
  and all changes in game main's earlier `8867499` pin. Its
  [native-analyzer](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39363621797),
  [CLI](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39363621857)
  and [Lua](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39363621822)
  workflows passed. That evidence does not qualify this game's full platform matrix.
- UI: `01bff9477e6847b2281260f0a9526a85e45b8ba2`, matching game main.

## Owner snapshot compatibility

The framework's `daa3310` changes RTLS version 9 to 10, appending a one-byte
`rdnmiPending` latch after the joypad records in `snes_saveload`. The original
co-op preflight required the saved guest version to equal the writer's current
version. Consequently the first live integration replay rejected the copied
version-9 state, never entered gameplay, and produced no contact trace.

The game now recognizes precisely this audited v9/v10 prefix-length difference.
The framework's existing v9 reader supplies the absent latch as false. Original
files are not rewritten. Other guest-version mismatches remain rejected; the
envelope's mode/ROM identity and CRC, renderer schema, roster CRC/layout/count,
total lengths and transactional staging checks still apply.

`tools/coop_fixture.py` also recognizes both guest versions, accounting for the
appended latch when locating WRAM. It continues to work only on copied data.

The real preflight harness tests both versions, every truncated prefix,
malformed magic/length/schema/roster data, unsupported versions, and removal of
previously staged data following a rejected load. It asserts that preflight
leaves the active roster byte-for-byte unchanged and that accepted loads commit.
The later live replay loads the original v9 fixture and validates a native stomp.

## Reproduction and evidence

Local evidence root: `build-adaptive/playtest/goal-20261019`.
Generated code, ROMs, snapshots, captures and executables are not committed.

Use the native Python interpreter and MinGW toolchain on Windows. The full
stock regeneration used the original ROM, current native analyzer, CFG roots,
`recomp/renderer_aot_roots.c`, and handwritten C source roots, following
`tools/regen.sh --stock`. It emitted 3,432 AOT-eligible variants and 4,844
interpreter-only variants with the approved disassembly authority.
Generated C was not edited by hand.

```text
cmake -S . -B build-adaptive -DPython3_EXECUTABLE=<native-python>
cmake --build build-adaptive -j 8
python tools/test_native_coop.py
python tools/test_coop_input.py
python tools/test_coop_hooks.py
python -m pytest -q snesrecomp/tests/snapshot_guard snesrecomp/tests/ppu/test_extra_objects.py snesrecomp/tests/v2/test_interp_bridge_contract.py
powershell -File snesrecomp/tests/audio_delivery/run.ps1
python tools/test_coop_preflight.py build-adaptive/playtest/mount-feedback/entrance-v5.sav
python tools/test_coop_contacts_live.py --exe build-adaptive/SuperMarioWorldSNESRecomp.exe --rom smw.sfc --source build-adaptive/playtest/mount-feedback/entrance-v5.sav --out <new-output-directory>
```

The live contact runner creates a new directory per case with copied input,
isolated save namespace/settings/mod selection, dummy video/audio, commands,
hashes, logs, traces, and rendered captures. It validates native stomp bounce,
shell kick, pickup and side damage for each actor at center/right/left camera
positions, in interpreted and compiled-bounce execution. It checks dispatch
logs after every run. These position/camera fixtures do not qualify streamed
terrain: use unmodified loads and real camera travel for that.

`tools/run_coop_probe.py` provides the same isolation for arbitrary finite
recorded-input probes, including native width, solo/disabled mode, old-state
replay, and audio-paced runs. `--audio-paced` uses a dummy audio device while
retaining audio emulation and the strict audible-sample-loss gate.
`--turbo-burst start,count` is process-local; it never sends keys to another
window. Throughput runs do not qualify audio.

## Validation status

Passed:

- Full Windows SDL3/MinGW build, including the newer Falcon cache helper.
- Dynamic policy/serialization at rosters 2, 3, 4 and 17.
- Input assignment persistence and generated/interpreted hook checks.
- Framework snapshot guard, extra OBJ and bridge contract harnesses.
- Framework audio-delivery harness, including turbo recovery.
- v9/v10 co-op preflight and a real v9 load/contact replay.
- All 48 live contact cases: two actors, four encounter types, three camera
  positions, and both execution paths. All 96 paired contact/gameplay CSVs
  and rendered images are byte-identical between those paths. No dispatch
  misses were reported. Results and comparisons are in
  `contacts-matrix/results.json` and `contacts-matrix/parity.json`.

- The copied owner F1 v9 state loads, writes a real v10 snapshot, and reloads
  that snapshot in both native and wide views. Both actor records and room
  `$106` survive. All 27 owner save/settings files still match their preserved
  hashes. This is persistence evidence, not complete camera acceptance.
- Fresh solo and disabled-mod scripted boots pass the original menus/intro
  and reach overworld mode `$0E`, with the stock two-player flag clear and
  campaign data written only to normal SRAM. Frame 2380 was inspected in
  both runs. This does not establish full solo/alternating-play parity.

Failed/open: the first instrumented 800-frame individual-death audio run
reported 6,553 dropped audible samples and five output underflows at 57.1 FPS.
The no-death control had zero audible drops but six underflows at 49.8 FPS.
The turbo probe also reported audible drops; its intentional accelerated
interval makes the aggregate minimum-consumed-sample threshold unsuitable
for qualifying post-turbo recovery by itself. These findings do not demonstrate
healthy audio. Removing per-frame CSV/capture overhead restored approximately
60.05 FPS with no output underflows, but audible loss remained: 1,203 samples
in the death case and 3,765 in its control. The recorded loss is concentrated
near state loading. The current working hypothesis is a restored full host
audio queue; source inspection and a focused reproduction must establish that
before changing the framework. These results are in `audio-*-minimal`.

The native-width owner-state screenshot exposed an acceptance failure, now fixed:
the saved wide camera remains at X=98 while the sole surviving Luigi is at
center X=24. His view exclusion persists while he is stationary, after Mario
enters the death bubble. Loading at another aspect must refit that active
survivor without reintroducing death-driven camera jumps. Reproduction:
`owner-native-render`, screenshot/WRAM frame 220 and `coop.csv`. The fix releases a held death-camera offset only when a changed viewport excludes
an active actor. It does not change the guest scroll directly or override
autoscroll. The existing four-pixel focus step and native terrain streaming
bring the survivor back onscreen. `camera-refit-native` frame 220 was inspected:
Luigi is visible at X=24 with camera X=0. The saved v10 state reloads with that
framing. `camera-refit-wide` retains camera X=98 and both frame-80/220 captures
are byte-identical to the earlier wide replay. Roster tests at 2/3/4/17 cover
viewport refit, continued death-camera holds when the survivor still fits, and
autoscroll exclusion. Live window resizing and the complete camera matrix
remain acceptance work. A separate bounce replay timed out in host startup
before gameplay; it is retained as unverified, not counted as a pass.

Still unverified: full campaign and all routes/exits; remaining approved
gameplay implementation; expanded solo/disabled/save/input acceptance;
game-specific Linux/macOS/package checks; sustained performance; current
live audio health. The passing framework audio harness alone does not resolve
the previously recorded 574/1,307 dropped audible samples in live SMW runs.

### Audio investigation after the integration milestone

Compiled structure offsets from the actual framework headers locate the saved
DSP output cursors at guarded-state offsets 99308/99312 on this Windows build.
Both the entrance and individual-death fixtures contain write=8192/read=0:
a full host output queue. The diagnostic source/layout and decoded observations
are retained as `audio-layout.c`, `audio-layout.json`, and
`audio-snapshot-queue.json` in the evidence directory.

An isolated copy changed only the read cursor to match write and repaired the
outer CRC. Its replay still reported 3,793 audible drops. Therefore the full
restored queue alone does not explain the audio failure. That run also timed
out during shutdown after printing its failing benchmark; the output remains
failed. Next observation separates counters before input/state load and after
guest execution. No audio fix or relaxed acceptance threshold has been applied.
Framework investigation is tracked in `beads-8wg.2.139`.

### Audio producer pacing and current acceptance

The passive before-input/before-guest/after-guest counters locate audible loss
before any snapshot load: boot first loses audible samples on frame 2, and
frame 95's title loader adds a large burst. In the delayed-load control, all
19,706 audible drops precede the frame-180 state load; that load adds no further
audible drops through frame 799. This supersedes the initial queue hypothesis.

The selected framework's `runner/src/desktop/host_main.c` already handles long
native loaders with producer backpressure at the outermost APU unlock. The
separate SMW host lacked that integration. It now uses the same 4,096-sample
high-water behavior: release the recursive mutex so the device can consume,
and stop waiting after 250 ms without a healthy consumer. A stalled device is
not waited on repeatedly until callbacks resume. This applies to ordinary
local audio playback; turbo, throughput/no-device tests and the existing netplay
path retain their previous scheduling. Guest SPC/DSP generation is unchanged.
No framework source, ROM, generated C, or audio acceptance threshold was changed.

The diagnostic CSV and raw device capture are optional and buffered. An early
version synchronously flushed every frame and produced misleading extra
underflows; those failed/partial runs remain retained. `run_coop_probe.py` now
retains a result even after a timeout and can write muted stereo WAV captures.

Current evidence:

| Probe | Result |
|---|---|
| `audio-backpressure-control`, 800 frames | Strict gate passes; zero dropped samples; 3 instrumented underflows retained. |
| `audio-backpressure-death`, 800 frames | Strict gate passes; zero dropped samples and underflows. |
| `audio-fresh-entry-buffered`, 3,600 frames | Original menus, intro, overworld and YI2 entry; strict gate passes, zero dropped samples. One underflow at frame 424 in file-menu mode `$08`; resolved by the later namespace-write correction below. |
| Same fresh entry, frames 3200–3599 | Zero drops/underflows; 213,600 samples produced, 213,595 consumed; captured stage output peak 4,104, RMS 681.5. |
| `audio-turbo-buffered`, 120-frame turbo interval | Whole-run strict result remains failed because accelerated frames deliberately overflow the device-rate queue. It is not relabeled as passing. |
| Same turbo run, frames 240–1199 (after release) | Separate recovery acceptance passes: zero drops/underflows, 512,640 produced, 501,529 consumed; output peak 4,418, RMS 677.2. No persistent silence. |

All listed completed runs have no dispatch misses. Saved fresh-entry state
`savequick.sav` contains two actors in level `$106`; frame 3500 of the earlier
captured fresh-entry run was inspected. Capture uses a dummy output device so
no sound reaches the owner's speakers. WAV data is retained for independent
inspection; peak/RMS establish sustained non-silence, not musical fidelity.

Reproduce interval acceptance without rerunning or altering any game state:

```text
python tools/check_coop_audio_probe.py <audio-fresh-entry-buffered> --start 3200
python tools/check_coop_audio_probe.py <audio-turbo-buffered> --start 240
```

The native-camera bounce rerun also completed: both complete gameplay/contact
CSVs and the inspected frame-220 image match the interpreter replay byte for
byte. The earlier startup timeout remains in the evidence directory. Contact
fixtures now explicitly store their intended 448-pixel viewport, so their
synthetic fixed-camera offsets do not accidentally exercise viewport refitting.

The later persistence section resolves the reproducible file-menu underflow.
Remaining audio qualification: exercise stage-entry/recovery across the campaign and supported devices/platforms,
and retain the distinction between routine normal-speed playback and intentional
turbo sample disposal. These bounded checks do not close the full audio issue.

## Default contacts, ordinary items and transformation ownership

All artifacts in this section are under
`build-adaptive/playtest/goal-20261019/contact-arbitration/`.

### Negative controls and root causes

`before-stomp` uses two identically positioned falling actors against one
stock Koopa. At policy frame 756, only Mario gets the native bounce. In the
following frames Luigi continues down while Mario rises. The initial sprite
scheduler bound only the nearest actor to both AI and contact processing.

The new adapter separates contact queries from the once-per-world update.
At the same frame, both actors now receive velocity -48 and their individual
stomp-chain increment. The Koopa changes status 8 to 9 once; the additional
stomper does not create another native score sprite. Actual before/after
captures at frames 70/80 were inspected.

The first broad contact run, `full-matrix`, passed 132 contact/winner checks
and 330 cross-execution trace/image comparisons. **That was insufficient
acceptance:** the stronger saved-state check in `strengthened-recheck.json`
fails eight cases because Mario's flower/cape animation remains active.
`native-matrix` confirms four corresponding failures among its 44 cases.
These earlier runs remain retained and are not counted as final passes.

`flower-freeze-writer` uses the existing passive WRAM write logger, watching
`$009D` without pausing or halting. It records the flower routine setting
`SpriteLock=$20` on host frame 60, then `PlayerState00_HandleLRScrolling`
writing zero on frame 61 and subsequent frames. The ROM's `$00:CDDD` routine
always copies `CameraIsScrolling` to that lock. Merely masking L/R input did
not disable this singleton side effect for the idle partner.

The correction disables that manual-look routine in active co-op and retains
the freeze until all roster transformations finish. Unequal simultaneous
transformations exercise both orderings: Mario finishes first, and Luigi
finishes first. Both keep the world frozen for the original mushroom's 48
frames, then accept actual rightward movement. `transform-pilot` passes all
eight cases across both execution paths and all 20 compared files.

The subsequent `final-wide` run passed 136 of 144 cases and all 360 execution
comparisons, exposing eight cape failures confined to the expanded margins.
`final-native` passed 48 cases and 96 comparisons. In the failed margin saves,
the cape was awarded, but `$71=$00`, `$1496=$00`, and `$9D=$01`: there was no
animation to release the lock. The original `$01:C5AE` viewport guard bypasses
animation setup, while its caller increments `SpriteLock` unconditionally.
The co-op adapter now starts the original 24-frame cape animation for these
collectors too. It retains the original guard for the separate smoke effect,
whose coordinates have no high bytes. Widescreen smoke presentation remains
unverified and requires a separate presentation adapter; it is not claimed
fixed by this gameplay correction.

`cape-fixed` reruns both collectors at center/left/right in both execution
paths: **12/12 pass**, with **30/30** trace/capture comparisons identical.
The saved-state and trace recheck additionally requires exactly 24 frozen
frames in every case. Captures at frames 70 and 80 were inspected at all three
camera positions: the center retains the original puff; margins do not display
a wrapped puff at an unrelated location. This is an affected-case rerun on
`build-9`, not a claim that the entire matrix ran on that binary.

The preceding combined `build-8` also passes `final-single` (48/48 individual
stomp/kick/pickup/damage cases, 96/96 comparisons) and `final-native` (48/48,
96/96 comparisons). `final-wide` supplies the 136 passing non-failing cases;
its eight failed cape cases are superseded by `cape-fixed`. The rejected
results and binary/input hashes remain available.

The build verifies 49 compiled and four interpreter-only hook sites. Core
roster/state tests at 2/3/4/17, hook installation checks, v9/v10 preflight
rejection/atomicity tests, and input configuration tests pass. Accepted live
cases have no dispatch misses. Owner saves/settings still match all 27
preserved hashes.

### Scope and acceptance

The shared default-contact prefix uses original ROM clipping and eligibility,
then records candidates before responses. Power-ups use the separate original
`$01:C4AC` qualification through `$01:C4C6`. The observer's phase 2 records
candidate qualification; phases 0/1 record actual response entry/exit.
Each accepted test requires an executed full-WRAM query audit, balanced guest
stack, completed process, no dispatch misses, and retained capture.

Checks cover:

- Two stomps or spin jumps: both native bounces/chains, one enemy outcome and
  one score sprite.
- Spiny spin bounces, one shared star defeat, damage to both players, and
  independent protection.
- Contested shells: primary tie, nearer Luigi, Luigi as the only grab-eligible
  actor, and a nearer Mario on the wrong side of actual Map16 net contacts.
- Mushroom, flower, feather, star, moving coin and 1-up: both tie and nearer
  Luigi cases; one item consumed, one reward, only the collector receives
  equipment/star benefits. Saved states prove shared coin/life increments
  and completed transformations with no lingering freeze.
- Unequal simultaneous transformations: exact native freeze duration, both
  ownership orderings, and movement after completion.

The fixtures do not qualify terrain streaming or campaign routes. Their
coordinates and, in the fence case, collision tiles are staged deliberately;
VRAM is not rebuilt. Remaining custom sprite contacts, combined solid-object
responses, cross-ability same-frame damage, projectiles, committed targets,
mount interactions and campaign acceptance remain open.

## File-menu persistence and audio

`audio-storage-before` adds a buffered phase timer around namespace selection.
At host frame 424, choosing the already loaded two-player namespace spent
149.7626 ms in `RtlWriteSram`. The queue had 3,709 samples before that guest
frame and underflowed during it. The stricter captured interval check correctly
fails, even though the broader benchmark's audible-drop gate passes.

Retaining an unchanged namespace removes the redundant write/read:
`audio-storage-after` completes 3,600 native-width frames through the original
menus, intro, overworld and Yoshi's Island 2 with zero dropped samples and zero
underflows. Whole-run capture passes; stage frames 3200–3599 pass independently
with 213,600 samples produced, 213,594 consumed, peak 4106 and RMS 681.49.
Frame 3300 was inspected. Selection itself took about 0.229 ms.

That correction alone was insufficient for actual namespace changes.
`audio-storage-two-to-one` spent 863.4382 ms flushing the departing namespace
and underflowed at frame 423. `audio-storage-one-to-two` spent 82.3401 ms and
did not underflow. Both retained runs completed without dispatch misses.
The asynchronous copied-image writer addresses these required writes rather
than omitting durability or allowing one namespace's bytes to reach the other.

The real SDL worker harness passes gated I/O, immutable path/data, FIFO,
latest-image readback, superseded-failure, retained-failure and retry checks.
Production menu routing passes namespace sentinels, same-mode retention,
enqueue-failure/retry, invalid-count and disabled-mod checks. The real framework
writer passes file/backup/failed-publication and explicit-image isolation checks.
The worker harness also passes against SDL2 and SDL3 on Windows.

The combined asynchronous build passes:

| Evidence | Result |
|---|---|
| `audio-storage-async-two-to-one`, 650 frames | Zero drops/underflows; captured interval passes. |
| `audio-storage-async-one-to-two`, 650 frames | Zero drops/underflows; captured interval passes. |
| `audio-storage-async-entry`, 3,600 frames | Whole-run and YI2 interval pass with zero drops/underflows; stage peak 4106, RMS 681.49. Frame 3300 inspected. |
| `audio-storage-sram-comparison.json` | All four resulting normal/co-op SRAM files match their synchronous baseline bytes. |
| `storage-menu-exit`, 400 frames | Exit while the original count menu is open preserves both 2,048-byte namespace sentinels, creates only the selected co-op backup, and leaves no temporary file. Frame 390 confirms the count menu. |

All runs complete with no dispatch misses. The old failed results remain
retained. Supported-platform, full campaign and device audio qualification
remain open; these checks resolve the observed menu-write failures.

CI for framework `1c31f29` passes its [analyzer/runtime/content-pack matrix](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39401397475),
[CLI build](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39401397680)
and [Lua matrix](https://github.com/RetroPortingToolKit/snesrecomp/actions/runs/39401397427).
The game `6457cb6` [Linux co-op core/storage workflow](https://github.com/mstan/SuperMarioWorldRecomp/actions/runs/39401727263)
passes as well. Framework PR #191 is ready for review; game PR #19 remains draft.

## Independent projectile allowance: retained negative control

`projectiles/before-shared-limit` on the combined contact/storage build stages
two fire-powered actors at the stock YI2 entrance and presses both actual fire
buttons twice, with twelve released frames between attempts. The first saved
state contains two type-5 shots, in native slots 8 and 9, and both actors'
shooting timers equal 8. The second contains the same two moving shots;
both shooting timers are zero. Both second attempts were refused by the
original shared slot limit. The frame-80 image was inspected, and the run has
no dispatch misses.

The ROM's `$00:FEA8` `ShootFireball` searches only extended slots 8–9. The
once-per-world loop at `$02:9B0A` updates ten extended slots through `$02:9B16`.
Its twelve sprite tables occupy `$170B–$1782`. Per-player fireball allowance
was still unimplemented at the `e3c678a` checkpoint; two control images or
independent fire-button input do not establish independent ammunition.

The initial control script used save slot 12, which is the Quick slot and writes
`savequick.sav`. The first inspection incorrectly looked for `save12.sav`;
the game log establishes the actual path. A redundant 120-frame rerun is
retained under `before-shared-limit-complete`. The reusable
`make_coop_projectile_fixture.py` uses ordinary slots 11 and 9 instead.

The source audit also identifies a required collision boundary: `ProcessFireball`
can increment Chuck's `$1528` hit count without setting a contact cooldown.
Extending the projectile pool alone would permit duplicate damage in one frame.
Projectile ownership, original motion/drawing, shared damage arbitration and
snapshot support must be implemented and validated together.

## Owned fireball milestone

The [permanent implementation audit](native-coop-fireballs-2026-10-20.md)
documents the owned banks, original-code boundaries, CNR6 records, legacy
compatibility and two Mode 7 defects found during real boss entry.

The combined build passes:

- `projectiles/acceptance-first`: 34/34 base checks. Independent two-shot
  allowance, refused third shots, expiry/reuse, exact save/load replay,
  both widescreen margins, one Chuck hit, all five direct boss fireball
  handlers, and conservative legacy-shot migration. All native/bounce
  comparisons match complete WRAM and co-op payloads.
- `projectiles/freeze-acceptance`: 8/8 checks, including four repeated lifecycle
  controls. An actual feather pickup freezes all existing fireballs; they
  resume after the native transformation in standard/wide and native/bounce.
- `projectiles/contact-regression`: 10/10 common stomp, spin, star, exclusive
  pickup and dual-transformation cases; 20/20 trace/capture comparisons.
- ROM-free 2/3/4/17-roster snapshot tests, legacy/current preflight and hook
  coverage checks. The build has 58 compiled and four interpreter hook sites.
- `projectiles/owner-data-verification.json`: 27/27 owner saves/settings retain
  their preserved hashes.

The five boss-contact checks use real loader initialization and actual shots.
They bypass stage prerequisites and do not qualify boss defeat or next-stage
transitions. Custom contacts, other abilities, complex camera/recovery,
mount behavior, special scenes and the complete stock-game audit remain open.
