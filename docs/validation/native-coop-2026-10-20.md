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
