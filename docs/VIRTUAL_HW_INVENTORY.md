# Virtual-HW Timing Inventory — SMW

**Branch:** `virtual-hw-timing`
**Goal:** make recomp's attract demo byte-for-byte identical to snes9x by giving recompiled C virtual-hardware semantics that match cycle-accurate behavior at every register-read site whose result depends on cycle timing.

This document inventories every timing-sensitive HW register READ in SMW (write sites are not enumerated here — writes don't have timing semantics in the same way; they merely program state). The inventory drives the runner's virtual-HW handler design (task #12) and per-register implementation (task #13).

Source: grep across `SMWDisX/bank_*.asm` for `LDA/LDX/LDY/CMP/BIT/EOR/ORA/AND/ADC/SBC/TSB/TRB/ASL/LSR/ROL/ROR HW_*` of timing-sensitive registers.

Total: **57 read instructions** across **6 distinct registers**.

---

## $4210 RDNMI — NMI flag (read-clears)

**Behavior on hardware:** Bit 7 = 1 if NMI has occurred since last read; the low nibble is CPU revision. Reading the register clears bit 7 (latch reset).

**Reads in SMW:** 1
- `bank_00:204` — at NMI handler entry: `LDA HW_RDNMI ; Read to clear the n flag.`

**Why timing matters:** The NMI handler reads it to acknowledge the interrupt; if the read returns the wrong value (e.g., always 0), the CPU-rev fingerprinting at boot may misdetect the system. SMW only USES the return value to be discarded (LDA result not consumed); the side-effect (clearing the latch) is what matters. Static recomp can return `0x82` (CPU rev 2 + NMI bit) and it'll work.

---

## $4211 TIMEUP — IRQ flag (read-clears)

**Behavior on hardware:** Bit 7 = 1 if IRQ has occurred since last read. Reading clears bit 7.

**Reads in SMW:** 3
- `bank_00:331` — IRQ #1 arming: `LDA HW_TIMEUP` (in `CODE_008294`, before setting up status-bar IRQ)
- `bank_00:446` — at IRQ handler entry: `LDA HW_TIMEUP ; Read the IRQ register, 'unapply' the interrupt; BPL ExitIRQ` — *gates IRQ execution on bit 7*
- `bank_00:489` — IRQ chaining (Roy/Morton boss): `LDA HW_TIMEUP ; Set up the IRQ routine for layer 3`

**Why timing matters:** The bank_00:446 read is load-bearing — the IRQ handler EXITS if bit 7 is clear, treating the IRQ as spurious. The recomp's `I_IRQ()` is invoked synchronously from `SmwDrawPpuFrame` per-scanline (smw_rtl.c:33-34). The runner must assert bit 7 BEFORE calling I_IRQ so the handler doesn't bail.

**Current handling:** smw_rtl.c:33 sets `g_snes->inIrq = true` before `I_IRQ()`. Need to verify `recomp_hw.c::ReadReg(0x4211)` returns `inIrq << 7` and clears `inIrq` on read.

---

## $4212 HVBJOY — vblank/hblank/joypad-busy

**Behavior on hardware:**
- Bit 7 = 1 during vblank (V flag in BIT instruction reads bit 6, hblank)
- Bit 6 = 1 during hblank (V flag from BIT-test)
- Bit 0 = 1 while auto-joypad-read is in progress

**Reads in SMW:** 2 — both in `WaitForHBlank` busy-wait loops
- `bank_00:542` — `BIT HW_HVBJOY; BVS -` — wait while currently in hblank (skip past current hblank)
- `bank_00:544` — `BIT HW_HVBJOY; BVC -` — wait until next hblank starts

**Callers of `WaitForHBlank`:** any IRQ-tail handler that needs to write hblank-only registers (BG3 scroll, layer-3 IRQ adjustments). Used at bank_00:454, 484, and others.

**Why timing matters:** This is THE classic cycle-accuracy busy-wait. snes9x runs the loop ~1500 cycles until hblank actually starts; recomp's CPU has no concept of "scanline progress within a frame," so the loop will spin forever or instantly terminate depending on what `ReadReg(0x4212)` returns.

**Current handling (smw_rtl.c:21-37):** Recomp drives `ppu_runLine(g_ppu, i)` inside `SmwDrawPpuFrame` for i=0..224, then dispatches IRQ at the configured V-trigger line. The IRQ handler runs synchronously inside the per-scanline loop. So when `WaitForHBlank` runs INSIDE I_IRQ's call chain, we ARE conceptually mid-scanline — the runner needs to advance the line/HBlank-flag while the busy-wait spins.

**Fix shape:** `ReadReg(0x4212)` must be wired to the runner's hblank simulator. When recompiled C reads $4212 in a busy-wait, the runner advances the simulated h-counter and returns the bit pattern that matches "now." snes9x effectively does this by executing more cycles during the busy-wait read.

---

## $4214/$4215 RDDIV — division quotient

**Behavior on hardware:** After writing $4204 (lo), $4205 (hi), $4206 (divisor), the SNES divide unit takes 16 CPU cycles, then $4214/$4215 hold the 16-bit quotient and $4216/$4217 hold the 16-bit remainder. Reading earlier returns garbage.

**Reads in SMW:** 14 (10 lo + 4 hi)
- bank_00, bank_01, bank_02, bank_04, bank_05 — scattered across math-heavy code (sprite physics, BG-tilemap math, etc.)

**Why timing matters:** Hardware delay is real but recomp can compute the result instantly at write time (no separate divide unit; just `div = a / b; mod = a % b;` in C when $4206 is written). As long as RDDIV/RDMPY are populated synchronously on the write, the read returns the correct value regardless of cycle delay.

**Current handling:** Need to verify `WriteReg(0x4206)` triggers the divide and stores result in the readback registers. If yes, no further work; if no, this is a bug.

---

## $4216/$4217 RDMPY — multiplication product / division remainder

**Behavior on hardware:** After writing $4202 (multiplicand), $4203 (multiplier), 8-cycle delay, then $4216/$4217 hold the 16-bit product. After a divide ($4204/$4206 sequence), $4216/$4217 hold the 16-bit remainder.

**Reads in SMW:** 33 (15 lo + 18 hi)
- All banks. Heaviest user. Includes RMW: `ASL HW_RDMPY` (bank_02, bank_03) — read-modify-write the product directly! That's a 65816 trick to multiply by 2 in-place.

**Why timing matters:** Same as RDDIV — recomp computes synchronously at write time, no delay needed.

**Current handling:** Same audit as RDDIV.

---

## $4218/$4219 CNTRL1 + $421A/$421B CNTRL2 — auto-polled joypad shadows

**Behavior on hardware:** SNES auto-poll latches joypad data into these registers during vblank (if $4200 bit 0 is set). The values are stable for the rest of the frame. $4218 is player 1 low byte (B/Y/Sel/St/Up/Dn/Lf/Rt), $4219 is high byte (A/X/L/R + 4 zeros).

**Reads in SMW:** 4 — all in `ControllerUpdate` (bank_00:787-833)

**Why timing matters:** The auto-poll happens at a specific point in vblank. If the recomp's joypad shadows aren't populated BEFORE `ControllerUpdate` runs, the function reads zeros and Mario gets no input.

**Current handling:** The recomp runner provides `g_snes->autoJoyTimer` simulation but the actual $4218 read implementation needs verification. The NMI-order fix (parent commit 4135d20) ensures NMI runs before main, so NMI's ControllerUpdate sees the correct shadow values — IF the runner wrote them before NMI fires.

---

## NOT used by SMW (can stay unimplemented)

These registers exist but SMW doesn't read them — no virtual-HW handler needed:

- $2137 SLHV (write-strobe to latch H/V counters): SMW doesn't write
- $213C/D OPHCT/OPVCT (latched H/V counter readback): no reads
- $213E/F STAT77/78 (PPU status, external-latch flag): no reads
- $2138/9/B (RVMDATA/ROAMDATA/RCGDATA — PPU data readback): no reads
- $2180 WMDATA (WRAM via PPU port readback): no reads
- $4213 RDIO (programmable I/O readback): no reads
- $421C/D/E/F (CNTRL3/4 — players 3-4 joypad shadows): no reads

This narrows the audit/implementation surface considerably.

---

## Next steps

- Task #11: audit `snesrecomp/runner/src/recomp_hw.c::ReadReg/WriteReg` for the 6 registers above. Note current behavior vs required.
- Task #12: design contract — for each register, a one-paragraph spec of what the runner must return and what state must be updated.
- Task #13: implement, regen unaffected, validate against state-sync diff + regression + boot smoke per register.
