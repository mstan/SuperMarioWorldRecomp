# Phase B — differential fuzz harness

## Goal

For every 65816 opcode × addressing mode × M × X × interesting
input state, prove that `recomp.py`'s generated C produces the
same final CPU + memory state as snes9x (the embedded oracle).
Any divergence is a codegen bug. Coverage tracked so we know what
we've verified and what's still dark.

## Why this, why now

Phase A closed 4 concrete width-slip bugs by reading the emitter.
The A-register ROR bug (2026-04-24) took 6 hours of gameplay
debugging to find. Phase A would have caught it if we'd read the
emitter first. Phase B closes the loop: any future ROR-A-shape
bug is caught by running the instruction through both sides and
diffing state, instead of waiting for a visible gameplay symptom.

Also: Phase A audits are point-in-time. Every emitter edit from
here on needs regression protection. Phase B becomes the gate.

## Scope

In:
- Every 65816 opcode the decoder recognizes.
- Every addressing mode that opcode supports.
- Every combination of M (8/16-bit A) and X (8/16-bit X/Y) where
  the opcode is valid.
- A seed set of interesting input states per opcode:
  - Zero, one, max, sign-bit, carry-in (0/1) for arithmetic.
  - DP=0 and DP=0x0100 for DP-mode.
  - Indexed with X=0 and X=non-zero.
- Final state compared: A, X, Y, D, DB, PB, P (flag byte), and
  any WRAM bytes the opcode touched.

Out:
- Full-function execution (that's golden-oracle's job).
- Multi-instruction interaction patterns (future work once
  single-opcode coverage is green).
- DMA, PPU, APU side effects (instruction-level only; hardware
  registers compared at the byte level, not through their side
  effects).
- BRK / COP / WAI / STP / WDM — handled as comments by the
  emitter; not arithmetic, skip.

## Architecture

Three components, built in order:

### 1. Snippet runner (recomp side)

Input: raw 65816 bytes + initial CPU state + initial WRAM slice.
Output: final CPU state + final WRAM slice, after running the
snippet through `recomp.py` → compile → execute.

Implementation: a standalone C driver under `snesrecomp/fuzz/`
that:
- Accepts a JSON stdin describing the snippet + initial state.
- Calls `recomp.py` as a library (not a subprocess — we already
  expose `emit_function`).
- Writes the emitted C into a scratch file, compiles it with the
  same compiler used for gen.
- Dynamically loads the scratch object, seeds the CPU state,
  calls the snippet's entry function, dumps final state to stdout
  JSON.

One-per-snippet compile is expensive (~100 ms). Batch by grouping
~100 snippets per compile unit.

### 2. Snippet runner (oracle side)

Same input/output, but runs the snippet through snes9x via the
existing oracle backend. snes9x's `cpuops.cpp` executes the
65816 instructions directly — no codegen path to test.

Implementation: extend `snesrecomp/runner/src/snes9x_bridge.cpp`
to accept a "load this ROM, seed these registers + WRAM, run N
insns, dump state" RPC. Re-uses the blocking-step plumbing we
added this session.

### 3. Differ + coverage DB

SQLite file under `snesrecomp/fuzz/results.db`. Schema:

```
CREATE TABLE runs (
  opcode INTEGER,       -- 0-255
  mode TEXT,            -- 'IMM', 'DP', 'ABS_X', ...
  m_flag INTEGER,       -- 0 or 1
  x_flag INTEGER,       -- 0 or 1
  seed TEXT,            -- JSON string describing initial state
  recomp_final TEXT,    -- JSON final state
  oracle_final TEXT,    -- JSON final state
  matched INTEGER,      -- 0/1
  ts DATETIME,
  PRIMARY KEY (opcode, mode, m_flag, x_flag, seed)
);
```

Queries:
- `SELECT opcode, mode FROM runs WHERE matched=0` — regression list.
- `SELECT DISTINCT opcode, mode FROM runs` — what's covered.
- Coverage gaps: left-join against an opcode-mode truth table.

### Layout on disk

```
snesrecomp/fuzz/
  README.md               (points at this doc)
  opcode_table.json       (derived from snes9x dispatch)
  snippets/               (generated ROM fragments)
  results.db              (SQLite, gitignored)
  runner.py               (orchestrator)
  run_recomp.c            (recomp-side runner)
  run_oracle.cpp          (oracle-side, compiled into Oracle build)
```

## Build order (rough, one commit per step)

1. **Opcode table.** Parse snes9x's `cpuops.cpp` dispatch into
   JSON. Each entry: opcode byte, mnemonic, mode, M-dep, X-dep,
   cycles. ~300 lines of Python. Commit the JSON alongside.
   Validated by: every entry resolvable via `decode_insn`.
2. **Snippet generator.** Walk the opcode table; for each entry
   produce a minimal ROM (prologue to set M/X, the instruction,
   RTS). Seed set: ~4 inputs per opcode. Total: ~3500 snippets.
   Output to `snippets/`. Commit generator; don't commit the
   snippets themselves (derivable).
3. **Recomp runner.** Takes a snippet + initial state, runs,
   dumps final state. Standalone exe. Compile in a scratch dir.
4. **Oracle runner.** New TCP command `fuzz_run_snippet` on the
   Oracle build. Takes JSON, runs, dumps.
5. **Differ.** Python orchestrator: for each snippet, call both
   runners, diff, insert into SQLite.
6. **Coverage report.** Python script that queries the DB and
   prints a coverage matrix + failure list.
7. **CI hook.** Regen+run on every commit that touches recomp.py.
   Fail on any regression (matched=0 where previous run was
   matched=1).

## Non-goals of this doc

- Specifying the exact JSON wire format (we'll iterate once the
  runners exist).
- Deciding the compile-batching granularity (tune after first
  end-to-end run).
- DMA/HDMA/PPU side-effect modeling — instruction-level only.

## Exit criteria

Phase B is "done" when:

1. Every 65816 opcode × addressing mode × (M, X) is represented
   by at least one seed in the DB.
2. Every entry's recomp-vs-oracle result is `matched=1`, or the
   divergence is documented as a known limitation.
3. A single `python snesrecomp/fuzz/runner.py` command rebuilds
   the DB and exits nonzero on any regression.
4. The command is wired into CI (or an equivalent pre-push hook)
   so emitter changes that break codegen fail before merge.

The ROR-A bug, re-filed in the fuzz DB as a seed, would have been
caught at step 2 the first time we ran.
