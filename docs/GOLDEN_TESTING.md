# Golden-Oracle Debugging for Static Recompilers

A platform-agnostic methodology for finding and fixing codegen bugs in
statically-recompiled game binaries by pairing the recompiled runtime
with an embedded cycle-accurate emulator as an oracle. Documented from
the 2026-04-23 Bug #8 investigation in the SMW recomp project, but
intended for direct reuse in any recomp: NES (nesrecomp), SNES (this
project), Genesis (segagenesisrecomp), PSX, Xbox HLE, etc.

## The problem this solves

A static recompiler translates ROM bytes into native C. When the
translation diverges from the ROM's intended behavior, the visible
symptom is usually something small and late — a character renders one
tile off, a sound cue skips, a cutscene stalls — but the ROOT is
typically a single instruction-level codegen gap buried under thousands
of frames of correct execution.

The hard part is the *distance* between symptom and root:
- **Game frames:** symptom shows up on frame 400; bug fired on frame 95
- **Function depth:** visible byte is in player state at DP $72; the
  chain that sets it is 60 calls deep
- **Abstraction layers:** symptom is rendering; root is register
  tracking in the codegen pass

Without the right observability, you end up guessing which subsystem
to blame (codegen? runtime? emulator timing? ROM data?) and chasing
symptoms.

## The core idea

**Run the recomp alongside a cycle-accurate emulator, in the same
binary.** When the recomp's state diverges from the emulator's, you
can drill down to the exact instruction that caused it.

This is not novel — it's exactly what every recomp project should do.
What this doc captures is *the layered observability you need on top*
to make the drill-down fast enough to close real bugs in a session.

## The tool stack (platform-agnostic)

Each tool answers one specific question. Build them in this order; do
not skip ahead.

### Layer 0 — Oracle embedding

Embed an authoritative emulator inside the recomp binary, gated on a
build flag so the production build stays byte-identical. Give it:

- `emu_step N` — advance emulator N frames without touching recomp
- `emu_read_wram <addr> <n>` — read emulator WRAM
- `emu_read_vram` / `emu_read_palette` / etc. — read other RAM regions
- `emu_cpu_regs` — read current A, X, Y, S, P, PC, etc.
- `find_first_divergence <subsystem> <lo> <hi> <context>` — byte-diff
  recomp's state against emulator's at the current frame; return the
  first offset that differs with a context window

This is the floor. Without it, you have no ground truth for "what
SHOULD the byte be" — you're guessing from hand-written HLE or
hand-decomp, both of which have their own interpretation errors.

**Sibling precedents:** segagenesisrecomp embeds clownmdemu; nesrecomp
embeds Nestopia; SMW recomp embeds snes9x.

### Layer 1 — WRAM write trace on BOTH sides

On recomp: every `store_to_wram(addr, old, new)` goes through a hook
that records `(frame, addr, old, new, current_function, caller)`.
Controlled via `trace_wram <lo> <hi>` and `get_wram_trace`.

On emulator: install a bus-write hook (snes9x's `s9x_write_hook`,
clownmdemu's analog, etc.) that records the same plus `(PC24,
bank_source)`. Controlled via `emu_wram_trace_add <lo> <hi>` and
`emu_get_wram_trace`.

**What it answers:** "Which specific instruction wrote this byte on
each side, and what value did it write?"

Without the emulator-side PC trace you know `$XX is wrong` but not
`$XX was supposed to be written by STA at ROM $ABCDEF`.

### Layer 1.5 — Recomp call trace

Every RecompStackPush appends `(frame, depth, function, parent)` to a
ring. Controlled via `trace_calls` / `get_call_trace
contains=<substr> from=<f> to=<f>`.

**What it answers:** "Did the recomp execute function X during this
frame? If so, what called it?"

Critical caveat: this is bounded-output — the JSON buffer truncates.
**Treat any 0-hit result as inconclusive until you've verified with
multiple frame windows via the `from=/to=` filter.** Our first pass
wrongly concluded `GameMode04_PrepareTitleScreen` was never called; a
second pass with frame windows proved it fired at f95.

### Layer 2 — Block trace (basic-block boundaries)

At every basic-block entry, emit a hook that records
`(frame, depth, pc, A, X, Y, current_function)`.

**What it answers:** "What was the exact sequence of basic blocks that
executed? What were the register values at each block entry?"

Filter at query time with `pc_lo=<hex> pc_hi=<hex>` to see only the
region of interest. The ring fills fast — expect ~100K block hooks per
gameplay frame — so arm the trace *immediately* before the target
frame and query after that single frame's execution.

### Layer 2.5 — Watchpoints and breakpoints

`watch_add <addr> [match_val]` — pause the recomp synchronously when
a specific address is written (optionally with a specific value).
`break_add <pc>` — pause when a specific ROM PC is entered.

The pause must capture state *at the moment of the hit*:
- the writer's function name (from `g_last_recomp_func`)
- the **full call stack snapshot** (not just one level)
- the PC, the written value

**Critical discovery: `g_last_recomp_func` is unreliable alone.** When
tail-call dispatchers push-without-pop, the stack leaks and the "last
func" global shows a stale caller, not the actual writer. You MUST
snapshot the full `g_recomp_stack[]` array at hit time and let the
client decide which level to trust.

Our investigation found this the hard way: the parked watch on $0100
reported `writer=LoadSublevel_02A751` but the full-stack snapshot
showed 39 leaked entries, and the real writer was
`GameMode11_LoadSublevel_0096D5` at a different stack depth.

### Layer 3 — Time-travel WRAM inspection

Periodic full-WRAM snapshots tied to block indices, plus per-block
delta logs. Query `wram_at_block <block_idx> <addr> <n>` to read WRAM
as it was at an earlier moment. Lets you answer "what was $XX right
before this call happened?" without re-running the boot sequence.

Not load-bearing for Bug #8, but essential when the cascade spans
many frames and you want to pin the *first* time $XX diverges.

### Layer 4 — Per-instruction trace

Every ROM instruction logs `(frame, block_idx, pc, opcode, A, X, Y,
B, m-flag, x-flag)`. Both on recomp (via the generator's insn hook)
and on the emulator (via its own dispatcher).

Controlled via `trace_insn` / `get_insn_trace pc_lo=<hex> pc_hi=<hex>
limit=<n>` on each side.

**What it answers:** "At this exact ROM PC on both sides, what are
the register values? Which earlier PC caused them to diverge?"

This is the finest granularity; capture rings are large (1M+
entries, ~24MB for the emulator). Arm them only when you know the
frame window — a single gameplay frame can burn ~30K insns. Use PC
filters at query time to see just the region you care about.

## The methodology

### 1. Reproduce as a structured test

**Write a golden-fixture test *before* debugging.** Capture the
oracle's expected state at named checkpoints into a JSON file, then
run the recomp and assert on matches.

**Sync the checkpoints on state, not frame number.** Cycle-accurate
emulators advance per-cycle, while recomps advance per-C-call, so
their frame counters diverge by hundreds within seconds of boot.
Define each checkpoint as `wait_for_<state>=<val>, dwell_frames=N`
and have each side independently advance until its own state matches,
then step N more. Both sides end up at "frame N of the attract-demo
script after entering mode X", regardless of how many wall-clock
frames each side needed to get there.

The fixture is the golden baseline; divergences in the recomp-vs-oracle
diff are the bug.

### 2. Find the first-divergence BYTE (not symptom)

`find_first_divergence wram 0 0x1FFFF` at the earliest sync point.
Filter noise (DP scratch, boot-timing-dependent regions) by narrowing
the range. Example: we narrowed from full 128K WRAM to `$0070-$009F`
(player scratch) to get a clean 6-byte divergence table at dwell=0,
which immediately flagged `$72 PlayerInAir` as the seed.

**Do NOT** look at high-level symptoms ("Mario is one tile low") and
reason about possible causes. Do look at the BYTE that first differs
and trace it back.

### 3. Find the first-divergence WRITE

Arm Layer 1 write trace on the seed address on both sides. Run to the
sync point. Compare write streams.

The first write where recomp wrote a different value than oracle
(or where one side wrote and the other didn't) is the root-cause
*moment*. The attribution identifies the root-cause *function*.

### 4. Verify attribution

**Never trust `g_last_recomp_func` alone.** Use Layer 2.5's full-stack
snapshot, or cross-check against Layer 1.5's call trace filtered to
the same frame. Stack leaks from tail-call dispatchers will silently
point at stale callers.

### 5. Find the first-divergence INSTRUCTION

Once you know the function, use Layer 4 insn trace on both sides.
Filter to the function's PC range. Align by PC. The first PC where A,
X, or Y differs is where the codegen gap lives.

This turned what could have been a manual walkthrough of ~850 lines
of generated C into a single-line finding: "at PC $EBB2, recomp has
X=0, oracle has X=2."

### 6. Fix the generator, not the output

The codegen gap is ALWAYS in the recompiler (`recomp.py` or
equivalent). Never hand-edit generated code — the next regen will
overwrite it, or worse, silently fail to overwrite and leave the
cascade latent.

If the gap applies to one function, it will apply to any future
function with the same shape (across every game you'll recompile).
Fix the framework once, update per-game cfg annotations minimally.

### 7. Hand the exe to the user for visual verification

Buffer-match is necessary but not sufficient. Internal state can
match at checkpoint N but visual rendering can still be wrong, or
vice versa. After any fix that buffer-matches, launch the exe and
ask the user to watch the actual behavior. Only close the bug after
visual confirmation.

## Worked example: SMW Bug #8 ("Mario one tile too low")

### Symptom
Attract-demo Mario visually sinks into the ground — renders 16 pixels
(one map-tile) below the expected position.

### Investigation (6 hours)

| Step | Tool | Question | Answer |
|------|------|----------|--------|
| 1 | Golden test v2 | Where does state first diverge? | 46 bytes differ by dwell_30 |
| 2 | State-sync diff | What's the SEED byte? | `$0072 PlayerInAir`: recomp=0x24, oracle=0x00 at mode-0x07 entry |
| 3 | Write trace (both) | Which instruction cleared $72 on oracle? | `STZ $72` at ROM `$EF6B`, oracle-frame 296 |
| 4 | Call trace | Did recomp run the clearing function? | 0 hits — recomp never executes `RunPlayerBlockCode_00EEE1` during boot |
| 5 | Full-stack watch | Who really writes $100 at f94? | Stack had 39 leaked entries — `g_last_recomp_func` was stale |
| 6 | Block trace | Exact path recomp takes through `EB77`? | `EB77→EB83→...→ED4A→ED4F→EDDB→EE1D` (wrong branch) |
| 7 | Insn trace (both) | Where does X first diverge in that path? | PC `$EBB2` (post-JSR `F44D`): oracle X=2, recomp X=0 |
| 8 | ROM + generator read | Why? | `F44D`'s cfg sig is `RetAY` — X modification lost |
| 9 | Framework fix | Add `RetAXY` return type | types.h + recomp.py |
| 10 | Regen + rebuild | Test? | v2 golden: 46→35 divergences, PlayerYPosNext matches oracle |
| 11 | Visual | User confirms? | "Mario is where he's supposed to be!" ✓ |

Each step was one specific tool query. None required reading more
than a few hundred lines of code.

### What made it tractable

- **State-sync in the golden test** — boot-timing delta (recomp 201
  frames to mode 0x07, oracle 405 frames) would have drowned any
  frame-number-based comparison.
- **Pairing recomp's Tier-1 write trace with emulator's PC-attributed
  write trace** — knowing "$72 was cleared by STZ at $EF6B" on the
  oracle side gave us the exact instruction to look for on recomp.
- **Full-stack snapshot at watch-hit** — without it, the stale
  `g_last_recomp_func` attribution would have sent us down a
  multi-hour wrong path chasing `LoadSublevel_02A751`.
- **Paired insn traces on both sides** — reduced 850 lines of
  generated C down to a one-line diff: "X is 2 here on oracle, 0 on
  recomp."

## Worked example 2: SMW frozen koopa

### Symptom
Attract-demo koopa spawns but doesn't move. Mario walks past a statue
instead of a walking enemy.

### Investigation (1 hour — same recipe as Bug #8, 6x faster)

| Step | Tool | Question | Answer |
|------|------|----------|--------|
| 1 | Sprite-table dwell diff | Which sprite byte first diverges? | Recomp puts koopa in slot 0 XSpeed=0, oracle in slot 9 XSpeed=0xf8 |
| 2 | SpriteStatus write trace | Which function decided the slot? | Both sides use `ParseLevelSpriteList_Entry2` — different X at `STA SpriteStatus,X` |
| 3 | SpriteMemorySetting check | Slot-loop entry state matches? | Yes, both sides should start X=9 |
| 4 | Tier-4 insn trace | Where does X first diverge? | At `$02A91B` X=9, at `$02A93C` X=0 — X lost across BEQ |
| 5 | Gen'd C read | Why? | `label_a93c` has two predecessor branches; the second (X=v34) overwrote the first's X_var (X=v23). The first BEQ's X value was silently lost at runtime when that path fired |
| 6 | Framework fix | Add phi-merge for X/Y at multi-predecessor labels (mirror of existing A-register logic) | `recomp.py` |
| 7 | Regen + rebuild + visual | Confirm? | User: "koopa responds now" (spawned, walks — separate fall-through-map bug remains) |

### What made it 6x faster than Bug #8

The tool stack was already in place (all Tiers 1-4, oracle embed,
state-sync golden, parked stack). No new tooling needed. Each step was
one probe (~10 minutes), not a tool-buildout.

### What it reinforced

**Asymmetry in the framework is a code smell.** When Bug #8 pinpointed
`RetAY` as missing X-return, the methodology's answer was "add `RetAXY`
to fill the gap." When frozen-koopa pinpointed "second branch overwrites
first's X_var at label", the fix was "the A-register phi-merge already
existed at line 4948-4952; mirror it for X and Y." Both fixes pattern-
matched against EXISTING asymmetries where A was handled but X/Y
weren't. Before writing new machinery, check: does the framework
already handle this for a different register/flag/slot? If yes,
mirror it.

**Different register tracking bugs can produce the same symptom class.**
Bug #8 was X-loss across CALLS (sig didn't propagate). Frozen-koopa was
X-loss across BRANCH JOINS (second predecessor overwrote first's phi
var). Both produced "wrong X at the next instruction, downstream writes
go to the wrong slot." The methodology pins which specific mechanism
via the insn-level diff; generic "reason about codegen" wouldn't have.

## Anti-patterns

### Don't sync by frame number

Boot timings diverge between recomp and emulator. A test that asserts
"at frame 200, player Y = 0x0150" will fail on both sides' valid
states. Sync on gameplay state (GameMode, HP, level loaded, etc.).

### Don't printf-debug the generated C

The generated C is output. Treat it like compiler output in any other
language: read it, don't edit it. If you need observability, add a
hook to the generator so the next regen emits the hook for every
similar site.

### Don't trust `g_last_recomp_func`

It's a single global that lags behind actual execution under tail-call
patterns. Snapshot the full call stack at any pause point.

### Don't search for "what's similar in this function"

When you find a buggy function, the temptation is to hand-patch just
that function. Resist it. The codegen gap that produced the bug will
produce the same bug in the next function with the same shape. Add
the fix to the generator and regen.

### Don't claim visual fix from buffer-match alone

Internal state matching is necessary, not sufficient. Rendering is a
downstream pipeline (NMI timing, DMA, PPU state) that can diverge
silently even when CPU state matches. Hand the exe to the user.

## Checklist for reproducing this pattern in a new recomp project

- [ ] Embed a cycle-accurate emulator as the oracle (snes9x, Nestopia,
      clownmdemu, etc.) behind a build flag
- [ ] Expose `emu_step`, `emu_read_<region>`, `emu_cpu_regs`,
      `find_first_divergence` over TCP
- [ ] Add `trace_wram` with old/new capture on the recomp side
- [ ] Add `emu_wram_trace_add` with PC attribution on the emu side
- [ ] Add `trace_calls` with contains/from/to filters
- [ ] Add `trace_blocks` with pc_lo/pc_hi filters
- [ ] Add `watch_add` / `break_add` with full-stack snapshot
- [ ] Add per-insn trace on both sides (Tier 4)
- [ ] Write a state-synced golden fixture for your attract/demo mode
- [ ] Write a test that asserts recomp WRAM matches golden at each
      checkpoint; exits non-zero on divergence
- [ ] Establish a rule that codegen bugs never get hand-patched —
      the fix goes in the recompiler
- [ ] Establish a rule that visual bugs get handed to the user after
      a buffer-match fix, before closing

Once the floor is in place, any future codegen bug in the project
follows the same 7-step recipe: state-sync diff → seed byte → write
trace → call trace / full stack → block trace → insn trace → diff.
Each step takes minutes, not hours.

## Measured scaling (2026-04-23 session)

Two codegen bugs fixed back-to-back using this recipe:

- **Bug #8** (Mario one tile under): 6 hours. Tool-build work interleaved
  with investigation — added `parked`-stack snapshot, extended Tier-4
  insn trace usage, wrote 20 probes. Framework fix: `RetAXY` return
  type. Codegen gap: X-loss across function returns.
- **Frozen koopa**: 1 hour. Zero new tooling. 4 probes. Framework fix:
  phi-merge X/Y at multi-predecessor branch joins. Codegen gap:
  X-loss across goto-into-shared-label.

Ratio is the expected shape: first bug pays for the tool-stack
buildout; subsequent bugs exercise the already-built tools. Plan
accordingly when estimating "how long until this recomp is done."
The first 3-5 bugs are tool-expensive; bugs N+5 drop to the 1-hour
range.

## When to reach for this methodology vs. alternatives

| Symptom | This methodology | Alternative |
|---|---|---|
| Recomp renders wrong | YES — find the byte, then the write, then the instruction | — |
| Recomp crashes in a specific function | YES — the byte that got corrupted IS the seed | — |
| Recomp emits wrong warning at compile time | No — this is a decoder issue, not runtime; read the ROM + gen'd output directly | Decoder unit tests |
| Framework test fails after a recomp change | No — the test tells you which file/line; check the diff | git bisect + tests |
| Emulator oracle isn't matching real hardware | No — that's an emulator bug, upstream to the emulator project | — |
| PPU / rendering subsystem fails | This works, but the SEED byte is in VRAM/OAM/CGRAM, not WRAM. Adjust `find_first_divergence` subsystem arg | Frame capture + visual diff |

The methodology is specifically for **recomp-vs-oracle behavioral
divergence**. Not every bug is that class — but most visible ones are.

## References

- `CLAUDE.md` — project-level rules (no stdout debugging, no
  hand-edited generated code, rule-0 framework-fix-first)
- `DEBUG.md` — structured debug protocol (first-divergence discipline,
  timeseries requirement, full-state requirement)
- `REVERSE_DEBUGGER.md` — the tier layout for observability tools
- `BUG8_INVESTIGATION_TOOLING.md` — the specific Bug #8 worked example
- `snesrecomp/tests/l3/_probe_bug8_*.py` — the 20+ probes used to
  narrow Bug #8, useful as templates for future investigations
