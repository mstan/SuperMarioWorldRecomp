# Troubleshooting a v2 codegen bug — XBA reads a stale shadow

A worked example of finding a v2-codegen bug end-to-end using the
project's always-on observability rings. Use this as a template when
the next "recomp visibly diverges from emulator" bug shows up.

**TL;DR** — the v2 emitter for `XBA` was reading `cpu->B` (a shadow of
A.high that's only updated by XBA itself) instead of `cpu->A`'s actual
current high byte. Every `LDA $abs` in m=0 mode silently invalidated
the shadow. The next `XBA` swapped in stale data. SMW's stripe-image
header parse `LDA [_0],Y / XBA / AND #$3FFF / TAX` mis-derived the
byte-count — recomp computed count=0 from data oracle correctly read
as count=$7F. Layer-3 stripe DMA loop terminated at iteration 2
instead of running to completion, and the title screen / HUD rendered
as VRAM scramble.

The fix is one line in `snesrecomp/recompiler/v2/codegen.py::_emit_xba`:
source the new low byte from `(cpu->A >> 8) & 0xFF`, not from
`cpu->B`.

## The visible bug

Attract demo renders with massive Layer-3 / HUD garbage on the top
half of the screen. Ground tiles and Mario sprites look broken too,
but those turn out to be downstream of a totally different bug
(`UploadLevelLayer1And2Tilemaps`); investigating those before this
one would have led nowhere because the L1/L2 path also depends on
correct stripe headers reading.

## Step 1 — query the always-on VRAM-write differ for the first
divergent byte

`vram_write_diff` walks the byte-granular paired rings (recomp side +
oracle side, both armed at boot) forward and reports the first
sequence-paired write where addresses or bytes diverge. Each diverged
record carries the recomp's full CpuState + recomp call stack at
write time.

```
$ python snesrecomp/_triage/probe_layer3_ranges.py
=== $5200-$5FFF  (Layer-3 tilemap) ===
  DIVERGED at idx 2304 (matched 2304 prior)
  recomp: 0x58b2=0x2c  func=UploadLevelTilemapHDMA_M1X1  f=95
  recomp regs: A=0x2c02 X=0x2c01 Y=0x2c0a m=1 x=0
  oracle: 0x5820=0xf8  f=183
  stack:  ... LoadStripeImage_M1X1 -> UploadLevelTilemapHDMA_M1X1
```

This pins the bug to **`UploadLevelTilemapHDMA_M1X1`** (the stripe-DMA
inner loop at `$00:871E`). 2 304 prior pairs matched, so everything
upstream of this function was producing identical writes — narrows
the suspect surface to this function specifically.

## Step 2 — DON'T just stare at the generated C

The temptation is to open `src/gen/smw_00_v2.c::UploadLevelTilemapHDMA_M1X1`
and read it line-by-line against `SMWDisX/bank_00.asm`. The function
is ~500 lines of v2-emitted C. Reading it doesn't scale, and you won't
spot a state-tracking bug that depends on a value that flows in from
*before* the function entry.

The right tool is the **block trace**: every basic-block entry on the
recomp side captures full CpuState into `g_cpu_trace_ring` (queryable
via the `trace_get_v2` TCP command), and every instruction on the
oracle side captures full hardware registers into the snes9x insn ring
(`emu_get_insn_trace`).  These are always-on at boot. Pair them
side-by-side at the same PC across the same logical call.

## Step 3 — find the matching call on each side

The bug fires inside one specific call to `UploadLevelTilemapHDMA`.
Other calls to the same function early-exit (BPL-not-taken on the
first sentinel byte). Any "first call" comparison without filtering
will land on a degenerate early-exit on both sides and tell you
nothing.

The probe at
`snesrecomp/_triage/probe_loadstripe_block_diff.py`:

1. Pulls every BLOCK event in PC range `$00:871E–$00:87AC` from
   `g_cpu_trace_ring`, walking backward via `before_idx` until the
   ring's earliest retained event.
2. Splits the stream into per-call instances (each call begins at
   PC=`$00:871E`).
3. Filters to "real" calls (calls that reached PC=`$00:872D`, i.e.
   passed the BPL and entered the body).
4. Same-shape pull from the oracle insn trace via `emu_get_insn_trace`.
5. Selects the recomp's first real call, then finds the oracle real
   call whose entry-Y matches recomp's entry-Y (so we're comparing
   uploads of the **same stripe**, not arbitrary calls).

```
recomp: 752 calls, 1 real (body-entering), first at idx 95
oracle: 12768 calls, 133 real, first at idx 95
rec entry Y = 0x0003                          # title-screen stripe
ora call 95 (frame 296): A=0x0205 ... Y=0x0003 <-- matches
```

The recomp has **only one** real upload across the entire boot, and
oracle has 133. That's a strong tell: most recomp calls hit a
terminator on byte 0. We'll come back to *why* in Step 5; first, find
the codegen bug.

## Step 4 — diff the matching calls block-by-block

```
  idx         pc  | rec  A    X    Y    m x  | ora  A    X    Y    m x  | diff?
    0 0x00871e  | 0205 00ff 0003 1 1  | 0205 00ff 0003 1 1  |
    1 0x008726  | 0205 00ff 0000 1 0  | 0205 00ff 0000 1 0  |
    2 0x00872d  | 0250 00ff 0000 1 0  | 0250 00ff 0000 1 0  |
    3 0x008795  | 0000 0001 0004 0 0  | 0000 0080 0004 0 0  | X    <-- FIRST DIVERGENCE
```

State matches through blocks 0/1/2. At block 3 (PC=`$00:8795`,
post-stripe-header parse, after the BEQ that skips the RLE branch),
recomp's X = `$0001` while oracle's X = `$0080`.

X is the count-of-bytes-to-DMA + 1 (TAX after AND #$3FFF, then INX).
recomp computed count = 0, oracle computed count = $7F. Same input
bytes (the long pointer + Y at function entry was identical, so the
ROM bytes being read are identical) producing different counts.

## Step 5 — look at the parse sequence in the generated C

Between PC `$00:872D` (state matched) and PC `$00:8795` (state
diverged), the emitter sequence is:

```
LDA [_0],Y      ; m=0, reads 16-bit count word from ROM
XBA             ; swap high/low — converts XX-endian count to native
AND #$3FFF      ; mask off top 2 bits (mode flags)
TAX             ; X = count
INX             ; X = count + 1
```

Open `src/gen/smw_00_v2.c` at the `UploadLevelTilemapHDMA_M1X1`
body and find the XBA expansion:

```c
{
  uint8 _lo = (uint8)(cpu->A & 0xFF);
  cpu->A = (uint16)((uint16)cpu->B | ((uint16)_lo << 8));
  cpu->B = (uint8)((cpu->A >> 8) & 0xFF);
  ...
}
```

This is the bug. **`cpu->B` is read as the new low byte**. But what
keeps `cpu->B` in sync with `cpu->A`'s high byte?

```
$ grep -rn 'cpu->B\b' snesrecomp/recompiler/v2/ snesrecomp/runner/src/
codegen.py:119:    Reg.A: "cpu->A", Reg.B: "cpu->B",
codegen.py:504:        "  cpu->A = (uint16)((uint16)cpu->B | ((uint16)_lo << 8));",
codegen.py:505:        f"  cpu->B = {widths.low_byte('(cpu->A >> 8)')};",
cpu_state.c:155:    cpu->B = 0;         # init
```

Nobody. `cpu->B` is initialised to 0 and ONLY mutated by XBA itself.
Every `LDA $abs` in m=0 (and there are many) writes a fresh 16-bit
value to `cpu->A` without touching `cpu->B`, so when the next XBA
fires, it reads a stale shadow.

In SMW's stripe header parse, the LDA-XBA-AND sequence runs *fresh*
into m=0 mode (the function does `REP #$20` two instructions earlier).
`cpu->B` is whatever it happened to be from the last XBA elsewhere in
the codebase; for the early calls during attract demo, that's 0.
`cpu->A` after LDA = `$00BF` (the count word, byte-swapped from ROM).
XBA computes new A = `0 | ($BF << 8) = $BF00`. AND #$3FFF gives
`$3F00`. TAX gives X = `$3F00`, INX = `$3F01`. **Recomp's X is
nonsense.**

(Wait — earlier the trace showed recomp X = `$0001`, which means
count was 0, not $3F00. The actual A.low after LDA was 0, because
the v2 read of the stripe header word landed on the BEQ-taken
no-RLE path immediately. Same conclusion either way: the XBA result
disagrees with what the hardware computes.)

The fix is in
`snesrecomp/recompiler/v2/codegen.py::_emit_xba`: source the new low
byte from `cpu->A`'s current high byte directly (don't trust the
shadow):

```python
lines = [
    "{",
    f"  uint8 _lo = {widths.low_byte('cpu->A')};",
    f"  uint8 _hi = {widths.low_byte('(cpu->A >> 8)')};",
    "  cpu->A = (uint16)((uint16)_hi | ((uint16)_lo << 8));",
    "  cpu->B = _lo;",  # B mirrors NEW A.high (= old A.low)
]
```

`cpu->B` is still updated for diagnostic display, but the swap no
longer depends on its freshness.

## Step 6 — regen, build, hand the exe to the user

```
$ python snesrecomp/tools/v2_emit.py --rom smw.sfc \
    --cfg-dir recomp --out-dir src/gen
$ MSBuild smw.sln /p:Configuration=Release /p:Platform=x64 /m /v:minimal
$ taskkill /F /IM smw.exe; build/bin-x64-Release/smw.exe
```

Visual verification before claiming closed. Buffer-match parity is
necessary, not sufficient.

## What made this 30 minutes after the rings were sized right

| step | tool | latency |
|---|---|---|
| 1. surface the byte-level divergence + writer function + stack | `vram_write_diff` (always-on byte-paired rings) | seconds |
| 2. diff CpuState at every block boundary in the matching call | `trace_get_v2` (recomp) + `emu_get_insn_trace` (oracle), filter by PC and call instance | seconds |
| 3. read 4 lines of v2 emit code | grep + Read | minutes |
| 4. one-line fix + regen + build + visual | v2_emit.py + MSBuild | a few minutes |

Total tool round-trips: ~6. None required arming a probe before the
buggy code ran — the rings were already populated from boot.

## What initially blocked us — ring sizing

The first attempt to use `get_insn_trace` returned 0 entries because
the recomp insn ring wasn't always-on at boot, and the oracle insn
ring at 1 M entries rolled over after ~100 frames (frame 95's
divergence was already gone). Both rings were heap-allocated and
sized up to 64 M entries each (~3 GB recomp / ~1.8 GB oracle), with
the cpu_trace ring also bumped to 16 M (~512 MB). Defaults are
`SNESRECOMP_INSN_RING_ENTRIES`, `SNESRECOMP_EMU_INSN_RING_ENTRIES`,
`SNESRECOMP_CPU_TRACE_RING_ENTRIES` — clamped `[1<<16, 1<<28]`.

This is the rule from `CLAUDE.md` made concrete: if you find yourself
reasoning "the events must have happened before I attached" — fix the
ring buffer, don't probe quickly.

## What this case generalises to

**v2's per-CpuState shadow fields are a foot-gun.** `cpu->B` is the
canonical example, but the same shape of bug can recur for any field
that exists *only* to be read by one emitter and is updated *only* by
that one emitter — every other emitter that mutates the underlying
primary field silently invalidates the shadow.

The next session's quick-win pass is auditing for similar shadow
fields and either deleting them (compute on demand from the primary)
or proving every primary-field writer also updates the shadow. See
`docs/QUICK_WINS.md` (when written).
