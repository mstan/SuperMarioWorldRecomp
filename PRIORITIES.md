# Priorities

North star: **all-native-code execution of stock SMW**. Every hand-written
body in `src/smw_*.c` is a gotcha waiting to happen — divergence from the
ROM, wrong calling convention, silent behavior drift. The recompiler is
the only authoritative translator; every line we can retire to gen is a
line we can't have a bug in.

Work is tracked top-to-bottom. Complete a section before starting the
next. Within a section, commit after each landing so rollback is cheap.

---

## Done

- **Issue A — dispatch_known_addrs in augment pass** (snesrecomp c0074c2).
  `recomp.py` no longer decodes dispatch-handler bodies into the caller's
  insn list during `_augment_cfg_sigs_one_pass`, cutting phantom live-in
  cascade. Shipped with `test_decode_func_terminates_dispatch_when_all_
  handlers_known`.
- **SMW_DISABLE_LM compile-time toggle**. `HAS_LM_FEATURE(i)` and
  `HAS_HACK(i)` collapse to `0` when `SMW_DISABLE_LM` is defined
  (default ON in all four vcxproj configs). MSVC dead-codes the Lunar
  Magic emulation layer out of the binary. Undefining the macro restores
  LM-patched-ROM support.
- **SMWDisX harness v0.1** (`tools/smwdisx_compare.py`). Parses
  `SMW_U.sym` for per-label code/data classification; runs `decode_func`
  on every cfg func; flags instructions whose addresses land inside
  SMWDisX-declared DATA regions. Auto-detects dispatch helpers before
  decoding. Current baseline: 97.4% pass rate, 47 FAILs (decoder walks
  into data — separate triage bucket from skips).
- **Skip elimination — all four tiers complete**. 38 → 1 skip across
  the codebase. The one remaining skip (`Spr036_Unused_DataTable` in
  bank 01) is a legitimate no-op: the ROM's CallSpriteMain dispatch
  table has `dw DATA_01E41F` for sprite $36, which points at real data
  bytes. Empty body matches observed ROM semantics (rule -1 compatible).
  - Tier 1 (bank 0c): 2 → 0 — phantom auto_XX deletions
  - Tier 2 (bank 02): 3 → 0 — struct-return via hand-body-aware sync
  - Tier 3 (bank 01): 6 → 1 — struct-return family (CheckTilting,
    Spr0A7, Spr05F, Spr029_IggyLarry) + carry-return sigs
  - Tier 4 (bank 03): 27 → 0 — clipping/collision, SubOffscreen
    multi-entry, phantom dispatch fakes (Firework table cap), Mode7
    tilemap/sprite anim, Peach/Bowser/KoopaKid/GameMode12, Spr0BD
    carry-merge, Spr0A0 ReturnsTwice (PLA/PLA/RTS auto-detected by
    recompiler — no framework work needed)
- **Framework machinery that landed**: hand-body-aware reconciliation
  in `sync_funcs_h.py` (scans `src/*.c` + cfg verbatim blocks; when no
  hand body exists, funcs.h rebuilds from cfg ret-type + gen params,
  filtering out non-register pointer/struct params so stale sigs get
  dropped). Pinned by 3 tests in `test_sync_funcs_h.py`.

## Landed: real SPC via recompiled code (2026-04-19)

Route A landed. `g_use_my_apu_code = false` is the default. Real
SPC audio runs through the recompiled `HandleSPCUploads_Inner` IPL
handshake + byte-upload loop. No bifurcation; `g_spc_player` is
off the critical path (still in the tree, queued for retirement).

Three framework fixes required to reach this state, each with a
pinning test:

- `SEP #$20` narrows A to `(uint8)` so 8-bit ops post-SEP see
  the low byte only (snesrecomp@7dc2cdc).
- Decode-order-≠-ROM-order insn emission now injects an explicit
  `goto label_<pc+length>` when natural C fall-through would land
  on wrong code (snesrecomp@48a11cd).
- Loop-header A/B/X/Y phi assignments emitted at every back-edge
  goto, including fall-through back-edges (snesrecomp@990cb33).
  B was not previously tracked at labels — REP #$20 merges A+B,
  so a loop body that refreshes B via XBA-after-LDA needs the
  header's B-var updated on the back-edge.

Runtime cleanup in the same commit: `g_is_uploading_apu` deleted
(redundant with `g_use_my_apu_code`). `snes_readBBus` / `RtlApuWrite`
under real SPC go straight to the APU, lock-serialised with the
audio thread. `RtlRenderAudio` no longer gates on an upload flag.

Harness 2052/2052 (+1 over pre-real-SPC: HandleSPCUploads_Inner
is now a real function being checked). All 79 framework tests pass.

## Active: chase the first real divergence under real SPC

Real SPC is landed but the $03 trace still differs from the oracle
at boot — recomp writes nothing, oracle writes `$00 → $FD` at
frame 0 and `$FD → $04` at frame 2. That baseline hasn't moved
since session start, so SPC was one confounding source, not the
only one. Now that the SPC handshake isn't masking anything, the
`$03` divergence is the live signal for whatever framework gap
fires next.

Plan: run `/recomp-debug` Phase 3 classification on the $03
divergence.

1. Sync both runtimes to frame 0, scan the full WRAM delta via
   `tools/divergence_diff.py scan --advance --start 0 --end 500`
   — the target byte isn't necessarily $03; it's wherever the
   first deterministic write differs.
2. For each divergent address, ring-buffer-trace both sides to
   find the first write that disagrees. The WRITING function on
   the oracle side names the code region the recomp isn't
   reaching (or is reaching with wrong inputs).
3. Classify: framework bug → fix the recompiler. Runtime bug →
   fix the runtime. ROM-faithful behaviour → update expectations.
   No cfg additions that encode framework-derivable facts.

Stop condition: one pinning test lands for whatever bug surfaces
(rule 1b), recomp $03 trace matches oracle through frame 5, and
we can demonstrate any further divergence is genuinely downstream.

### Scope guardrails

- Do not retire `g_spc_player` yet — it's not on the critical path
  anymore but keeping it compiled proves the HLE code still builds.
  Queued for a separate targeted deletion commit after $03 lands.
- Do not touch `kPatchedCarrys_SMW[]` in this pass. That's a
  framework gap (carry-flag inference), queued separately.
- The harness is 2052/2052 so static checks aren't the pain point
  right now. Dynamic (runtime) behaviour is where bugs hide.

## Queued (post-$03, pre-hardening)

These all have to happen eventually. Order is "active" by information
density, not absolute priority — the $03 chase surfaces unknowns,
these are known work items we already understand the shape of.

- **Retire `g_spc_player`** (HLE SPC player, `snesrecomp/third_party/
  spc_player/*`). Off the critical path since ec2d971. Pure deletion
  commit; no framework work. Blockers: grep for all `g_spc_player`
  references (RtlPopApuState_Locked, RtlApuReset, RtlApuUpload,
  RtlRestoreMusicAfterLoad_Locked, RtlSaveMusicStateToRam_Locked,
  RtlRenderAudio) and simplify each to the real-SPC path only. Risk:
  low. Reversibility: trivial.

- **Carry-flag inference — retire `kPatchedCarrys_SMW[]`**. ~50 entries
  in `src/smw_cpu_infra.c` that force the CPU emulator to patch carry
  at specific ROM addresses the recompiler can't infer statically.
  Framework amortization: any recompiled game hits carry-inference
  gaps; improving the analysis removes this table + generalises.
  Specific patterns to understand: carry-in at function entry when
  callers set C via SEC/CLC before JSR; carry propagation through
  branches where C is live across the join. Needs a pinning test per
  pattern. Estimated multi-session.

- **SMWDisX harness v0.2 — mnemonic + operand parity**. v0.1 catches
  "decoder walked into data." v0.2 catches "decoder read a different
  instruction than SMWDisX did at the same address." Tooling, not a
  game-behaviour change; low risk, separable. (Design details in the
  detailed queue section below.)

- **Warning elimination — Issue B (FuncU8J/A/JA union-sig dispatch)**.
  Dispatch-target guard at `recomp.py:1627` caps handlers to
  `void()` or `void(uint8 k)`. Wider union typing collapses the
  remaining `RECOMP_WARN: X/A/j unknown at call site` warnings.
  (Details in queue section below.)

## Queued details: SMWDisX harness v0.2 — mnemonic + operand parity

v0.1 catches "decoder walked into data." v0.2 catches "decoder read a
different instruction than SMWDisX did at the same address." Needed
because v0.1 passes don't prove instruction correctness — they only
prove we're at least in the code region.

Design: parse each `bank_XX.asm` line-by-line to extract
`(addr, mnem, operand)` per instruction. Compare against `decode_func`
output. Handle:
- Macros (`%BorW(LDA, addr)`, `%insert_empty`, `%WorB`, etc.) — expand
  from `SMWDisX/macros.asm` by substitution.
- `if ver_is_XXX(!_VER)` conditional blocks — select U-ROM branch (done
  in v0.1's prototype parser, port properly).
- `con($XX,$XX,$XX,$XX,$XX)` per-version constants — pick [1] for U.
- Anonymous labels (`+`, `-`, `++`, `--`) — re-resolve per basic block.

Growth plan:
- v0.2: mnemonic + operand parity (this phase)
- v0.3: M/X state tracking
- v0.4: full macro + conditional handling
- v0.5: repo-wide pass-rate dashboard in CI

## Queued: warning elimination

Current warning count: ~58. Two work items:

- **Issue B — FuncU8J / FuncU8A / FuncU8JA union-sig dispatch**.
  Dispatch-target guard at `recomp.py:1627` caps `FuncU8*` handlers to
  `void()` or `void(uint8 k)`. Collect dispatch targets, compute union
  live-in across handlers, widen all handlers in each table to the
  union sig, emit matching cast type. Target: the remaining
  `RECOMP_WARN: X/A/j unknown at call site` warnings collapse.
  Typedef family goes in `snesrecomp/runner/src/types.h`.

- **Live-in rescue for mid-body PH/PL scribble-restore pattern**. When
  `…PHX ; TYX ; JSL ; PLX ; <read X>` appears mid-body, entry-X is
  legitimately live-in but current `_insn_reg_use` (recomp.py:499)
  doesn't detect it because it intentionally skips PH/PL as register
  reads. Only pursue if harness v0.2 surfaces false narrowings.

## Harness-flagged FAIL triage — DONE

47 FAILs → 0 FAILs / 100.0% pass rate (2051/2051). Three framework
fixes + one stale-cfg deletion. NO cfg `end:`/`name`/`exclude_range`
additions.

Harness pipeline fixes (commit d6d4a5a):

 1. Harness now calls `discover_bank` + `promote_sub_entries` before
    decoding, matching the real regen pipeline. Without that step the
    harness's `known_func_addrs` was incomplete, so dispatch-entry
    acceptance was looser (non-known entries treated as maybe-real,
    causing the decoder to over-read past table ends in the harness
    but not in the real build).
 2. Code-vs-data check now uses the parser's per-byte `data_addrs`
    set (from db/dw/dl/dd directives) instead of label-region lookup.
    The label-based check false-positived when an anonymous-labeled
    code block (`+`/`-`) sat between a `DATA_XX` label and the next
    `CODE_XX` label — not in SMW_U.sym.
 3. `dispatch_known_addrs` now threaded into decode_func so the Issue
    A terminal-dispatch fix applies to harness decodes too.

Framework fixes (snesrecomp 5a92fec):
 1. **Dispatch-table byte range filter** in `discover.py` — rejects
    seeds that land inside inline dispatch table bytes. When an
    earlier walker mis-sizes an instruction (typically 8-bit vs
    16-bit A-mode immediate width), the resulting byte shift can
    produce a phantom JSR/JSL target whose operand lands inside
    some other function's pointer table. These false-positive
    seeds used to be added to the worklist unfiltered. Now rejected
    at add-time (via lookup against accumulated dispatch ranges)
    and in a post-filter sweep for seeds added before the
    containing table's walk.
 2. **Known-handler cluster break** in `decode_func`'s dispatch
    reader — once at least one known-function entry has been
    accepted, any subsequent unknown entry treats as end-of-table.
    Real SNES dispatches are contiguous runs of pointers to real
    code; transition from known→unknown after a known cluster
    almost always means the reader has fallen off the real table
    into data that happens to parse as a valid $8000+ address.

One cfg cleanup (commit 208cc06):
 - Deleted `func GameMode12_PrepareLevel_03DAE2` — orphan entry
   pointing at DATA_03D9DE bytes, zero callers. Legitimate rule 0c
   deletion (cfg entry predating recompiler fix that made it dead
   weight). NOT a new cfg addition.

### Rule-0 lesson that drove these fixes
During initial triage I reflexively added cfg `end:`/`name:`/
`exclude_range` entries to close FAILs. User pushed back: every one
of those edits would have encoded framework-derivable facts as
per-game data, and would recur in Contra III / Mega Man X / any
future SNES game. The correct fix was architectural — teach
discover/decode_func to detect dispatch-table boundaries properly.
That fix amortizes across every game.

Captured in `CLAUDE.md` rule 0a (the north star is the framework,
not green numbers) and rule 0b (bias toward holistic completeness,
never toward speed). Memory: `feedback_north_star_framework_not_tests.md`,
`feedback_cfg_is_last_resort.md`.

## Hard rules in force

See `CLAUDE.md`. Key ones for this priority list:

- Recompiler is the authority; cfg is last-resort for things not
  derivable from ROM (rule 0). Each skip removed that doesn't come
  back is a recompiler fact gained.
- SMWDisX is the primary literal-code oracle (session start rule 3).
- No stubs, placeholders, compat shims (rule -1).
- Tests ship with framework changes (rule 1b).
- Generated files never hand-edited: `src/gen/*_gen.c`, `recomp/funcs.h`
  (auto-block only — preamble is preserved/hand-maintained),
  `src/gen/bank_range.h` (rule 7).
- Commit snesrecomp first when framework changes; SMW commit references
  the snesrecomp SHA.
