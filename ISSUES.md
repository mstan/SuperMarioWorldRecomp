# Open issues — SMW recomp

## Working policy (read first)

We do not fix individual visible bugs in isolation. Each visible
symptom is a probe into a recompiler / framework gap. The goal is
to identify the **underlying class of recompiler bug** that
generated the symptom, fix that class in the framework
(`snesrecomp/`), regen all banks, and re-evaluate every symptom
together. Per-game cfg shimming is last-resort (Rule 0).

If individual symptom diagnosis is necessary to extract the
underlying pattern, do that work — but the deliverable is always a
framework fix, never a per-symptom patch.

Methodology: golden-oracle (`docs/GOLDEN_TESTING.md`) — diff
recomp vs embedded snes9x at the same state-sync point, narrow to
seed byte → write trace → call trace → block trace → instruction
trace → framework fix.

---

## Session 2026-05-16 — DA49 M/X claim trip is verifier-only, benign at site, latent async-x_flag-write upstream

**Context:** End-of-session free-run probe on commit `4c40c40`
(top-level main) / `808e918` (snesrecomp main) — the autoroute
fixpoint fix had just landed, and the F465 + F461 + 9D30
`exit_mx_at` hints were all subsumed and removed. A 5-min verifier
sweep reached frame 4581 before latching on a single trip:

```
func:  LoadOverworldLayer1AndEvents_04DA49_M1X0
pc24:  $04:DA49
claim: m=1, x=0      runtime: m=1, x=1
stack: InitAndMainLoop_ProcessGameMode_M1X1
       → GameMode0C_LoadOverworld_M1X1
       → LoadOverworldLayer1AndEvents_M1X1
       → LoadOverworldLayer1AndEvents_04D7F2_M0X0
       → LoadOverworldLayer1AndEvents_04DA49_M1X0
```

User playtest immediately after: Yoshi's Island plays end-to-end
including the Iggy boss platform — **no observable regression**
despite the verifier alarm.

### Why this trip is benign at the call site

DA49's first ROM instruction (verified by `_triage/walk_f465.py`-
style hex dump):

```
$04:DA49  C2 30  REP #$30
$04:DA4B  A5 0F  LDA $0F
$04:DA4D  29 F8 00  AND #$00F8
...
```

The opening `REP #$30` clears both M and X bits in `cpu->P` and
syncs `cpu_p_to_mirrors`, so `cpu->m_flag` and `cpu->x_flag` are
both 0 by the time any width-sensitive instruction runs.
Whichever (M, X) state the function was entered at, the body
normalizes to (0, 0) at the first instruction. The verifier
alarm fires BEFORE that REP runs (`cpu_trace_func_entry` is the
first thing in the emit), so the mismatch is recorded but never
materializes into wrong-width execution at DA49.

### Why the verifier trip is real (not a false positive)

`LoadOverworldLayer1AndEvents_04D7F2_M0X0`'s body was statically
audited:
- `$04:D7F2: C2 30  REP #$30` — opens by clearing M and X.
- `$04:D7F7: E2 20  SEP #$20` — sets M only.
- No further M/X-touching op in the entire body (verified via
  `grep cpu->P = | grep -v ~0x82` in the M0X0 emit body — only
  two non-NZ P writes, both at the entry REP/SEP pair).

Decoder's static (m=1, x=0) at the JSR-to-DA49 site is faithful
to the bytes. Yet runtime arrives at DA49 with `cpu->x_flag=1`
and `cpu->P=0x30` (bit 4 set). Something asynchronous —
overwhelmingly likely an NMI/IRQ entry/exit path — is setting
`cpu->x_flag=1` mid-stream of D7F2's body.

### Latent risk

DA49 happens to be self-normalizing at entry, so this specific
trip causes no visible damage. The same async-write event could
trip at **another** M0X?/M1X? variant entry whose body does NOT
start with a REP/SEP — in which case the width mismatch would
flow into actual emit code (operand widths, indexed addressing).
Symptom would manifest as silent data corruption on
overworld-load codepaths or wherever the async source fires.

### Instrumentation now in place — `mx_async_check` tripwire

Built on 2026-05-16 (session-internal). Auto-arms at boot. See
`snesrecomp/docs/TRIPWIRES.md` for the framework + commands. Briefly:
every legitimate cpu->m_flag / cpu->x_flag write calls
`cpu_trace_px_record` which increments `g_px_mutation_count`. The
tripwire snapshots `(m_flag, x_flag, g_px_mutation_count)` at every
`cpu_trace_block` hook; if flags change without the count changing,
an async writer mutated them between checkpoints. Latches one-shot
with frame + block PC + recomp call stack.

TCP: `mx_async_check_get` returns the trip snapshot.

**Note (2026-05-16 smoke test):** with the new tripwire armed, the
DA49 M/X *claim* trip ALSO didn't reproduce at frame 4581 in a free-
run sweep that reached frame 7209+. Two readings: (a) the bug is
sensitive to hot-path timing and the extra work in
`cpu_trace_block` shifted execution enough that the divergent
codepath isn't reached at the same point; (b) the prior trip was a
one-shot artifact of that run's specific NMI/oracle synchronization.
Either way, the tripwire is now armed and will catch the trip — or
any sibling-class trip — on a future run.

Suspect for the async writer (when it does fire): NMI handler's
P-restore path, possibly the recomp's `I_NMI` emit body or
`cpu_p_to_mirrors` interaction with the runtime's NMI dispatch
glue.

### Not in scope this session

The trip is captured here for the next investigator. Continuing
with Donut Plains gameplay testing on main (commit `4c40c40` +
`808e918`). If a downstream visible regression points back to
this class — particularly anything Map16-related on overworld
load — pivot here first.

### Tools left for the investigation

- `_triage/probe_mx_claim.py` — minimal TCP client that queries
  `mx_claim_check_get`; banner-aware. Existing.
- `_triage/walk_f465.py` — body-walk script template (deleted at
  session end; restore from git log of branch
  `fix/runblockcode-f28c-m0x1-gap` if needed). Can be adapted to
  walk any function under any (m, x) entry variant and dump every
  M/X-touching op.

---

## Session 2026-05-08 — attract demo remaining issues (branch: fix/attract-demo-remaining-bugs)

**Context:** Koopa-on-slope physics fixed by Codex on `fix/koopa-slide-physics` (merged to main
2026-05-09). Two visible bugs remain in the attract demo as of the screenshot captured post-fix.
Neither has been investigated yet. Work is tracked on `fix/attract-demo-remaining-bugs`.

### Issue F — Pokey falls through the map (attract demo) — FIXED 2026-05-11

Pokey (sprite $70, NOT Spiny $06 as originally guessed) spawned during the late attract demo
and fell straight through the ground every cycle, instead of landing and being eaten segment-
by-segment by Yoshi. Sprite identity verified by polling `$7E:009E,X` during the demo
(`_triage/poll_sprites.py`); Pokey appears briefly in slots 9 and 6.

**Root cause:** wrapper bypass on `$01:9138` (`PHB / PHK / PLB / JSR $9140 / PLB / RTL` wrapper
around the `HandleNormalSpriteLevelCollision` body at `$01:9140`). Cross-bank `JSL $01:9138`
callers from bank 02 (13 sites incl. Pokey's at `$02:B6E8`) and bank 03 (4 sites) had
`name 019138 HandleNormalSpriteLevelCollision` aliasing the wrapper PC directly to the body
function name → recompiler emitted direct calls to the body, skipping the DB transition. Body
ran with `DB=$02/$03`, so `LDA.W SpriteObjClippingX/Y,Y` (bank-01 ROM tables) read garbage
bytes from the wrong bank → bounding-box returned wrong floor offset → tile lookup at
`$01:9523` mis-indexed Map16 → `$1588 |= $04` never set → Pokey's check at `$02:B6F3`
(`AND.B #$04 / BEQ +`) always took the no-floor branch → Y-speed never zeroed → Pokey fell.

Same wrapper-bypass class as Issue G ($01:8042 / $01:90B2) and Codex's $01:802A fix in
commit 9dc3131.

**Fix:**
- `recomp/bank01.cfg`: declared `func HandleNormalSpriteLevelColl_Wrap 9138 sig:void(uint8_k)`
- `recomp/bank02.cfg` + `recomp/bank03.cfg`: renamed
  `name 019138 HandleNormalSpriteLevelCollision` → `HandleNormalSpriteLevelColl_Wrap`

Ring-verified post-fix: every entry to `$01:9140` (body) now has `DB=$01` (was `$02` pre-fix).
Pokey `$70` stays active in slot 9 for ~32 sample frames vs ~4 pre-fix. **User confirmed
Pokey no longer falls.**

### Issue F-eat — Yoshi-eats-Pokey removes wrong # of segments (attract demo) — FIXED 2026-05-11

After the falls-through fix, the Yoshi-swallow logic was wrong: 1st bite removed 3 Pokey
segments (5→2), 2nd bite removed 1 (2→1, head only), 3rd+ bites removed nothing — Pokey's
head was stuck forever, Yoshi got hurt walking through it. Expected: 5→4→3→2→1→0 one
segment per bite.

**Root cause:** another wrapper bypass, exact same class. `$02:B81C` is the
`PHB / PHK / PLB / JSR $B7ED / PLB / RTL` wrapper around the `RemovePokeySgmntRt` body at
`$02:B7ED`. Bank 01's Yoshi-swallow code at `$01:F5E0`-ish (SMWDisX bank_01.asm:15824)
does `JSL RemovePokeySegment` → `$02:B81C`. Pre-fix cfg:
- `bank02.cfg`: `name 02b7ed Spr070_Pokey_RemovePokeySegment` — body PC named with the
  wrapper-canonical name.
- `bank01.cfg`: `name 02b81c Spr070_Pokey_RemovePokeySegment …` — cross-bank caller
  routed to the body-named function.
- No `name` for `$02:B81C` in bank02.cfg → wrapper never emitted as its own callable.

Bank 01's JSL resolved to the body at `$02:B7ED`, ran it with `DB=$01`. The body's
`AND.W PokeyUnsetBit,Y` (`$02:B805`) is `AND abs,Y` — read mask from `cpu->DB:0xB824+Y`.
With `DB=$01`, the read landed on `$01:B824+Y` (arbitrary bank-01 ROM bytes) instead of
`$02:B824+Y` (correct table `EF F7 FB FD FE`). Each bite ANDed `$7E:00C2,X` (segment
bitmap) against a random mask byte → wrong segments cleared, eventually mask happened to
be `$FF` for the head's Y-delta → 0 bits cleared → head stuck.

**Fix:**
- `recomp/bank02.cfg`: renamed `name 02b7ed Spr070_Pokey_RemovePokeySegment` →
  `name 02b7ed Spr070_Pokey_RemovePokeySgmntRt` (body now has body-specific name).
- `recomp/bank02.cfg`: added `name 02b81c Spr070_Pokey_RemovePokeySegment sig:uint8(uint8_k,uint8_a)`
  — wrapper correctly declared at its real PC.
- `recomp/bank01.cfg`: kept `name 02b81c Spr070_Pokey_RemovePokeySegment …` as-is
  (now correctly resolves to the wrapper).

Ring-verified: body `$02:B7ED` entries now consistently `DB=$02` (was `$01`), `S=$01F6`
(was `$01F7`, deeper by 2 due to wrapper PHB+PHK). Wrapper code at `$02:B81C` properly
emitted in `src/gen/smw_02_v2.c:94750`. User visual confirmation pending.

**Latent follow-up (not fixed, may be silent):** same wrapper-bypass pattern at
`$01:801A` (UpdateYPosNoGvtyW), `$01:8022` (UpdateXPosNoGvtyW), `$01:8032` (SprSprInteract),
`$01:803A` (SprSpr_MarioSprRts). Their bodies access only mirrored low-RAM (<$2000) so
DB-mismatch is invisible. Suspect them if any future visual bug points there.

**Framework-level cleanup queued:** snesrecomp linter pass to detect `name <pc> <fn>`
directives where the bytes at `<pc>` match the SMW wrapper signature
`8B 4B AB 20 LO HI AB 6B` and `<fn>`'s declared PC ≠ `<HI:LO>`. Would catch all four
extant wrapper bypasses ($802A, $8042/$90B2, $9138, $02:B81C) automatically.

### Issue G — Piranha plant visually malformed

In the attract demo, a Piranha Plant (pipe-spawned) renders with incorrect graphics — malformed
tile layout, wrong palette, or wrong OAM arrangement. The plant appears but looks broken (see
screenshot 2026-05-08).

**Suspected class:** OAM tile-selection or palette assignment for Piranha Plant sprite
(`Spr004_PiranhaPlant` / related handler). Possible sub-causes:
1. Draw-info function (`GetDrawInfo` / `GetSpriteDrawInfo`) reading wrong tile index due to
   register-width mismatch (m/x mode at draw time).
2. WRAM state for the sprite's animation frame or tile pointer diverged upstream.
3. Palette slot assignment wrong (DB or bank register stale at draw time — similar to the
   DB=$C0 class that was fixed for `GetDrawInfo_Bank01`).

**Approach:** func_watch on the Piranha Plant draw function; VRAM write differ
(`cmd_vram_write_diff`) scoped to the sprite OAM address range for that slot; compare tile
index and palette nibble between recomp and oracle at the first frame the plant renders.

---

## Session 2026-04-27 — phi-prealloc fix landed, two regressions deferred

**Branch:** `virtual-hw-timing` (merged to main same session).

**Landed:** snesrecomp `1eeb1d8` + parent `1cce70a` ship the multi-
pred phi-prealloc fix in `_emit_backedge_phi` / label-emission. When
a forward goto targets a multi-pred label that hasn't been emitted
yet, the recompiler now pre-allocates fresh phi vars and the label
adopts them. Closes diagonal-ledge-walk-and-sink (Issue B remaining
contributors), bushes left-half repeating, BG slope tiles, trailing
BG tiles, Mario-jumping sprites.

**Regressions opened — known broken, not blocking:**

1. **Berries render as `?`-blocks.** SMW level berries on bushes
   should display as small round red sprites; with the fix they
   render as orange `?`-blocks. Map16-tile-rendering pipeline.
2. **Yoshi `?`-block doesn't activate.** Mario hits the yoshi-`?`-
   block with shell from below; ROM-correct behavior is to replace
   the tile with a used-block + spawn yoshi-egg sprite; with the
   fix the tile stays `?` and no egg spawns.

**Audit findings (this session, see `_triage/option_a_anchored_diff.py`):**

- **At GameMode=0x07 (level-loaded) anchor:** recomp WRAM matches
  snes9x byte-for-byte on sprite types ($9E-$BF), Map16 long
  pointers ($65-$71), and tile buffer ($1933-$1956). The fix
  produces ROM-correct level data at level-load. Validated via
  Option A's first primitive (logical-frame-anchored WRAM diff).
- **Block-hit chain codegen is byte-identical pre/post fix:**
  `HandleNormalSpriteLevelColl_01944D`, `CheckIfBlockWasHit_Entry3`,
  `SpawnBounceSprite_02887D` — none touched by phi-prealloc.
- **Cascade flows through WRAM state evolution.** At frame 443,
  `CheckIfBlockWasHit_Entry3` is called with the same code in both
  builds, but pre-fix writes `$0005=0x0C` (block hit detected),
  post-fix writes `$0005=0x00` (no hit). Inputs differ because
  upstream functions wrote different WRAM. The cascade root is
  distributed across many functions whose phi-prealloc'd output
  produces slightly different per-frame state evolution.
- **Per-function hand-coding doesn't fix it.** Verbatim-reverting
  `RunPlayerBlockCode_EB77` (the largest reshuffled function in
  bank 00, +94 lines) closed neither bug. The cascade is in
  upstream WRAM-writers, not the block-hit chain itself.
- **Bank-level toggling can't separate the bugs.** Disabling phi-
  prealloc per-bank either reintroduces diagonal-ledge or doesn't
  fix berries+yoshi:
  - bank 0D only → berries+yoshi OK, Mario falls under
  - bank 0D + 02/03/04/01 → same as above
  - bank 0D + 00 → yoshi BREAKS
  - bank 0D + 05 → berries BREAK
  Banks 00 and 05 both need to be enabled for Mario-doesn't-fall
  AND both break yoshi/berries respectively. There's no clean cut.

**Framework regression test status:** `test_attract_demo_invariants_hold`
(`yoshi_spawns_in_demo` at frame 900) FAILS post-fix. The test catches
attract-demo-trajectory yoshi-spawn, which is sensitive to Mario's
exact frame-by-frame position. Interactive gameplay yoshi works fine
in some scenarios. Treat the test failure as demo-trajectory noise,
not a real yoshi-functionality regression — until we have Option A's
deterministic-sync harness to make this comparison meaningful.

**To attack in a fresh session:**

1. Build Option A's full deterministic-sync harness
   (`docs/OPTION_A_DETERMINISTIC_SYNC.md`) — needed to compare
   recomp vs snes9x WRAM at every frame, not just at one anchor.
   Without it we can't isolate the upstream cascade root.
2. Per-function bisect within bank 00 / bank 05 with a smarter
   technique than verbatim-revert (e.g., apply phi-prealloc only
   to specific function classes, or instrument per-function WRAM
   writes to find the first divergent write).
3. Consider whether phi-prealloc itself has an architectural
   issue worth rethinking — the user's suspicion was that the
   mechanism may be too invasive; user-visible state being so
   distributed across the cascade supports this. A different
   mechanism for the diagonal-ledge fix (e.g., explicit per-
   function phi-merge cfg directives instead of automatic
   pre-allocation) might give us the diagonal-ledge close
   without the cascade.

**Update later 2026-04-27 — Option A partial harness shipped:**

Built Half 2 (per-frame full-128KB emu ring at 1500 frames, new
`emu_dump_frame_wram` TCP cmd) and partial Half 1 (per-logical-
frame keying, canonical WRAM zero-fill at snes9x init). Cascade-
finder probe at `_triage/cascade_root.py` runs end-to-end. Run
captured at `_triage/cascade_run_2026_04_27.txt`.

**Cascade triage signal (from `_triage/cascade_run_2026_04_27.txt`):**

At rec_frame=443 (k=243 from anchor — exactly the cascade frame
ISSUES.md cites), 19 NEW divergent bytes appear:
- `$7E:c800` rec=0x52 emu=0x25 (Layer 1 Map16 buffer first byte)
- `$7F:c800` rec=0x01 emu=0x00 (Layer 2 Map16 buffer first byte)
- `$7E:837d-$7E:838d` rec=0x20 emu=0x00 (16-byte run, level tile data)
- `$7E:0099` rec=0x00 emu=0x01

Earlier histogram peaks (k=49 / 52 / 189 / 234) are upstream
contributor candidates worth investigating:
- k=49 (rec_frame=249, ~0.8s into level): 69 new divergent bytes,
  including 20-byte runs at `$7E:1c3e-$7E:1c51` and
  `$7E:1cbe-$7E:1cd1` (sprite OAM tables).
- k=52: 61 new divergent bytes including 16-byte run at
  `$7E:02f0-$7E:02ff`, 8-byte at `$7E:02d8-$7E:02df`.

**Limitation — data is noisy:** both sides reach GameMode=$07 at
different rec_frame values (rec=200, emu=182). The 18-frame
offset means inputs from the demo track aren't truly aligned.
For pure-codegen signal, full Half 1 needs save-state injection
at the anchor on both sides:
- snes9x exposes `retro_serialize / retro_unserialize /
  retro_serialize_size` (`snesrecomp/runner/snes9x-core/libretro/
  libretro.cpp:2099`).
- Recomp has `RtlSaveSnapshot/Load`.
- Bridge work needed: `snes9x_bridge_serialize/unserialize`,
  TCP cmds (`emu_serialize_save/restore`), and a probe entry
  point that captures both at GM=$07, restores both before
  cascade scan, and advances them in lockstep.

Until that lands, the current `_triage/cascade_root.py` is a
useful triage starting point — the divergences it surfaces at
specific frames are real WRAM differences, just contaminated
by timing-offset noise.

**Tools shipped:**
- `_triage/option_a_anchored_diff.py` (Option A primitive)
- `_triage/option_a_anchor_pre_egg.py` (per-event anchor)
- `_triage/probe_05_and_18e2.py` (block-hit byte trace)
- `_triage/list_changed_funcs.py` (lists functions changed by the fix)
- ~10 other triage probes
- `smw.local.ini` mechanism for local config overrides
- `emu_step` concurrency mutex (`emu_oracle_cmds.c`)

---

## Session 2026-04-25/26 — post-koopa-shell-pop attract-demo audit

Branch: `post-koopa-discovery` (both repos). Landed:
- `dispatch-extent-multipass` merged to main (snesrecomp): WIP
  reorder + cross-bank thunk-sig-from-funcs.h fix. Koopa-shell-pop
  closed (user visual-confirmed).

After the koopa fix the attract demo runs further than ever, and
the following new visible issues surface. **Looking for the
underlying recompiler-framework cause(s), not per-issue fixes.**

### Issue A — koopa fails to render on 2nd attract-demo cycle

The first cycle of the attract demo renders the koopa correctly.
On the **second** cycle, the koopa is invisible until Mario stomps
it; the moment of contact, the koopa + shell render and eject
normally. After that the koopa is normal for the rest of the cycle.

**Suspected class:** state-carryover across attract-demo loop. A
sprite slot's render state (OAM tile, palette, draw-enable bit)
isn't being re-initialized when the demo restarts. Possible
underlying causes: a NMI/init pathway whose first-time-only branch
isn't taken on the second run, OR a recompiler-side stale-variable
issue where a function's second invocation reuses a stale local
that should have been reset to a WRAM value.

### Issue B — Mario falls below ground level near a ?-block

When Mario walks toward a specific ?-block, he sinks slightly
below the ground tile (single-pixel-or-two drop, then he's stuck
at the lower Y). Should remain at ground level.

**Suspected class:** collision/ground-detection Y-axis arithmetic.
Likely related to Issue C below — both touch ?-block interactions
on the Y axis.

### Issue C — Yoshi floats into the sky after emerging from a ?-block

When the ?-block spawns Yoshi, Yoshi rises and keeps rising (no
gravity applied, or wrong Y velocity sign). Yoshi should drop and
land normally.

**Suspected class:** Y-velocity initialization or gravity-apply
pathway on sprite-spawn-from-block. Strongly likely same root
cause as Issue B (both: Y-axis state at ?-block interaction
boundary).

### Issue D — random dirt tiles in background-layer slopes

Hilly slopes in the BG layer (Layer 2) show random dirt blocks
scattered across the green slope, breaking up the slope graphic.
ROM has clean slope tiles; recomp injects extra dirt tiles.

**Suspected class:** tilemap upload / Layer-2 BG-tilemap source
selection. Possible underlying causes: a tile-source pointer
loaded with wrong width (M=8 vs M=16) reading from the wrong
half-byte; or a dispatch-table over-read (Tier-1 test still red on
29 sites) routing a Layer-2 tile-fetch to the wrong handler.

### Issue E — berries now positioned correctly (positive)

Berries previously rendered too far up-left on the bushes. They
now snap to the correct positions. **No new fix targeted this** —
it was an inadvertent side-effect of the dispatch-extent / thunk-
sig fix landing on main. Worth tracking because it confirms the
class of bug we just fixed reaches farther than the koopa-shell
case.

### Cross-issue pattern hunting (the actual work)

**UPDATE 2026-04-26:** Issue C investigation traced the visible
"Yoshi-floats-up" bug to the chain:

  1. Dispatch over-decode at $01:FAC3 produces phantom entries.
  2. discover_bank promotes one of them ($01:ECEC) as auto_01_ECEC.
  3. The promotion CAPS Spr035_Yoshi's emit range at $ECEC.
  4. Spr035_Yoshi's body decode reaches PC=$ECED (after a JSR);
     end_addr=$ECEC, so the emit closes with a fall-through call
     to auto_01_ECEC.
  5. auto_01_ECEC runs the on-ground init block `LDA #$F0; STA
     SpriteYSpeed,X` every frame, defeating gravity.

**Fix attempts on 2026-04-26 all regressed visible behavior:**
  * Reject auto-promote inside MANUAL func body — broke koopa
    shell-pop (real handlers like SprStatus06 get rejected too).
  * Reject only when dispatch_only AND inside-MANUAL-reach —
    broke YoshiEgg→Yoshi spawn (BigBoo dispatch handlers at
    $F8F8 are dispatch-only AND inside-reach).
  * Suppress the fall-through emit when next_func is dispatch-
    only — broke Yoshi-egg spawn entirely (the egg sprite
    handler vanished).

**All three reverted to baseline state.** The dispatch-overread
class is genuinely subtle: phantom promotions and real body-
internal sub-handlers are not distinguishable by any single
heuristic tried so far. **Future attempts MUST run the
attract-demo regression test** (`test_attract_demo_regression`)
which encodes the visible-behavior invariants we know are
correct. Whack-a-mole regression debt is the cost of NOT
running that test between attempts.

Status as of commit:
  - Issue A (koopa invisible on 2nd attract cycle): OPEN
  - Issue B (Mario falls below ground at ?-block): OPEN
  - Issue C (Yoshi floats up after ?-block hatch): OPEN
  - Issue D (BG slope dirt blocks): OPEN
  - Koopa-stomp shell-pop: WORKING (regression-tested via
    test_attract_demo_regression invariants)
  - Yoshi-egg → Yoshi spawn: WORKING (regression-tested)
  - Demo-progresses-past-boot: WORKING (regression-tested)

Do NOT add per-game cfg entries (exclude_range, jsl_dispatch counts)
to mask any of A–D unless framework fixes are blocked.

---

# Historical: Open issues + session summary from autonomous rip session 2026-04-19/20

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

## Framework gap: `uint8 k` signature sticks even when callee doesn't read X

Discovered 2026-04-20 while investigating ground-bug's 5× VRAM-write
undercount (frame 95, BG1 tilemap $V2800-$V2FFF: 179 recomp / 909 oracle).

**Symptom.** `src/gen/smw_05_gen.c:212-213` emits:
```c
BufferScrollingTiles_Layer1_Init(0 /* RECOMP_WARN: X unknown at call site */);
BufferScrollingTiles_Layer2_Init(0 /* RECOMP_WARN: X unknown at call site */);
```
inside the 32-iter loop in `InitializeLevelLayer1And2Tilemaps`
($05:809E). The call sits after a loop back-edge; `_build_call_args`'s
in-BB X tracker (`self.X`) is None, so it emits `0` with WARN.

**Why the sig says `uint8 k` at all.** All 6 dispatched Buffer*
functions (Layer1, Layer1_NoScroll, Layer1_VerticalLevel, Layer2,
Layer2_Background, Layer2_VerticalLevel) carry `(uint8 k)` in
`recomp/funcs.h`, yet grep shows **exactly 1 `\bk\b` match per
function body** — the declaration line. `k` is never read. The sig
is wrong.

**Root.** `_augment_sig_with_livein` in `recomp.py:989-1039` is
deliberately one-way: "This pass only WIDENS: it never drops a param
that's already in the sig, even when live-in says the register isn't
consumed." Rationale in the docstring: live-in analysis is
conservative, has known gaps (PHX…PLX scribble-restore, DP-indirect
reads), and hand-written callers codify the true ABI. Dropping a sig
param could break them.

Consequence: once `(uint8 k)` got introduced at any point in history
(probably via a tail-call propagating `reads X` upward in
`infer_live_in_regs` at lines 709-715), the sig is pinned forever,
and every caller that can't resolve X at the call site emits a
WARN + wrong-looking `0` argument.

**Why this case is harmless.** In these 6 callees, `k` is dead, so
passing 0 is semantically equivalent to passing the real X. The WARN
is cosmetic clutter. But the framework invariant is fragile: the next
time a dispatch target DOES read X, the same caller-side pattern
would silently miscompile.

**What a fix looks like (scoped, not done in this session):**

1. **Sig narrowing** (most direct): teach the widening pass to also
   narrow when liveness *definitively* says the register isn't live-in
   AND the body contains no PHX…PLX scribble-restore pattern AND no
   DP-indirect read of `$7E:XX` where X is used as index. Guarded
   behind a cfg opt-in for rollout safety. Requires regen of every
   bank + regression eyeball.
2. **Unused-param elimination at emit time**: during function emit,
   scan generated body for `\bk\b`; if absent, rewrite sig to drop
   `uint8 k` post-emit and propagate to the sync_funcs_h writer.
   Then any caller re-resolves against the narrower sig. Smaller
   blast radius than Option 1.
3. **Cross-BB X tracking in the caller tracker**: carry `self.X`
   through loop back-edges via join/fixpoint. Even with this, a
   TAX-from-runtime-value pattern (as here) would still give "X
   unknown at call site" → falls back to Option 2 anyway.

All three are non-trivial. Design review recommended before
implementation.

**Until then:** `RECOMP_WARN: X unknown at call site` in generated
output is acceptable only when an independent check confirms the
callee doesn't read `k`. Do not claim the WARN is the root cause of
a runtime divergence without that check (ground-bug 2026-04-20:
confirmed dead, NOT the cause).



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

## Tier 3b kPatchedCarrys_SMW — CLOSED 2026-04-20

Resolved by verifying the 46 patch addresses were already dead for
runtime. Cross-check: `tools/check_patch_carrys.py` confirmed every
entry lives in a recompiled bank (00-04) and outside every
`exclude_range` in those cfgs. Recompiled paths thread C explicitly
in generated code, so the interpreter never saw these sites — the
BRK patches were masking nothing.

Ripped: the 46-entry array, `patch_carrys`/`patch_carrys_count` on
`RtlGameInfo`, `kPatchedCarrysOrg[]` buffer, `FixupCarry()`, the
init-time ROM-byte patcher, the `CpuOpcodeHook` carry loop, and the
ADC/SBC carry-set switch cases in `cpu.c`'s BRK handler. Framework
no longer needs a reaching-defs carry inference pass for this — the
premise (interpreter executing ADC/SBC with unset carry) was stale.

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

---

## Session 2026-05-14 — first-koopa-boss platform doesn't spawn, game freezes

**Context:** Reported by user during play-testing the post-v0.3.0 build
(playable milestone with overworld navigable + save persistence). User
reached the first koopa boss fight in world 1 (Iggy's castle); the
boss's platform never spawned and the game froze on entering the
fight.

**Symptom:** Boss fight room loads, boss arena visible, but the
platform that the koopa stands on / Mario lands on doesn't appear.
Game becomes unresponsive (no input progression, may still NMI but
nothing playable).

**Likely class:** sprite spawn failure for boss-specific extended
object or boss-fight gimmick. Could share root with the historical
ParseLevelSpriteList branch-emit-as-return bug (closed 2026-04-21,
re-verified obsolete 2026-05-14 via D1 cleanup test). Could be a
distinct sprite or extended-object class. Could also be an
Iggy-specific dispatch table not yet covered by indirect_call_table
authorization.

**Reproduction:** From v0.3.0 build, navigate Yoshi's Island 1 →
Switch Palace → Donut Plains 1 / equivalent path through to Iggy's
castle. (Or use a savestate just before the boss room.)

**Probes to run when investigation begins:**
- Watch boss-room sprite spawn list (`ParseLevelSpriteList_Entry2`
  invocations + sprite slot fill rate)
- Check ExtObj table for boss-platform handler
- Compare oracle vs recomp at boss-room entry frame for first
  divergence (VRAM, OAM, sprite RAM)
- Check stack/phantom-PC traps for fires during boss-room load
- Check if the freeze is a true CPU hang vs main-loop stuck in
  spinwait — watchdog should catch the former

**Why deferred:** cleanup-pass priority during chore/cleanup branch.
Re-prioritize after cleanup work completes. Capture a savestate just
before the boss room as starting probe state.

**Tools needed:** none beyond the existing always-on rings. F1–F4
keyboard savestates work in this build (verified path:
src/main.c:1015 RtlSaveLoad).
