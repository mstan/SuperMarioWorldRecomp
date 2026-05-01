# Handoff: VRAM write-trace differ + upstream attribution

Build the missing tooling so "first divergent VRAM write" is a one-shot
TCP query, then close the Layer-3/HUD corruption bug with it. Today we
can attribute who wrote a VRAM byte on the recomp side, but we cannot
compare the *sequence* of writes against the snes9x oracle; investigation
falls back to hand-reading SMWDisX. This adds the symmetric oracle log
plus a sequence differ so the answer comes mechanically.

## Why this, why now

Probe `_triage/probe_layer3_vram_writers.py` (last session) found
`ClearLayer3Tilemap_M1X1` writes 2300 entries of `$00FC` into word
`$5200-$5BFF` at boot via
`InitAndMainLoop_ProcessGameMode → GameMode00_LoadNintendoPresents →
ClearLayer3Tilemap`. Single function, single fill value. Open question:
**is `$00FC` what the oracle writes too?**

Without this tooling, answering it requires either:
- Reading SMWDisX bank_00.asm by hand to confirm the fill constant, or
- Hand-coding a per-symptom byte-diff at a state-synced anchor.

With this tooling, `vram_write_diff $A400 $B800` returns the first
mismatched (addr, value) pair across recomp + oracle write streams, with
recomp-side function + stack attribution baked in. Same pattern works
for every future VRAM bug (Layer 1, OBJ tiles, palette, CGRAM…).

## What's already shipped (do not redo)

- `snesrecomp 5e342b8` — Always-on `s_vram_trace` full-range
  (`runner/src/debug_server.c:4264-4274`). Captures every word-VRAM
  write with frame, addr, val, `last_recomp_func`, and stack snapshot.
- `snesrecomp 64ea0b4` — `post_mortem_dump` TCP cmd.
- `SuperMarioWorldRecomp e54c4b8` — `src/post_mortem.{c,h}` writes
  `build/last_run_report.json` on SEH / signal / atexit / on-demand.
- Probe at `snesrecomp/_triage/probe_layer3_vram_writers.py` — uses
  the always-on ring; pause-then-query (NOT arm-then-attach).

Memory entries with full context: see
`feedback_dry_when_pattern_repeats.md` (this is the same shape of
gap — DRY the differ instead of hand-investigating each VRAM bug),
`feedback_never_time_always_ring_buffer.md` (rings only — both sides
must be always-on), and `feedback_state_delta_is_not_symptom.md`
(byte-diff alone is necessary not sufficient; tie to a visible
symptom before claiming a fix).

## Task A — Oracle-side VRAM write ring (the killer feature)

**File:** `snesrecomp/runner/snes9x-core/ppu.h`

Six write sites that store into `Memory.VRAM[address]`:

| Function | line | byte addr basis |
|---|---|---|
| `REGISTER_2118` | 365, 368 | `Memory.VRAM[address] = Byte` |
| `REGISTER_2118_tile` | 399 | same |
| `REGISTER_2118_linear` | 423 | same |
| `REGISTER_2119` | (find via grep) | high byte at `address|1` |
| `REGISTER_2119_tile` | (find) | same |
| `REGISTER_2119_linear` | (find) | same |

After each `Memory.VRAM[address] = Byte;` (or the `address|1` variant
for `_2119_*`), call:

```cpp
extern "C" void s9x_oracle_on_vram_write(uint16_t byte_addr, uint8_t value);
s9x_oracle_on_vram_write((uint16_t)address, Byte);
```

**Implementation (recomp side):** add a parallel ring next to
`s_vram_trace` in `snesrecomp/runner/src/debug_server.c:253-268`.
Same shape, but byte-addressed (snes9x stores bytes, not words):

```c
#define ORACLE_VRAM_TRACE_LOG_SIZE 65536
static struct {
    int active;
    uint64_t write_idx;
    uint64_t count;
    struct { int frame; uint16_t adr_byte; uint8_t val; uint8_t pad; }
        log[ORACLE_VRAM_TRACE_LOG_SIZE];
} s_oracle_vram_trace = {0};

void s9x_oracle_on_vram_write(uint16_t byte_addr, uint8_t value) {
    if (!s_oracle_vram_trace.active) return;
    int idx = s_oracle_vram_trace.write_idx % ORACLE_VRAM_TRACE_LOG_SIZE;
    s_oracle_vram_trace.log[idx].frame = snes_frame_counter;
    s_oracle_vram_trace.log[idx].adr_byte = byte_addr;
    s_oracle_vram_trace.log[idx].val = value;
    s_oracle_vram_trace.write_idx++;
    if (s_oracle_vram_trace.count < ORACLE_VRAM_TRACE_LOG_SIZE)
        s_oracle_vram_trace.count++;
}
```

Arm always-on at `debug_server_init` next to the recomp side
(`debug_server.c:4264`). Gate on `SNESRECOMP_REVERSE_DEBUG` AND
`ENABLE_ORACLE_BACKEND` so Release|x64 stays clean.

No function-attribution on the oracle side — we don't need it. The
oracle is the *reference*; we trust its writes are correct. We only
need the **sequence** so the differ has something to compare against.

**Cost:** ~6 bytes/entry × 65536 ≈ 0.4 MB. Negligible.

**Acceptance:** boot the Oracle build, run 30 frames, query a
`get_oracle_vram_trace` cmd (you'll need to add it — mirror
`cmd_get_vram_trace` at `debug_server.c:1731`). Expect entries with
the same shape and roughly same count as the recomp side. Sanity-check:
both sides should write to the same byte-address pages in the same
order, modulo per-side ordering nuances.

## Task B — Sequence differ TCP command

**File:** `snesrecomp/runner/src/debug_server.c`

Add `cmd_vram_write_diff(args)` that takes a byte-address range and
walks both rings forward from index 0 (or from each ring's oldest
in-window entry), looking for the first mismatched `(addr, val)` pair
*restricted to the requested range*. Returns:

```json
{
  "ok": true,
  "first_diff_idx": 4218,
  "recomp": {"frame": 1, "adr_byte": "0xA400", "val": "0xFA",
             "func": "ClearLayer3Tilemap_M1X1",
             "stack": ["...","..."]},
  "oracle": {"frame": 1, "adr_byte": "0xA400", "val": "0x02"},
  "matched_pairs_before": 4218
}
```

If both streams agree across the entire requested range, return
`{"ok": true, "matched_pairs": N, "diverged": false}`.

**Subtle:** the recomp ring is word-addressed and stores 16-bit values
(see `s_vram_trace.log[].adr` and `.val`); the oracle ring is
byte-addressed and stores 8-bit. Normalize during the diff: each
recomp word entry produces TWO byte-pairs `(addr*2, val&0xFF)` and
`(addr*2+1, val>>8)`. Or store both rings in byte coords from the
start (cleaner — refactor the recomp side to byte-addressed too).
**Pick byte-addressed everywhere; word coords have caused
sign-of-sign confusion in this project before** (the user's earlier
"$5200-$5FFF" was byte coords, my probe assumed word, lost one
session to it).

**Acceptance:** `vram_write_diff 0xA400 0xB800` against today's boot.
Either:
- Returns `diverged: false` → ClearLayer3Tilemap is correct, the bug
  lives in a *later* upload routine that overwrites the clear.
- Returns a first-diff record → the cause is at or before the named
  recomp function on the named call stack.

Either outcome closes the open question. **This is the gate for the
session being "complete."**

## Task C — Per-write CpuState in `s_vram_trace`

**File:** `snesrecomp/runner/src/debug_server.c:260-267` (the entry
struct).

Extend each ring entry with `A, X, Y, P, D, DB` (12 bytes):

```c
struct {
    int frame;
    uint16_t adr;
    uint16_t val;
    uint16_t A, X, Y, D;
    uint8_t  P, DB, m_flag, x_flag;
    char func[64];
    const char *stack[TRACE_STACK_DEPTH];
    int stack_depth;
} log[VRAM_TRACE_LOG_SIZE];
```

Capture inside `debug_server_on_vram_write` (line 270): pull from
`extern struct CpuState g_cpu;`. Already declared at `common_rtl.c`.

**Why:** when the differ surfaces a divergent write, the raw "wrote
$FA instead of $02" question becomes "what was X at write-time?" If
X was off-by-8, the bug is the caller's loop counter; if X was 0 the
bug is a width-slip in the addr calc; etc. This is the difference
between "bug is somewhere upstream" and "bug is a width-mask in
function F at call-site G."

**Cost:** ~12 bytes × 65536 ≈ 0.8 MB.

**Acceptance:** existing probes still parse; new fields appear in
`get_vram_trace` JSON output.

## Task D — Reverse-search command

**File:** `snesrecomp/runner/src/debug_server.c`

Add `cmd_last_vram_write_to(args)`. Args: `<byte_addr_hex>`. Walks the
recomp ring **backward from write_idx**, returns the most-recent entry
that hit `byte_addr` (either as `addr*2` or `addr*2+1`):

```json
{"ok": true, "found": true, "frame": 1, "adr_byte": "0xA400",
 "val": "0xFA", "func": "ClearLayer3Tilemap_M1X1",
 "stack": ["...", "..."],
 "A": "0x1234", "X": "0x5678", "Y": "0x..." /* with Task C */}
```

If no entry in the ring hits `byte_addr`, return
`{"ok": true, "found": false, "ring_depth": N}`.

**Why:** today's "fetch entire ring; filter in Python" pattern hits
the JSON-buffer cap (~520 KB → ~7400 entries returned). For "what was
the last write to $A400" the ring may have rotated past it but the
answer is one ring walk on the server side. This avoids the cap
entirely.

**Acceptance:** `last_vram_write_to 0xA400` returns
`ClearLayer3Tilemap_M1X1` against today's boot. Smoke-test with an
address known to NOT have writes (e.g. `0xFEFE`); expect
`found: false`.

## Recommended order

1. **Task A first** (oracle ring) — biggest unknown, highest risk of
   discovering snes9x architecture surprises (e.g. DMA bypassing
   REGISTER_2118 entirely; double-counting from CHECK_INBLANK
   forced-blank paths). Build it and validate write counts before
   building anything that depends on it.
2. **Task B** (differ) — depends on A.
3. **Task C** (CpuState in entry) — independent, do whenever; lands
   easily once the entry struct grows.
4. **Task D** (reverse-search) — small; nice-to-have polish.

**One commit per task** in snesrecomp; each commit message names the
piece and links back to this doc.

## Final acceptance — answers the open question

After all four tasks land, run:

```
> python snesrecomp/_triage/probe_layer3_vram_writers.py 4377
```

…replaced by a new probe that calls `vram_write_diff 0xA400 0xB800`.

Expected: ONE of two outcomes, both of which CLOSE the question:

- **Match.** ClearLayer3Tilemap's $00FC fill is correct. The HUD/Layer-3
  corruption (Image #14, #15) lives in a downstream upload that
  overwrites the clear. Update memory with that finding and start a
  new probe targeting later writes in the same byte range.
- **Mismatch.** Differ pinpoints the first divergent byte. Use Task C
  fields + Task D reverse-search to walk back to root cause. File a
  new fix branch with the framework correction.

Either way, the deliverable from this session is the *answer*, not
just the tooling.

## Pitfalls / things to know

- `runner/snes9x-core/` is upstream snes9x with our patch
  (`runner/snes9x_oracle.patch`). Edits to ppu.h MUST be reflected in
  that patch file (run the apply tool to regenerate, or hand-edit the
  patch). If you skip this, the next clean build resets your work.
  Check `runner/apply_snes9x_patch.py` for the workflow.
- Oracle backend gating: `ENABLE_ORACLE_BACKEND` is set per-config in
  `src/smw.vcxproj` (Oracle config only). Release|x64 must stay byte-
  clean — no oracle code, no extra data, no extra string literals.
  Validate with `dumpbin /headers` if you suspect drift.
- The recomp's `s_vram_trace` is shared between the two PPU paths
  (`runner/src/snes/ppu.c` AND `common_rtl.c::WriteVramWord`). The
  oracle's path is `runner/snes9x-core/ppu.h` ONLY — different code
  base. Don't confuse them.
- `snes_frame_counter` is recomp-frame; the oracle's frame counter is
  separate (in `Memory.FillRAM`-adjacent state). For the differ,
  index by ring-write-order, not by frame.
- The DBPB ring + always-on rings live in BSS on Oracle builds —
  total fixed cost is now ~90 MB (WRAM + VRAM + block + insn + dbpb +
  oracle VRAM). Fine on the dev box; stay aware if memory becomes a
  concern.

## Memory entries to skim before starting

- `feedback_complete_over_fast.md` — completeness over surgical
  patches. Build the differ for ALL VRAM addresses, not just $5200-
  $5FFF.
- `feedback_dry_when_pattern_repeats.md` — DRY reflex. This handoff
  IS that reflex applied: instead of hand-investigating Layer-3,
  build the tool that mechanically attributes ANY VRAM divergence.
- `feedback_never_time_always_ring_buffer.md` — both rings always-on,
  query backward. No arm-then-attach in either probe or differ.
- `feedback_pause_step_is_arm_attach_antipattern.md` — same rule
  applied to step-driven differs. Free-run; query rings.
- `feedback_state_delta_is_not_symptom.md` — a divergent byte is
  necessary not sufficient. Final close needs visible-symptom tie
  (HUD render in user's image #14 stops looking corrupt).
- `project_vram_garbage_2026_04_30.md` — prior VRAM scare turned out
  to be a timing-skew artifact at unsynced frames. The differ should
  index by write-order or state-anchor, NEVER by wall-clock.
- `feedback_oracle_backend_pluggable_debug_config.md` — Release|x64
  must stay unchanged by oracle work.

## File anchor cheat-sheet (verified end of last session)

| Concern | Path | Line |
|---|---|---|
| Recomp VRAM ring struct | `snesrecomp/runner/src/debug_server.c` | 253 |
| Recomp VRAM hook fn | `snesrecomp/runner/src/debug_server.c` | 270 |
| Recomp VRAM always-on init | `snesrecomp/runner/src/debug_server.c` | 4264 |
| Recomp `cmd_get_vram_trace` | `snesrecomp/runner/src/debug_server.c` | 1731 |
| Cmd table | `snesrecomp/runner/src/debug_server.c` | 4030 |
| Recomp `WriteVramWord` | `snesrecomp/runner/src/common_rtl.c` | 300 |
| Recomp ppu.c hooks | `snesrecomp/runner/src/snes/ppu.c` | 1048,1055 |
| Snes9x core REGISTER_2118 | `snesrecomp/runner/snes9x-core/ppu.h` | 355 |
| Snes9x core REGISTER_2118_tile | `…/ppu.h` | 392 |
| Snes9x core REGISTER_2118_linear | `…/ppu.h` | 417 |
| Snes9x core REGISTER_2119* | `…/ppu.h` | 463+ (find by grep) |
| Snes9x bridge backend struct | `snesrecomp/runner/src/snes9x_bridge.cpp` | 971 |
| Existing probe (template) | `snesrecomp/_triage/probe_layer3_vram_writers.py` | — |

Open the doc; do the tasks; close the question; update memory.
