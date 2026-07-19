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

The correction belongs in `snesrecomp/runner/src/common_rtl.c` and its APU queue support, not in SMW-generated code or the SDL callback. The CPU-to-APU write path must keep writes visible at the current emulated APU time while preventing a distinct command from being overwritten before one SPC poll. Any scheduling change must retain the real upload handshake behavior that motivated immediate visibility.

The SMW repository consumes the corrected runner through its `snesrecomp` submodule. After the runner fix is validated, update the submodule pointer in SMW. No compatibility flag, game-specific workaround, or default environment override will remain.

### Regression coverage

Add the smallest deterministic runner-level test that creates distinct writes to the same APU port across a callback-style production burst and asserts the SPC observes each command in order. Exercise both the normal path and turbo-equivalent compressed producer timing. Keep the existing SMW `sfx_probe.py` transition capture as the end-to-end smoke test for SPC and MSU-1.

## Verification

1. Run the new focused runner test; it fails before the fix because the first distinct command is not observed.
2. Apply the minimal shared APU fix and rerun the test until it passes.
3. Run the existing relevant runner test suite.
4. Run the SMW transition capture from the same save state with SPC, then MSU-1; each reports zero `LOST` commands and audio continues after the transition.
5. Record and fix only additional reproducible failures discovered by those runs.

## Non-goals

- Rewriting the SPC emulator, DSP, MSU decoder, or SDL mixer.
- Changing sound design, music selection, or PCM packs.
- Fixing unrelated issues without a reproduction.
