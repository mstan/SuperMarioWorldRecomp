# Option A — Deterministic-Sync Harness for the Block Differ

**Status:** designed, not built. Build trigger: see "When to build" below.

## What this document is

`tools/oracle_block_diff.py` ships as ring-driven (no pause/step), and
the always-on block + insn rings ship turned on at runtime init. The
infrastructure is correct.

The **comparison strategy** is not. Triage of 150+ flags from two
free-running attract-demo captures (2026-04-27) showed every flag was
a benign timing artifact, not a codegen bug. See
`memory/project_differ_design_limitation_2026_04_27.md` for the full
analysis. The two failure modes were:

1. **Loop-iteration mismatch.** The differ matches the Nth occurrence
   of PC across two free-running rings. recomp and snes9x advance at
   different wall-clock rates (recomp's APU pacing is coarser than
   snes9x's cycle-accurate APU emulation). The Nth occurrence of
   PC=$8095 in rec ≠ Nth occurrence in emu — they're different loop
   iterations of the same correct loop.
2. **WRAM-state drift.** Attract demo is RNG/timing driven. By
   rec_frame 94 / emu_frame 549 the two sides have already diverged
   in `Layer1ScrollDir`, `Layer1XPos`, sprite layout, etc. PC-aligned
   compare at that point is comparing two correctly-executing-but-
   different game states.

Without a deterministic sync layer the differ is boot-prolog-only,
and even that is broken because the boot prolog gets evicted from
the 262 144-entry recomp BLOCK ring before any probe can connect
(SPC700 upload loop spins ~5-50× per byte, 30k+ bytes uploaded →
150k-1M+ block entries during boot alone; oldest still-resident
entry at t=1s has bi=61 025, well inside SPC upload).

## When to build (trigger)

Build this only if/when:

- We hit a bug we cannot close via golden-oracle methodology
  (`docs/GOLDEN_TESTING.md`), Tier-1 watchpoints, L3 fuzz, or
  targeted hand-written probes — AND
- The bug's diagnosis would benefit from cross-process state
  alignment (i.e., "I need to know whether rec and emu produce
  identical (A,X,Y) at the same *logical* moment given identical
  inputs").

If we are not stalled on such a bug, do not build this. The visible
bugs we can close one at a time are higher near-term value, and Option
A only pays back when there's a bug whose diagnosis specifically
requires the harness.

## Two halves required

Both must ship together. Building one without the other does not
move the differ from "boot-prolog-only" to "useful."

### Half 1 — deterministic comparison points

snes9x bridge gains:

1. **RNG seed control.** Today both sides have whatever RNG state
   their own boot path produced. Add a bridge cmd to clamp snes9x's
   relevant PRNG sources (joypad-input randomness, anything else
   that diverges per-run) to the same bit-pattern as recomp's
   counterpart at a known anchor.
2. **Reset-state injection.** Both sides must enter `I_RESET` with
   identical WRAM, identical PPU state, identical APU state. Today
   recomp clears WRAM differently than snes9x does on cold reset;
   the harness must inject one canonical reset state into both.
3. **Per-logical-frame stepping.** Replace the current
   `MAX_RETRO_RUNS=80` NMI-wait loop in
   `snes9x_bridge_run_frame` (which is wall-clock-frame-paced) with
   a stepping mode that advances exactly one **logical** 60 Hz SMW
   frame per call, matching what one `RtlRunFrame` advances on the
   recomp side. The two must agree on what "frame" means.
4. **Identical controller inputs.** A scripted input track replayed
   bit-identically on both sides for the duration of the diff
   window. Existing demo-input plumbing is the obvious starting
   point but must be audited for non-determinism (any timing-based
   input transformation breaks this).

Output: when both sides have run N logical frames under identical
inputs from the same canonical reset, the differ comparing rec ring
and emu ring **at frame N** is comparing the same logical moment.
A divergence at that point is a real codegen bug, not a timing
artifact.

### Half 2 — ring coverage

262 144-entry block ring is not enough to cover boot. Pick at least
one of these (combinations work):

- **10M+ entry rings.** Trade memory for coverage. ~256 MB at the
  current per-entry size. Acceptable for an Oracle build. Always-on
  cost on Release|x64 is zero (already gated).
- **Per-PC filtering at write-time.** Block-rate-limit hot loops:
  log only every Nth visit to PCs that fired >M times in the last
  K blocks. Keeps the ring small but loses some loop-body fidelity
  — fine for divergence detection, costs you the loop-iteration
  history.
- **Snapshot-on-signal.** Capture the early-boot ring to disk on a
  known boundary (e.g., end-of-SPC-upload), then rotate to a fresh
  ring for the post-boot window. Two ring slices can be diffed
  independently.
- **Per-PC write-time filter** seeded from the diff target. When
  Half 1 anchor + frame-N target are known, log only the PCs we
  intend to compare — eliminates SPC-loop spam by construction.

The right choice depends on the bug being diagnosed when Option A
is built. Decide at build time, not now.

## Validation criteria

Option A is "done" when:

1. The pre-existing free-running differ run (`tools/oracle_block_diff.py`
   no flags) produces zero divergences in a deterministic-sync window
   covering at least the I_RESET prolog through frame N for some
   N ≥ 60.
2. A synthetic codegen bug introduced into a single emit path
   (e.g., a known-bad phi merge) is detected by the differ at the
   correct PC and frame, with no false positives elsewhere in the
   window.
3. Release|x64 byte-clean.

If those three pass, the differ has graduated from "boot-prolog-only"
to "general validation tool."

## What NOT to do

- Do **not** build Half 1 alone — without ring coverage you still
  can't diff anything more than the first ~50 instructions of boot
  before the ring evicts.
- Do **not** build Half 2 alone — without deterministic comparison
  points you have a 10M-entry ring of timing-noise mismatches.
- Do **not** rebuild this with a pause/step model.
  `feedback_pause_step_is_arm_attach_antipattern.md` is non-negotiable.
  Always-on rings + free-running query is the only acceptable shape.
- Do **not** synchronize by "step until WRAM bytes match" — that's
  itself non-deterministic and is what `demo_sync.py` did wrong
  before the 2026-04-26 redesign.

## Related artifacts

- `tools/oracle_block_diff.py` — the differ itself (correct as
  infrastructure; comparison strategy is what's incomplete).
- `snesrecomp/runner/src/debug_server.c` — recomp BLOCK ring (always
  on at init).
- `snesrecomp/runner/src/snes9x_bridge.cpp` — snes9x INSN + WRAM-write
  rings (always on at init).
- `_triage/divs_full.txt`, `_triage/divs_boot.txt` — preserved
  output from the 2026-04-27 free-running captures that surfaced the
  design limitation.
- `_triage/probe_cluster_b_root.py` — the upstream-WRAM tracer that
  produced the cluster-B benign-drift finding; preserved as
  documentation of the kind of triage Half 1 makes unnecessary.
- `memory/project_differ_design_limitation_2026_04_27.md` — the load-
  bearing memo this doc supersedes for build-trigger purposes.
