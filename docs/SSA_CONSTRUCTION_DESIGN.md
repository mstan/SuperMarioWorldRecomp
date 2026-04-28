# SSA Construction Design — replacing heuristic phi machinery

## Why this exists

The recompiler currently tracks per-instruction register values
(`self.A`, `self.X`, `self.Y`, `self.B`) as C expression strings, and
synthesizes phi merges at multi-predecessor labels through a stack of
heuristic mechanisms:

1. `_label_a/b/x/y[pc]` — vars captured at label-emit time, used as
   "the label's vars" for backward-branch sync.
2. `_branch_states[pc]` — per-target merge dict storing materialized
   `branch_a/x/y` vars from forward conditional branches.
3. `_phi_preallocated` + pre-allocation in `_emit_backedge_phi` —
   patches the case where a forward JMP/BRA targets a not-yet-emitted
   multi-predecessor label.

The first two coexist; the third was added to plug a specific gap. They
interact in non-obvious ways:

- `_label_a[pc]` and `_branch_states[pc]['A_var']` can refer to
  different vars, depending on which mechanism fired first.
- `_phi_preallocated` overrides `_label_a[pc]` to a fresh var at label
  emission, but only for the predecessors `_emit_backedge_phi` was
  called from (JMP, BRA, BRL, fall-through). Conditional branches
  (BEQ/BNE/BPL/BMI/BCC/BCS/BVC/BVS) are NOT covered.
- Extending phi-prealloc to conditional branches (attempt 2026-04-27)
  passed unit tests but regressed Mario's input handling at runtime:
  more labels became phi-prealloc'd, more functions hit X-Y aliasing
  breakage, the `default_init_y = x` cfg directive's semantics broke
  for some functions on Mario's code path.

The pattern: each new heuristic patch passes its targeted unit test
and breaks something else somewhere. The 2026-04-27 phi-prealloc
commit message itself notes: *"Broad version regressed Mario sprites;
narrowing closed those."* This collision class is structural —
heuristic phi placement cannot be made path-precise without proper
SSA construction.

## What "proper SSA construction" means here

Standard compiler-textbook SSA construction:

1. **Build a control-flow graph (CFG)** of basic blocks per function.
2. **Compute dominators** (each block's immediate dominator + the full
   dominator tree).
3. **Compute dominance frontiers** (DF(b) = set of blocks where b's
   defs may need to be merged with other paths' defs).
4. **Place phi nodes** for each register V at the dominance frontier
   of every block that defines V. Iterate to fixpoint (a phi node is
   itself a def, so it can induce more phis downstream).
5. **Rename register references** to versioned values (the SSA form
   proper). For our purposes, this is implicit — the recompiler's
   `self.A/X/Y/B` already track per-block values; the rename happens
   naturally as we emit C variable assignments.
6. **Emit C with phi vars at merge points** — each phi node at block
   B becomes a hoisted C var; each predecessor's exit edge emits
   `phi_var = current_value;` for every phi B has.
7. **Dead-phi elimination** — remove phi nodes whose vars are never
   read by the subsequent code.

The key win: phi placement becomes *algorithmically necessary* (only
at true SSA join points where a register's value can differ between
predecessors), not heuristic. No more "should I emit phi at this
BEQ?" judgement calls.

## Current vs. SSA: what changes

### What the current emission does

For each linearly-decoded instruction:
- Tracks `self.A/X/Y/B` as C expression strings (the "value of this
  register *right now* in this block").
- At branch instructions, materializes regs to fresh vars, stores
  them in `_branch_states[target]`.
- At label emissions, runs the branch_states merge code, then
  optionally adopts pre-allocated phi vars.

For most simple functions, this is correct. For multi-predecessor
labels, it's correct only when the heuristic fires correctly.

### What SSA emission would do

For each linearly-decoded instruction (same walk order):
- Tracks `self.A/X/Y/B` as before within a block.
- At each branch, the CFG already says which blocks follow.
- At each label that the CFG marks as a phi join for register V,
  the label's V-var is a pre-determined `phi_<pc>_<V>` var.
- At each predecessor's exit (the branch site), emit
  `phi_<pc>_<V> = self.V;` for each V that the target needs phi for.
- At label emission, set `self.V = phi_<pc>_<V>;` for each phi V.
- The branch_states + label-capture + phi-prealloc machinery goes
  away.

### What stays the same

- `Insn` decoding, cfg-file reading, function discovery — unchanged.
- Per-instruction emit logic (LDA, STA, ADC, etc.) — unchanged. They
  still update `self.A/X/Y/B`.
- The `_materialize` / `_simple` / hoisted-var allocation — unchanged
  primitives, used by the new path too.
- All non-merge-related heuristics (RetAY/RetAXY return shapes, X-Y
  aliasing for `default_init_y = x`, `_ensure_mutable_x`, etc.) —
  unchanged. SSA only replaces the phi-merge mechanism, not the
  per-instruction value tracking.

## Phased plan

Each phase ships with tests + visual eyeball before the next phase
starts. Old emission stays alive throughout; SSA emission is gated
behind a feature flag until Phase 5.

### Phase 0 — design + lock baseline
- This document.
- Lock the new attract-demo invariants in
  `test_attract_demo_regression.py` (built 2026-04-27) as the visual-
  regression safety net for every later phase.

### Phase 1 — CFG construction (pure analysis)
- New module `snesrecomp/recompiler/cfg.py`:
  - `BasicBlock(start_pc, end_pc, insns, successors, predecessors,
    terminator)` dataclass.
  - `CFG(blocks, entry_pc, dominators, immediate_dominator,
    dominance_frontier)` dataclass.
  - `build_cfg(insns, valid_branch_targets, end_addr) -> CFG`.
- Reuses existing `_successors(addr)` logic from `recomp.py`.
- Tests in `snesrecomp/tests/test_cfg_construction.py` pin block
  boundaries, edges, dominators, DFs on hand-traced ROM examples
  (linear function, conditional, loop, multi-pred merge).
- **No production code path uses the CFG yet.** Existing emission
  unchanged.

### Phase 2 — phi placement (pure analysis)
- Add `compute_phi_placements(cfg, defs_per_register) -> Dict[block_pc,
  Set[register]]`.
- For each register, walk def sites + DF iteratively.
- Tests pin placements on hand-traced examples that the current
  heuristics get wrong (the BEQ-into-multi-pred case from
  HandleLevelTileAnimations).
- Still no production usage.

### Phase 3 — parallel SSA emission behind feature flag
- New `EmitCtxSSA` class (or a flag on `EmitCtx`) that uses
  `_phi_placements` to drive phi emission instead of `_branch_states`
  + `_label_a/x/y/b`.
- Per-function cfg directive `ssa: true` opts a function in.
- Differential test: emit one small function (e.g., a single-block
  RTS-only function) under both paths, assert C output is
  semantically equivalent.
- Ship with ZERO production functions opted in.

### Phase 4 — bank-by-bank migration
- Flip `ssa: true` on entire banks one at a time.
- After each bank flip:
  1. Full regen.
  2. Run framework tests (`run_tests.py`) — must pass.
  3. Run attract-demo invariants — must pass (no Mario regression
     etc.).
  4. Visual eyeball — user runs the Release exe, confirms gameplay
     is still equivalent.
- If a bank breaks, the flag is per-function so we bisect inside the
  bank, find the breaking function, ship the bank without it, file
  the function as a known-incompatible site.

### Phase 5 — delete old machinery
- Once all banks are on SSA path:
  - Remove `_label_a/b/x/y` writes (label-emit-time capture).
  - Remove `_branch_states` and its merge code.
  - Remove `_phi_preallocated` + pre-allocation in
    `_emit_backedge_phi`.
- Massive simplification of `recomp.py` emission code.

### Phase 6 — dead-phi elimination + X-Y aliasing
- Walk emitted body, find phi vars never read, drop the corresponding
  `phi_var = ...;` assignments at predecessor exits.
- For X-Y aliased registers (`default_init_y = x` cfg or natural code
  patterns), emit a *single shared phi var* instead of separate
  `phi_X` and `phi_Y` to preserve aliasing semantics through merges.
  Resolves the breakage class my failed BEQ-phi-prealloc fix hit.

## Rollback story

Each phase has a clean rollback because:
- Phases 1-2 add new code that nothing else uses yet — revertible by
  deleting files.
- Phase 3 adds parallel emission behind a flag — flag-off reverts to
  old path.
- Phase 4 is per-function flag — any breakage is isolated to one
  function and easy to revert.
- Phase 5 is the irreversible step. Prerequisites: all phases 1-4
  green for at least one full session of stress (visual eyeball +
  attract-demo invariants + framework tests).

## What this won't fix

- Per-instruction emit bugs (e.g., wrong width on STA [dp]) — these
  live in the per-insn handlers, not in phi machinery.
- Function-signature inference (RetAY/RetAXY/RetY) — a separate
  inference pass, unchanged.
- Dispatch-table over-decode — separate pass.
- Tier-1+ reverse debugger hooks — unchanged.

The scope is explicitly: replace the heuristic phi-merge code with
algorithmically-correct SSA. Everything else stays.

## Success criteria

After Phase 5 ships:
- The 2026-04-27 berries-as-?-blocks + yoshi-?-block-doesn't-activate
  bugs close (or are reproducible-and-actionable through structured
  golden-methodology, no longer drowned by phi-merge noise).
- The 2 known-broken yoshi attract-demo invariants pass.
- Mario-on-input-path codegen patterns are not broken by future
  phi-related changes (because phi placement is no longer heuristic).
- The `recomp.py` emission code shrinks by hundreds of lines as the
  heuristic machinery is deleted.

After Phase 6 ships:
- Dead phi vars are eliminated, gen'd C is smaller.
- X-Y aliasing breakage no longer occurs at multi-pred merges.

## References

- Cytron et al. 1991, "Efficiently Computing Static Single Assignment
  Form and the Control Dependence Graph" — the standard SSA-
  construction algorithm.
- `snesrecomp/recompiler/recomp.py` — current heuristic phi machinery
  at lines ~3275 (`_emit_backedge_phi`), ~3806 (label adoption),
  ~5314 (BRA branch_states), ~5407 (backward-branch sync).
- `snesrecomp/tests/test_attract_demo_regression.py` — the safety net
  this design relies on.
- `docs/GOLDEN_TESTING.md` — the per-bug debugging methodology that
  surfaces the bugs SSA construction will let us actually fix.
