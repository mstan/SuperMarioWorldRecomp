# Virtual-HW Audit — current `recomp_hw.c` + `snes_readReg` per register

**Branch:** `virtual-hw-timing`
**Source files:**
- `snesrecomp/runner/src/recomp_hw.c` (34 lines — the recomp's HW dispatch)
- `snesrecomp/runner/src/snes/snes.c::snes_readReg/snes_writeReg` (the borrowed snes9x register impl that recomp_hw.c delegates to for everything except $4212)

This audit covers every register from `VIRTUAL_HW_INVENTORY.md` and reports current vs needed behavior. Result: most are already correct via the snes9x-borrowed impl; the one architectural gap is $4212.

---

## $4210 RDNMI — NMI flag

**Current** (`snes.c:169-173`):
```c
case 0x4210: {
  uint8_t val = 0x2; // CPU version (4 bit)
  val |= snes->inNmi << 7;
  return val;
}
```

**Gap:** Does NOT clear `inNmi` on read. Real hardware clears bit 7 latch.

**Impact on SMW:** None — SMW's only read at `bank_00:204` discards the loaded value. `snes->inNmi` is also never set to `true` anywhere in our runtime (initialized false in `snes_init`, never written). So the read always returns 0.

**Fix:** Add `snes->inNmi = false;` after the OR. Trivial. Also: wire `inNmi=true` somewhere if/when we want NMI-pending to actually mean something. For now, the ROM doesn't notice.

---

## $4211 TIMEUP — IRQ flag

**Current** (`snes.c:174-178`):
```c
case 0x4211: {
  uint8_t val = snes->inIrq << 7;
  snes->inIrq = false;
  return val;
}
```

**Gap:** None. Returns + clears, matching hardware.

**Caller flow:** `smw_rtl.c:33` sets `g_snes->inIrq = true` before each per-scanline `I_IRQ()` invocation. The IRQ handler reads `$4211`, gets bit 7 = 1, and proceeds (instead of bailing on the `BPL ExitIRQ` at `bank_00:447`). ✓ correct.

---

## $4212 HVBJOY — vblank/hblank/joypad-busy

**Current** (`recomp_hw.c:21-32` — OVERRIDES `snes_readReg`):
```c
if (reg == 0x4212) {
  static uint8 hblank_toggle;
  hblank_toggle ^= 0x40;
  return (g_snes->inVblank << 7) | hblank_toggle;
}
```

**snes9x impl that's bypassed** (`snes.c:179-184`):
```c
case 0x4212: {
  uint8_t val = (snes->autoJoyTimer > 0);
  val |= (snes->hPos >= 1024) << 6;
  val |= snes->inVblank << 7;
  return val;
}
```

**Gap:** The hand-written hack toggles bit 6 (hblank) on every read. It works for `WaitForHBlank`'s `BIT $4212; BVS -; BIT $4212; BVC -` because the alternation guarantees both edge waits terminate, but it's not a hardware model.

The snes9x impl uses `snes->hPos >= 1024` for the hblank bit. `hPos` is the dot counter advanced by `ppu_runLine` inside `SmwDrawPpuFrame`. **But `recomp_hw.c::recomp_read_internal_reg` is called from recompiled C executing OUTSIDE `ppu_runLine`** — so when SMW's IRQ handler busy-waits on $4212, `hPos` is whatever value it was left at after the last `ppu_runLine` call. Static. The snes9x impl would deadlock.

The toggle hack avoided the deadlock without modeling the underlying time. The cost: anything that depends on $4212 reflecting "the actual current hblank state" sees garbage.

**Impact on SMW:** Only `WaitForHBlank` reads $4212, and it uses BOTH edges of the toggle. Both `BVS -` (skip current hblank) and `BVC -` (wait for next hblank) make progress because the toggle gives them alternating values.

**The deeper problem:** `WaitForHBlank` is called from inside `I_IRQ` (`bank_00:454`) and adjusts BG3 scroll registers expecting to be in hblank. In real hardware, the writes hit hblank because the busy-wait actually waited until hblank. In recomp, the writes hit at whatever sub-frame moment the per-scanline IRQ ran, regardless of "hblank" state. **The PPU sees these writes as "applied at the current scanline"** — which IS the IRQ's V-trigger scanline. So the hblank-or-not distinction may not affect the visible result for SMW specifically (BG3 scroll mid-scanline vs at-hblank both produce the same tilemap shift if applied before the next scanline starts rendering).

**Fix shape (task #13):**
- Option A — keep the toggle hack but document why. The visible result is correct for SMW; this isn't load-bearing for any open bug.
- Option B — model an h-counter that increments per `recomp_read_internal_reg(0x4212)` call (or per any HW-reg op) and returns the bit-pattern matching simulated hblank. Same end result for SMW, more general for game #2.
- Option C — drive `ppu_runLine` from inside the busy-wait (synthesize cycle progress on demand). Much heavier.

Recommend B (model an h-counter, cheap, generalizes).

---

## $4214/$4215 RDDIV — division quotient

**Current** (`snes.c:187-190`):
```c
case 0x4214: return snes->divideResult & 0xff;
case 0x4215: return snes->divideResult >> 8;
```

`divideResult` is computed synchronously on `$4206` write (`snes.c:278-287`):
```c
case 0x4206: {
  if(val == 0) {
    snes->divideResult = 0xffff;
    snes->multiplyResult = snes->divideA;
  } else {
    snes->divideResult = snes->divideA / val;
    snes->multiplyResult = snes->divideA % val;
  }
  break;
}
```

**Gap:** None. The 16-cycle hardware delay is irrelevant because recomp has no concept of "cycles between WriteReg and ReadReg" — the result is ready instantly. SMW always reads after at least one instruction, so it doesn't matter. ✓ correct.

---

## $4216/$4217 RDMPY — multiplication product / division remainder

**Current** (`snes.c:191-194`):
```c
case 0x4216: return snes->multiplyResult & 0xff;
case 0x4217: return snes->multiplyResult >> 8;
```

`multiplyResult` populated in `$4203` write (multiply) or `$4206` write (divide remainder).

**Gap:** None. ✓ correct.

**Note:** SMW uses `ASL HW_RDMPY` and `ASL HW_RDDIV` (RMW) at several sites in banks 02/03. The recomp's WRAM-backed g_ram doesn't include these registers; the RMW must dispatch through the read+write helpers. Need to verify recomp.py emits RMW on $4216 as `recomp_read_internal_reg + asl + recomp_write_internal_reg`. If it just touches g_ram, the read returns 0 and the write goes nowhere. **TBD audit at the recomp.py emitter level.**

---

## $4218/$4219 CNTRL1 — auto-polled joypad shadow (player 1)

**Current** (`snes.c:219-222`):
```c
case 0x4218: return SwapInputBits(snes->input1_currentState) & 0xff;
case 0x4219: return SwapInputBits(snes->input1_currentState) >> 8;
```

`input1_currentState` set in `common_rtl.c:70`:
```c
g_snes->input1_currentState = inputs & 0xfff;
```
where `inputs` is the per-frame controller value passed to `RtlRunFrame`.

**Gap:** Auto-poll timing model is "set once at frame entry, stable for the whole frame." Real hardware auto-polls during vblank; reads BEFORE the auto-poll latches return the previous frame's value (or zeros pre-first-NMI).

**Impact on SMW:** SMW's only read of $4218/$4219 is in `ControllerUpdate` (`bank_00:787-833`), which is called from NMI. By that point our `input1_currentState` is populated. ✓ correct.

**Attract demo specifics:** During attract, the live player input is empty (no controller plugged). `RtlRunFrame(inputs=0)` sets `input1_currentState=0`. NMI's ControllerUpdate reads $4218=0 and writes $15/$16=0. Then main loop's GM07 handler overrides $15/$16 with demo bytes. Mario reads $15/$16 mid-frame. The $4218 path is correct but unused for demo input.

---

## $421A/$421B CNTRL2 — auto-polled joypad shadow (player 2)

Same as CNTRL1 with `input2_currentState`. ✓ correct, unused by attract demo.

---

## Summary

| Register | Status | Action needed |
|---|---|---|
| $4210 RDNMI | bug: no read-clear | Fix `snes_readReg` to clear `inNmi` |
| $4211 TIMEUP | ✓ | none |
| $4212 HVBJOY | toggle hack works for SMW, not a model | Replace with h-counter model (recommended) |
| $4214/5 RDDIV | ✓ | none |
| $4216/7 RDMPY | ✓ ; verify RMW emit | audit recomp.py for `ASL $4216` codegen |
| $4218/9 CNTRL1 | ✓ | none |
| $421A/B CNTRL2 | ✓ | none |

**Verdict:** The runner's virtual-HW layer is in much better shape than the working hypothesis suggested. Most reads route through synchronous, recomp-friendly impls. The one architectural gap is $4212 (hack instead of model).

This means the demo desync we observed earlier is likely NOT explained by HW-register-level cycle-accuracy gaps — both sides should produce the same end-of-frame state given identical NMI/main-loop ordering. Other candidates for the desync source (to investigate next):
- snes9x's pre-attach state (snes9x runs RESET/early-frames before our debug_server_init armed the trace)
- Frame-counter offset (snes9x emulates GM=00/01 frames internally before our first emu_step)
- DMA timing differences
- IRQ scheduling differences (recomp fires per-scanline; snes9x fires at exact V-trigger cycle)
