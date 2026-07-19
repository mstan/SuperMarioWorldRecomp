# Audio Transition Recovery Design

**Goal:** Eliminate transition-triggered loss of SPC and MSU-1 audio while preserving hardware-correct CPU-to-APU port visibility.

## Scope

- Reproduce the reported dropout with both native SPC and MSU-1 music.
- Fix only the shared APU transport behavior proven to discard or delay a transition command.
- Include independently reproduced audio or gameplay blockers encountered on this path; do not perform unrelated cleanup.

## Evidence

The installed `v0.9.5` release uses `snesrecomp` revision `2ae1488` from 2026-06-13. That revision predates runner commit `814dfb8` (2026-06-17), whose documented failure is missing SFX/music at level transitions when a CPU port value is replaced before the SPC polls it. The current SMW `main` submodule revision contains that commit but later changed APU port visibility to immediate. The newer behavior must therefore be measured, not assumed correct.

## Design

### Reproduction and classification

Build a debug-capable SMW executable and use the existing audio trace rings plus `tools/sfx_probe.py` around a deterministic level transition. Run once with SPC music and once with a matching MSU-1 pack. Classify each nonzero port command as:

- `LOST`: replaced before the SPC observes it; this is the transport failure to fix.
- `SEEN-NOKON`: observed by the SPC but not accepted by the sound engine; investigate the engine path instead.
- `SEEN`: observed and keyed on; the transport is not the cause.

The target outcome is zero `LOST` transition commands at normal speed and turbo.

### Shared fix boundary

The correction belongs in `snesrecomp/runner/src/snes/apu.c` and its queue state, not in SMW-generated code, `common_rtl.c`, or the SDL callback. A write remains immediately visible when the port has no queued or unobserved command. A later distinct value waits for the existing `APU_PORT_MIN_DWELL`; an SPC read clears the gate. This preserves CPU↔SPC upload handshakes because their CPU writes follow SPC observations.

The SMW repository consumes the corrected runner through its `snesrecomp` submodule. After the runner fix is validated, update the submodule pointer in SMW. No compatibility flag, game-specific workaround, or default environment override will remain.

### Regression coverage

`tests/runtime_dispatch/apu_port_transition_test.c` queues a fade, a music command, and the NMI clear at one produced-sample point. It advances the queue at each dwell boundary, records the SPC observation, and asserts every value is visible in order. The test models both a callback collision and turbo-compressed producer timing.

## Verification

1. Run the new focused runner test; it fails before the queue’s observation APIs exist.
2. Apply the shared APU fix and rerun the test until it passes.
3. Run the runner framework suite; v2 checkouts skip the obsolete v1-only `sync_funcs_h` contract instead of importing a namespace package as its missing backend.
4. Build the current Linux target; it must link with MSU-1 launcher support.
5. Start the executable and confirm SDL opens the audio device.
6. End-to-end TCP audio-trace capture remains required before claiming a manual SPC/MSU-1 transition test; this harness could not reach the process-local listener.

## Non-goals

- Rewriting the SPC emulator, DSP, MSU decoder, or SDL mixer.
- Changing sound design, music selection, or PCM packs.
- Fixing unrelated issues without a reproduction.
