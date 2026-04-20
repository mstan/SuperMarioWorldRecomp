# Open issues + session summary from autonomous rip session 2026-04-19/20

## Session summary

Branch: `chore/tier3c-irq-vector` (both repos).

**Framework fixes (snesrecomp/recompiler/recomp.py + tests):**
- STA [dp] / STA [dp],Y in M=0 no longer drops the high byte. New
  `IndirWriteWord` runtime inline. `_emit_sta16` also now handles
  INDIR_Y / INDIR_DPX / DP_INDIR (were falling through to silent
  comment). 6 pinning tests.
- Fall-through-into-excluded-range is no longer emitted as a spurious
  tail call. 2 pinning tests.
- `_emit_function` now emits `RecompStackPop + return` on non-terminal
  bodies with no valid fall-through target so pushed stack frames
  stay balanced.

**Tooling:**
- `tools/sync_funcs_h.py`: orphan-decl deletion, duplicate dedup,
  also scans `snesrecomp/runner/src/*.c` for framework hand bodies,
  prints a scaffolding-smell metric.

**Rips landed on chore/tier3c-irq-vector:**
- Tier 1c: `g_did_finish_level_hook` dead decl.
- Tier 1d (partial): removed 4 `dp_sync` cfg directives from bank0d.cfg;
  bank 0d gen lost 41 stub-call sites. See residual below.
- Tier 3a: `PatchBugs_SMW1` (all 3 hooks were dead given Tier 3c status).
  Null-guarded `PatchBugs()` in the runner.
- Tier 3c/Reset: hand-written `SmwVectorReset` replaced by direct call
  to recompiled `I_RESET` at ROM $00:8000-$806A.
- Tier 3g: rip debug harness, unused vtable slots, HLE SPC executor
  body (~1000 LOC, 33 orphan statics), DspRegWriteHistory field.
  smw_spc_player.c: 1539 -> 71 LOC.
- Dead: `LoadStripeImage_UploadToVRAM` (stripe HLE, 0 callers),
  `UploadOAMBuffer` (superseded-codegen, 0 callers), 8 sprite
  coordinate accessors, `ParseBoolBit`.

**Metrics:**
- Scaffolding smell (hand-bodies in src/*.c): 147 -> 96.
- Release|x64 build: 0 errors, 105 warnings baseline maintained.
- Boot: reaches frame 200+ unchanged; user-confirmed visually
  "equally broken" at session start (ground-rendering bug is
  unchanged because it's a separate codegen issue tracked in
  memory/project_ground_not_rendering_*).
- Parent repo: 16 commits on `chore/tier3c-irq-vector` since main.
  Includes one revert: the globals `ptr_layer1_data / ptr_layer2_data
  / ptr_layer2_is_bg` looked orphan by grep but are live via an
  `extern` decl embedded inside `debug_server.c` — heuristic audit
  tools missed that. No net change for those three; everything else
  stuck.
- snesrecomp subrepo: 4 commits on the same branch — recomp.py
  emitter fixes, PatchBugs null-guard, dead-fn rips in common_rtl
  + common_cpu_infra + debug_server, + SyncDmaChannelToPpuFromSnapshot
  (Tier 1b orphan that had survived the compare-harness rip).
- Release|x64 file: smw_spc_player.c 1539 -> 71 LOC; various other
  src/*.c files shrunk; common_rtl.c lost ~50 lines; debug_server.c
  and common_cpu_infra.c each lost one dead function.

## Open issues after session



## Tier 1d dp_sync residual — dispatch file still calls no-op stubs

After bank 0d regen'd with the `dp_sync` cfg directives deleted, bank
0d's generated `dp_sync_map16_ptr()` / `dp_sync_map16_ptr_bak()`
calls are gone (41 → 0). But `src/gen/smw_0d_dispatch.c` still makes
10 hand-written calls to `dp_sync_map16_ptr_to_dp()` — see lines 48,
59, 64, 78, 87, 100, 113 (and a couple more).

`smw_0d_dispatch.c` claims in its header comment to be "Extracted
from tools/recomp/bank0d.cfg verbatim block" but there is NO
verbatim_start/verbatim_end block in bank0d.cfg. The file is in
practice hand-maintained despite living in `src/gen/`. Rule 7 says
don't hand-edit gen files; this is a real rule-7 conflict since the
file has no generator.

**What I deferred:** deleting the three dp_sync stub no-op bodies
from `src/dp_sync_bridge.c` and removing the file. Can't delete them
while the dispatch file still calls `dp_sync_map16_ptr_to_dp()` — the
build would break. The stubs remain as no-ops; runtime cost is zero.

**Options for next session:**
1. Rewrite `smw_0d_dispatch.c` by hand to drop the `dp_sync_map16_ptr_to_dp()`
   calls (acknowledge rule-7 exception: the file has no generator, it IS
   hand-written). Then delete the stubs + file + funcs.h decls.
2. Build an actual dispatch generator (tools/gen_dispatch.py or similar)
   and regenerate without the dp_sync calls. Heavier lift but removes the
   rule-7 conflict permanently.

Committed as part of the dp_sync cfg removal. Smell count: 146 unchanged
(stubs still in src/dp_sync_bridge.c).

## Tier 3b kPatchedCarrys_SMW — requires framework carry inference

The 45-entry `kPatchedCarrys_SMW` array patches specific ROM ADC/SBC
instruction bytes to 0x00 (BRK); the CPU interpreter's BRK handler in
`snesrecomp/runner/src/snes/cpu.c:768-794` reads the hook, then SETS
or CLEARS carry before re-executing the original opcode. So the list
fixes ROM-native buggy carry state when the INTERPRETER runs those
instructions.

Per the plan (Tier 3b), the right fix is carry-flag inference in
`recomp.py` so recompiled code emits the intended carry state
explicitly and the list can go to zero. That's a bigger framework
task than overnight scope allows.

**What I didn't do:** rip the list outright. The list is currently
still dead for recompiled paths (recomp C doesn't use this mechanism
— it's interpreter-only), but interpreter can still be reached via
excluded regions or unrecompiled banks; pulling the list without the
inference pass could mask the gap where carry inference fails.

**Next session:** land a carry-flag inference pass in `recomp.py`,
validate against SMW (list shrinks to zero), then delete the array
and the `patch_carrys`/`patch_carrys_count` fields on `RtlGameInfo`.

## Tier 3g residual — HLE SPC executor body (~900 lines)

After this session's partial Tier 3g work (rip debug harness + rip
unused vtable slots), the HLE SPC engine body in `src/smw_spc_player.c`
is now entirely unreachable: its only public entry point was the
`gen_samples` vtable slot, which was never called and has been deleted.

Unreachable pieces:
- Spc_Loop_Part2, Sfx0_Process, Sfx3_Process, PlayNote, ComputePeriod,
  WritePitch, Dsp_Write, Sfx0_TurnOffChannel, Sfx3_TurnOffChannel,
  SetEchoVolume, SetEchoOff, Port1_WriteInstrument, Chan_DoAnyFade,
  CalcFinalVolume, and many statics. ~900 LOC.

**Why deferred:** individual hand-body removals work in batches of
5-10 functions max if there's a risk of link-time or compile-time
fallout (e.g. one function calls another, removing wrong one first
breaks link). A full HLE-executor rip needs a dependency audit first
so functions are removed in leaf-first order. Overnight-autonomous
is a poor fit.

**Next session approach:**
1. Build a call graph of just smw_spc_player.c statics.
2. Topologically remove from leaves up, rebuilding after each batch.
3. Keep Spc_Reset + SmwSpcPlayer_CopyVariablesFromRam (live).
4. Keep SmwSpcPlayer_Upload + everything it transitively calls (live).
5. Delete everything else.

Estimated residual after that rip: smw_spc_player.c shrinks from
~1300 lines to ~150 lines.
