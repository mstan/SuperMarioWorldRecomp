# Virtual-HW Timing Model — Design Contract

**Branch:** `virtual-hw-timing`
**Inputs:** `VIRTUAL_HW_INVENTORY.md` + `VIRTUAL_HW_AUDIT.md`
**Output:** per-register spec for what the runner must return / update on read.

---

## Architectural premise

The recomp has no cycle-stepping CPU. The recompiled C executes function-by-function with no concept of "this LDA took 4 cycles." snes9x simulates ~21 MHz at cycle granularity; recomp executes at "as fast as the host can run the C." Any HW register whose value depends on cycle position needs the runner to SIMULATE the result without simulating the cycles.

Three classes of register based on where the timing dependency lives:

- **(A) Cycle-delayed result, computed synchronously on write.** Hardware would deliver the result N cycles after a write to a related register; we compute it instantly at the write. No further work — recomp's "function-by-function" execution always inserts at least one instruction between write and read, which is way more time than the hardware delay. ($4214/5 RDDIV, $4216/7 RDMPY)

- **(B) State latch updated by an external event (NMI fire, IRQ fire, joypad auto-poll, vblank start).** The runner must drive the latch from the same event source that the recompiled code expects. ($4210 RDNMI, $4211 TIMEUP, $4218/9 + $421A/B CNTRL1/2)

- **(C) Continuously-changing state (h-counter, v-counter, hblank-active flag).** Hardware updates these every dot-clock; the recomp has no dot-clock. The runner must synthesize a value that is internally CONSISTENT across successive reads from the same code path, so busy-waits terminate correctly and any code that reads two related registers (e.g. low byte then high byte of a counter) gets a coherent pair. ($4212 HVBJOY)

---

## Per-register contract

### $4210 RDNMI

**Contract:**
- Bit 7 = `inNmi`. Other bits = CPU revision (0x02).
- READ MUST CLEAR `inNmi`.
- `inNmi` MUST be set to true by the runner exactly when `auto_00_816A()` (the recompiled NMI handler) is about to be called.
- After NMI handler completes, `inNmi` is whatever the handler's read left it (false).

**Why:** SMW's NMI handler reads $4210 to acknowledge. If `inNmi` is not set when the read happens, bit 7 is 0; SMW currently doesn't check bit 7 (the load is discarded), but other ROMs may. The clear-on-read is hardware-correct and prevents a state leak across NMI invocations.

**Implementation:**
- `snes.c::snes_readReg(0x4210)`: add `snes->inNmi = false;` after computing val.
- `smw_rtl.c::SmwRunOneFrameOfGame`: set `g_snes->inNmi = true;` immediately before `auto_00_816A();`.

---

### $4211 TIMEUP

**Contract:**
- Bit 7 = `inIrq`. Other bits = 0 (open bus on real hardware, but SMW masks).
- READ MUST CLEAR `inIrq`.
- `inIrq` MUST be set to true by the runner immediately before each `I_IRQ()` invocation.

**Why:** SMW's IRQ handler at `bank_00:446` reads $4211 with `BPL ExitIRQ`. If bit 7 is clear, the handler treats the IRQ as spurious and exits without doing the layer-3 BG-scroll work. The runner must assert bit 7 BEFORE calling I_IRQ.

**Implementation:** Already correct — `snes.c:174-178` returns + clears; `smw_rtl.c:33` sets `inIrq = true` before each `I_IRQ()` call. **No change needed.**

---

### $4212 HVBJOY

**Contract:**
- Bit 7 = `inVblank` (1 if current scanline is in vblank window).
- Bit 6 = `hblank-active` (1 if current dot position is in hblank window — dots ~1024..1364 of a ~1364-dot scanline).
- Bit 0 = `autoJoyTimer > 0` (auto-poll in progress).

**Required behavior:**
- Successive reads from a busy-wait MUST eventually transition both edges (0 → 1 and 1 → 0) so `BIT $4212; BVS -` and `BIT $4212; BVC -` terminate.
- The transitions must happen in BOUNDED steps so the busy-wait doesn't burn unbounded host CPU.
- Bits 7 and 0 must reflect the runner's actual `inVblank` / `autoJoyTimer` state.

**Static-recomp model:** Maintain a virtual `hPos` counter inside `Snes`. Each $4212 read increments `hPos` by a constant (e.g. 64 dots per read — calibrated so a typical busy-wait takes ~10-20 reads to cross hblank). When `hPos` passes 1024, bit 6 = 1. When `hPos` passes 1364, wrap to 0.

The increment is per-READ, not per-cycle. We have no cycles. But the per-read increment guarantees that busy-waits make progress and that the bit pattern over successive reads matches what real hardware produces over the busy-wait's actual cycle count.

**Why this is correct for SMW:** SMW's only $4212 use is `WaitForHBlank` (bank_00:539-548). The two BIT/BVS+BIT/BVC sequence terminates after both transitions. The post-WaitForHBlank writes (BG3 scroll) hit the PPU at "the simulated scanline of the IRQ" — which is what the IRQ V-trigger configured. The hblank-or-not distinction within that scanline doesn't change the visible result because PPU rendering for the next scanline doesn't start until `ppu_runLine(line+1)` is called, which happens after `I_IRQ()` returns.

**Why it generalizes:** Any game that polls $4212 in a busy-wait gets a coherent transition sequence. A game that reads $4212 outside a busy-wait (e.g. one-shot test) gets `(inVblank, hblank=current_hPos_state, autoJoy=current_autoJoyTimer)` — semantically meaningful even if the dot-count is approximate.

**Implementation:**
- `snes.c::snes_readReg(0x4212)`: keep the snes9x impl shape but add `snes->hPos = (snes->hPos + 64) % 1364;` before computing the bit.
- `recomp_hw.c::recomp_read_internal_reg`: REMOVE the $4212 override entirely. Route through `snes_readReg`.

---

### $4214/5 RDDIV, $4216/7 RDMPY

**Contract:** Read returns the synchronous-computed value from the most recent matching write.

**Implementation:** Already correct (`snes.c:187-194` + `snes.c:262-287`). **No change needed.**

**Audit follow-up:** verify recomp.py emits `ASL $4216` as `recomp_read_internal_reg(0x4216) → ASL → recomp_write_internal_reg(0x4216)`. If it just touches g_ram, the read returns 0 and the write goes nowhere. **Open question for task #13.**

---

### $4218/9 CNTRL1, $421A/B CNTRL2

**Contract:**
- Read returns the auto-polled joypad shadow for the corresponding port.
- The shadow MUST be populated before NMI runs each frame. (It's read inside `ControllerUpdate`, which is called from NMI.)

**Implementation:** Already correct (`snes.c:219-226`; populated in `common_rtl.c:70` from `RtlRunFrame`'s `inputs` arg, which precedes the run_frame call). **No change needed.**

---

## Summary of changes for task #13

1. **$4210**: add `snes->inNmi = false;` in `snes_readReg`. Set `g_snes->inNmi = true;` before `auto_00_816A()` in `smw_rtl.c`.
2. **$4212**: drop the `recomp_hw.c` override. Add `hPos` advance in `snes_readReg(0x4212)`.
3. **$4216 RMW audit**: grep recomp.py output for any `ASL $4216` codegen path; verify it uses register helpers, not g_ram. If it uses g_ram, fix the recomp.py emitter to route HW-register RMW through the read/write helpers.

Per-change validation:
- Framework tests (192 PASS, 2 pre-existing reds).
- Regression test (4 invariants).
- Boot smoke (frame-100 state matches baseline).
- State-sync diff at GM=07 (re-run `_probe_state_sync_full.py`); the 10 OAM residue diffs are cycle-accuracy artifacts and may shift but should not grow significantly. Track delta.
