# recomp.py width-slip audit — Phase A

Seeded by the ASL/ROL/ROR-A 16-bit carry-bit-7-vs-15 bug found
2026-04-24 (snesrecomp commit 177ce2f). That bug's shape: emitter
knew the accumulator was 16-bit (`a_type == 'uint16'`) but used a
constant that was only correct for 8-bit. The symptom took ~6 hours
of visible-bug debugging to attribute.

This audit scans every emitter site that depends on register width
(M flag or X flag) and flags those that do not branch on width
correctly, that hard-code a constant tied to 8-bit, or that ignore
a width-change instruction entirely.

**Format.** Each entry: location, what it does, what the hardware
says, what the emitter does, severity (CONFIRMED BUG / PROBABLE BUG
/ SUSPECT / OK), and the validation path.

**Scope.** Only codegen for arithmetic/logic/shift/transfer/
control instructions touching A / X / Y / flags / the stack.
Addressing-mode decoders, CFG/flow analysis, and sig-inference are
OUT OF SCOPE — those belong to their own audits.

**Status legend.**
- **CONFIRMED BUG**: I have the expected-vs-actual in hand. Produces wrong output for a reachable code path.
- **PROBABLE BUG**: Emitter omits width-dependence where hardware requires it; high likelihood of wrong output given a ROM that uses that mode. Not yet demonstrated in gameplay.
- **SUSPECT**: Width-dependence is unclear from the code; warrants a targeted test before declaring safe.
- **OK**: Read carefully, found no width-related issue.

---

## Summary table

| # | Site | Severity | One-line |
|---|------|----------|----------|
| 1 | ASL/ROL/ROR A (lines 4321/4368/4391) | FIXED 2026-04-24 | Carry bit was 7, should be 15 in 16-bit A. |
| 2 | ASL/LSR/ROL/ROR memory | FIXED | 16-bit RMW routed through new `_emit_rmw16` under M=0. |
| 3 | TSB/TRB | FIXED | Same shape as #2. 16-bit TSB/TRB routes through `_emit_rmw16`. |
| 4 | BIT V-flag | FIXED | V-flag mask now 0x4000 under M=0, 0x40 under M=1. |
| 5 | SEP #$10 (X/Y → 8-bit) | REVERTED | Fix shipped but caused visible regression (Mario one tile into the ground, Bug #8 class). Reverted on branch memory-shift-rotate-16bit. Needs cross-function sig coherence — narrowing X inside a callee whose caller passed 16-bit X truncates a live value. Re-land after #5-bis audit. |
| 6 | REP #$10 high-byte preservation | DEFERRED | Low priority; any ROM relying on stale hi-byte of X/Y is buggy in its own right. Re-check via Phase B differential fuzz. |
| 7 | PHA/PHX/PHY + width-mismatched pop | DETECTION SHIPPED | Stack entries now carry push width; any PLA/PLX/PLY with mismatched width emits a RECOMP_WARN comment in the gen C. SMW produces zero such warnings — not a SMW-exercised bug class. Full byte-tracked stack deferred until a ROM trips the warning. |
| 8 | INX/DEX/INY/DEY wrap after SEP #$10 | REVERTED (coupled to #5) | Same fix as #5, same revert. Re-land together. |
| 9 | Decimal mode (CLD/SED + ADC/SBC) | MOOT for SMW | Grep of all SMWDisX banks: zero `SED` opcodes. D flag is cleared at reset and never set. Not a SMW-reachable bug class. Revisit for game #2 via Phase B. |
| 10 | TXA/TYA high-byte preservation in M=1 | VERIFIED OK | Re-read the emitter; it doesn't touch `self.B` during TXA/TYA, so B persists correctly for a subsequent XBA. No bug. |

Severity-weighted: **2–3 confirmed, 3–4 probable, 2–3 suspect.** Roughly matches my earlier estimate of "5-20 true bugs."

**Final status (closed 2026-04-24):**
- 4 fixed and shipped (#1 prior session + #2/#3/#4 here)
- 1 detection-only shipped (#7; zero matches in SMW gen)
- 2 reverted pending sig-coherence work (#5/#8 — caused visible regression)
- 3 verified-moot-for-SMW (#6 low-priority/suspect, #9 zero SED in ROM, #10 re-read OK)

#5/#8 re-land prerequisite: the narrowing fix must also update callee parameter types and live-in analysis so a 16-bit-X-passing caller's value doesn't get truncated when the callee SEPs #$10 mid-body. This is a Phase A.5 item between the current branch and Phase B.

Phase B (differential fuzz) remains next.

---

## 1. ASL / ROL / ROR on A — FIXED

`recomp.py:4321/4368/4391`

Already closed by `snesrecomp/177ce2f`. Tests pin at `snesrecomp/tests/test_accumulator_shift_width.py`. Listed here because it's the template for the rest of the audit — every other "OK" below has been checked against this pattern.

---

## 2. ASL / LSR / ROL / ROR memory — CONFIRMED BUG

`recomp.py:4331-4342, 4354-4365, 4378-4388, 4401-4411`

**What the code does.** Memory-operand branch of each shift/rotate calls `self._emit_rmw8(mode, v, '...')`. The helper is 8-bit by name and implementation (`recomp.py:3160`).

**What the hardware does.** When M=0 (16-bit accumulator), these are 16-bit RMW instructions: they read 2 bytes from `$DP`/`$DP+1` (or ABS+1), shift/rotate all 16 bits, and write both back. The carry out is bit 15 (shift-left) or bit 0 (shift-right — unchanged) of the 16-bit value.

**What the emitter does.**
- `ASL mem`: `cv = ({mem} >> 7) & 1;` and `new = {cur} << 1;` — treats as 8-bit. Wrong carry out (bit 7 not 15), wrong result (only low byte shifted, high byte untouched).
- `LSR mem`: `cv = {mem} & 1;` — carry out OK (bit 0 same for both widths). But the *stored* result is `{cur} >> 1`, which if `_emit_rmw8` narrows to 8-bit, loses the high byte.
- `ROL mem`: same as ASL with carry-in. Wrong bit 15 / bit 7 split.
- `ROR mem`: same as ROR-A bug (now fixed for A, still present for memory).

**Demonstrable impact.** A 16-bit ASL on `$DP` with M=0 changes a word like `$0180` to `$0300`; the emitter would change only `$80` at `$DP` to `$00` (and set carry), leaving `$DP+1` untouched — `$0100` instead of `$0300`. ROR/ROL is worse because the carry bit crosses between bytes in the correct implementation.

**Fix direction.** Add an `_emit_rmw16` helper (parallel to the 16-bit INC/DEC branch at `4277-4316`) and a `wide_a` check in each of the four shift/rotate sites. Alternatively, detect M=0 inside `_emit_rmw8` and forward.

**Test plan.**
- Unit test: emit an ASL $00 under M=0 and assert the body reads `_wram16` / writes `_wram16_write`, not `_wram` / `_emit_wram_store8`.
- Mirror tests for LSR/ROL/ROR.

**Validation against ROM.** Grep decoded listings for ASL/LSR/ROL/ROR in M=0 blocks; any hit is a confirmed miscompile. (Search SMWDisX bank files within REP #$20 blocks.)

---

## 3. TSB / TRB — CONFIRMED BUG

`recomp.py:4426-4445`

Same shape as #2. Both call `_emit_rmw8` regardless of `wide_a`. In 65816, TSB/TRB in M=0 test/set-or-reset 16 bits at the memory operand against the 16-bit accumulator.

**Fix direction.** Same as #2 — thread width through the helper or branch before calling.

**Test plan.** Emit TSB $00 and TRB $00 under M=0; assert 16-bit paths.

**Validation.** SMW uses TSB/TRB on DP flag words. Grep bank files for `TSB` / `TRB` within M=0 regions.

---

## 4. BIT V-flag for 16-bit A — PROBABLE BUG

`recomp.py:4421`

```python
self.overflow = f'({mem}) & 0x40'
```

Always bit 6. In 65816 BIT semantics:
- 8-bit (M=1): N = bit 7 of mem, V = bit 6 of mem.
- 16-bit (M=0): N = bit 15 of mem, V = bit 14 of mem.

N flag is handled correctly downstream via `flag_width`-driven sign-cast (`_branch_cond:3603-3605` casts to `int16` when flag width is 16). V is not width-dispatched — always bit 6.

**Impact.** Any BIT-then-BVS/BVC under M=0 takes the branch on the wrong bit.

**Fix direction.**
```python
bit = 0x4000 if wide_a else 0x40
self.overflow = f'({mem}) & 0x{bit:x}'
```

**Test plan.** Emit BIT $00 under M=0 followed by BVS; assert the generated condition references `0x4000` or bit 14 semantics.

**Validation.** BVS/BVC in 16-bit blocks is uncommon but exists. Grep SMWDisX for BVS/BVC inside REP #$20 regions.

---

## 5. SEP #$10 — X/Y width narrowing — PROBABLE BUG

`recomp.py:4477-4522`

REP #$10 (switch X/Y to 16-bit) is handled: tracker promotes the X/Y variable's hoisted type from `uint8` to `uint16` at lines 4494-4501.

**SEP #$10 (switch X/Y to 8-bit) is not handled.** Only SEP #$20 has a branch. If code does `REP #$10 ; ... ; SEP #$10 ; INX`, the INX runs on a uint16 variable in the emitter's view, so a value of 0x00FF increments to 0x0100. On real hardware, X is now 8-bit, so `0xFF + 1 = 0x00`, and the high byte is cleared.

**Impact.** Any loop that switches X/Y back to 8-bit and then increments/decrements through 256.

**Fix direction.** Mirror the REP #$10 handling: when SEP #$10 fires, walk X/Y hoisted types and demote them, potentially emitting a narrowing cast `{xn} = (uint8){xn};` to clear the high byte.

**Test plan.** REP #$10 ; LDX #$01FF ; SEP #$10 ; INX ; CPX #$00 ; BEQ — the BEQ should be taken; assert the emitted INX operates on 8 bits after SEP.

**Validation.** Less common than REP #$10 (most code goes 8→16→8 around a specific routine, not 16→8 mid-loop), but SMW has some. Grep SMWDisX for `SEP #\$10` or `SEP #\$30`.

---

## 6. REP #$10 high-byte preservation — SUSPECT

`recomp.py:4494-4501`

When promoting X/Y from uint8 to uint16, the emitter just changes the hoisted type. On the next read, the variable holds whatever C zero-extension gives (so the high byte is 0).

On real hardware, the high byte of X/Y is implementation-defined after REP #$10 — it's the value that was there before the last SEP #$10, which may be non-zero.

**Impact.** ROMs that rely on this stale high byte are rare and probably buggy in their own right, but it's a behavior divergence.

**Recommendation.** Document as known divergence; low priority to fix. Validate via differential fuzz (Phase B) — if no ROM trips it, stays low-priority.

---

## 7. PHA/PHX/PHY stack width — PROBABLE BUG

`recomp.py:4050-4131`

Stack entries are pushed as `(reg, value)` tuples. No byte count is tracked.

**Hardware.** PHA pushes 2 bytes if M=0, 1 byte if M=1. PLA pops 2 if M=0, 1 if M=1. Same for PHX/PLX, PHY/PLY with the X flag.

Mismatched pairs are rare but real:
```
PHA           ; M=1, pushes 1 byte
SEP/REP or some width change between
PLA           ; wrong width, pops stack incorrectly
```

**Emitter behavior.** `PLA` pops whatever the stack's top entry holds, regardless of the current M flag. A PHA(16) + PLA(8) in the emitter returns the 16-bit value, not the low 8 bits.

**Impact.** Width-matched push/pop pairs are the common case and work. Mismatched pairs (idiomatic in some flag-save routines) miscompile.

**Test plan.** Emit PHA under M=0, SEP #$20, PLA under M=1; assert the PLA produces an 8-bit narrowing.

**Validation.** Differential fuzz (Phase B) or grep bank files for `PHA.*PLA` across REP/SEP transitions.

---

## 8. INX/DEX/INY/DEY wrap after SEP #$10 — PROBABLE BUG

Related to #5, same root cause but worth separating because the symptom is different (wrap radix rather than stale-byte).

`recomp.py:4224-4263`. INX emits `{xn}++;`. If xn was allocated as `uint16` (during the 16-bit block) and SEP #$10 was executed, the emitter still treats it as uint16, so `0xFF + 1 = 0x0100` in C. On hardware, X is now 8-bit, so `0xFF + 1 = 0x00`.

Tied to #5's fix (demote at SEP). Same test plan applies.

---

## 9. Decimal mode (SED / CLD + ADC/SBC) — SUSPECT

`recomp.py:4538` treats `SED` and `CLD` as no-ops. `_emit_adc` / `_emit_sbc` emit binary arithmetic unconditionally.

**Hardware.** With D flag set, ADC/SBC operate in BCD. Each nibble is clamped 0-9 with tens-digit carry.

**Impact.** ROM that uses decimal mode (score counters in some games, passed-through values) miscompiles silently.

SMW is unlikely to use decimal mode (SNES games mostly avoid it). But this is an unverified assumption. Low priority to fix; high priority to *check* with a targeted scan.

**Validation.** Grep SMWDisX for SED. If zero hits, leave alone. If any hit, trace what uses it.

---

## 10. TXA / TYA high-byte preservation in M=1 — SUSPECT

`recomp.py:4008-4027`

On 65816 with 8-bit A (M=1), TXA transfers only X's low byte into A's visible low byte. The hidden high byte (B, which XBA can swap in) is NOT modified.

Emitter: `name = self._alloc(a_type)` where `a_type == 'uint8'`, then `{name} = {self.X};`. Self.A is set to the new var. Self.B is not changed.

This is actually correct behavior. The subtle case: if the code does

```
LDA #$FF00     (M=0)
SEP #$20       (M=1; B = $FF, A_low = $00)
LDX #$55
TXA            ; A becomes $55, B should still be $FF
XBA            ; A and B swap: A = $FF, B = $55
```

Does the emitter preserve B across TXA in the M=1 case? Checking line 4012-4018: it sets `self.A = name` and doesn't touch `self.B`. So `self.B` remains `$FF`. Good.

**Status:** On re-read, looks OK. Flagged as SUSPECT only because the interaction between the B register (XBA) and transfer instructions has bitten us before. Left for differential-fuzz confirmation.

---

## What's NOT in this audit (and where it belongs)

- **CFG liveness / RetA/RetAY/RetAXY inference** — separate audit, its own class of bugs (Bug #8 era).
- **Addressing-mode decoder** — covered by `test_smwdisx_compare.py` and existing opcode-table tests.
- **Function signature inference / cross-bank ABI** — separate from width.
- **D/DB/PB register tracking** — separate; bank/DP-aliasing audit.
- **Dispatch / jump tables** — structural, not arithmetic.

These should each get their own audit once #2-#8 above are closed and retested.

---

## Recommended commit sequence

1. Fix #2 (memory shift/rotate width) + test. One commit. Regen + visual-test.
2. Fix #3 (TSB/TRB width) + test. One commit. Regen + visual-test.
3. Fix #4 (BIT V-flag width) + test. One commit. Regen + visual-test.
4. Fix #5 / #8 together (SEP #$10 narrowing) + tests. One commit. Regen + visual-test.
5. #7 (stack width) — fix if differential fuzz flags it or a bug surfaces. The model change (byte-tracked stack) is invasive; weigh cost.
6. #6, #9, #10 — leave in the doc as known unverified; let Phase B shake them out.

Each "regen + visual-test" is the same loop as 2026-04-24's death-sprite fix: rebuild Release, launch, user visual-confirms, commit gen, merge.

---

## Method note for future audits of this shape

The common failure mode is **"emitter knows a width-related parameter exists but hard-codes a constant for one width."** Check every site that:

1. Accepts `a_type` / `x_type` / `wide_a` / `wide_x` as a parameter but uses only *some* of it.
2. Writes a literal constant where a constant *could* depend on width (`7` vs `15`, `0x80` vs `0x8000`, `0x40` vs `0x4000`, `256` vs `65536`).
3. Calls a helper that is named or implemented for one width only (like `_emit_rmw8`) from a code path that can execute in the other width.

The ASL/ROL/ROR-A fix covered pattern (1) by passing the already-in-scope `a_type` through the formula. #2/#3/#4 above all fit pattern (3) or (2). That's the common thread; future emitters should be reviewed against these three patterns explicitly.
