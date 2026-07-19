# Audio Transition Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove reproducible loss of SPC and MSU-1 audio commands during level transitions without changing guest-visible CPU-to-APU handshake semantics.

**Architecture:** Treat `audio_trace` as the transport oracle: a command is only valid when an SPC read observes it before a later CPU write replaces it. First reproduce against the v0.9.5 runner revision, then compare current `main`. Implement the smallest timing correction in the shared `snesrecomp` APU boundary proven by the trace, commit it in the framework, and advance SMW’s submodule pointer.

**Tech Stack:** C11, SDL2, CMake, the `snesrecomp` submodule, existing audio trace rings, Python 3 `tools/sfx_probe.py`.

## Global Constraints

- Preserve immediate guest CPU-to-APU visibility for real upload handshakes.
- Do not modify `src/gen/` or use a SMW-specific timing workaround.
- The same transition must produce zero `LOST` commands with SPC and MSU-1 music.
- Verify normal speed and turbo.
- Fix additional issues only when reproduced through this verification route.

---

### Task 1: Establish the transport failure

**Files:**
- Modify: none
- Use: `tools/sfx_probe.py`
- Use: `snesrecomp/runner/src/audio_trace.c`

**Interfaces:**
- Consumes: the debug server command `audio_events` and the existing `sfx_probe.py` classification.
- Produces: a saved transition trace whose nonzero commands are classified as `LOST`, `SEEN`, or `SEEN-NOKON`.

- [ ] **Step 1: Build a debug executable from a matching v0.9.5 checkout**

```sh
git -C ../../.. worktree add --detach .worktrees/v0.9.5-audio-baseline v0.9.5
cd ../../.worktrees/v0.9.5-audio-baseline
git submodule update --init --recursive
cp /absolute/path/to/legal/smw.sfc smw.sfc
bash tools/regen.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Expected: the executable starts with the debug TCP service and the existing audio trace ring enabled.

- [ ] **Step 2: Capture a native-SPC level transition**

```sh
python tools/sfx_probe.py mark "before SPC transition"
# Complete one level transition with MSU-1 disabled.
python tools/sfx_probe.py chain 8000
```

Expected before the fix: at least one nonzero `$1DFB/music` or SFX command is `LOST`; save the command output as the failing reproduction.

- [ ] **Step 3: Capture an MSU-1 level transition**

```sh
SNESRECOMP_MSU1=/absolute/path/to/matching/conn-msu-pack ./build/smw
python tools/sfx_probe.py mark "before MSU transition"
# Complete the same level transition with MSU-1 enabled.
python tools/sfx_probe.py chain 8000
```

Expected before the fix: the transport classification reproduces the same lost command or identifies an engine-level `SEEN-NOKON` failure.

- [ ] **Step 4: Repeat with turbo**

```sh
SNESRECOMP_FORCE_TURBO=1 ./build/smw
python tools/sfx_probe.py chain 8000
```

Expected: preserve the trace as the compressed-timing boundary case.

### Task 2: Add a focused APU transport regression test

**Files:**
- Create: `snesrecomp/tests/runtime_dispatch/apu_port_transition_test.c`
- Create: `snesrecomp/tests/runtime_dispatch/run_apu_port_transition_test.ps1`
- Modify: `snesrecomp/runner/src/snes/apu.c`
- Modify: `snesrecomp/runner/src/snes/apu.h`

**Interfaces:**
- Consumes: `apu_schedulePortWrite(Apu *, uint8_t, uint8_t, uint64_t)` and `audio_trace_on_spc_port_read(uint8_t, uint8_t)`.
- Produces: `apu_applyDuePortWrites(Apu *, uint64_t)` for one sample-boundary drain and a standalone PASS/FAIL executable.

- [ ] **Step 1: Write the failing transition-order test**

```c
/* Queue a transition fade, its music command, and the NMI clear on port 2.
 * Drain each scheduled point and record the SPC observation before advancing. */
failures += check(apu.inPorts[2] == 0x80, "fade visible before music");
audio_trace_on_spc_port_read(2, 0x80);
failures += check(apu.inPorts[2] == 0x23, "music visible before clear");
audio_trace_on_spc_port_read(2, 0x23);
failures += check(apu.inPorts[2] == 0x00, "clear follows music observation");
```

Use `0x80`, `0x23`, and `0x00` only if Task 1’s failing trace has this normal SMW transition sequence; otherwise substitute the exact observed port and values in the test before implementation.

- [ ] **Step 2: Run the new test and verify red**

```powershell
pwsh snesrecomp/tests/runtime_dispatch/run_apu_port_transition_test.ps1
```

Expected: FAIL because the existing immediate queue drains the distinct writes at one produced-sample point, leaving no opportunity for the SPC to observe the earlier command.

- [ ] **Step 3: Expose one-sample queue draining without changing semantics**

```c
/* apu.h */
void apu_applyDuePortWrites(Apu *apu, uint64_t produced_sample);

/* apu.c */
void apu_applyDuePortWrites(Apu *apu, uint64_t produced_sample) {
  while (apu->portQHead != apu->portQTail) {
    ApuPortWrite *w = &apu->portQueue[apu->portQHead & (APU_PORT_QUEUE_LEN - 1)];
    if (w->target_sample > produced_sample) break;
    apu_applyPortWrite(apu, w);
    apu->portQHead++;
  }
}
```

Replace the private queue drain call in `apu_cycle` with `apu_applyDuePortWrites(apu, produced)`, where `produced` comes from `audio_trace_sample_clocks`.

- [ ] **Step 4: Run the test and verify it still fails for the original transport**

```powershell
pwsh snesrecomp/tests/runtime_dispatch/run_apu_port_transition_test.ps1
```

Expected: FAIL. This confirms the harness detects the real collapse rather than merely testing queue plumbing.

### Task 3: Correct the proven command collapse

**Files:**
- Modify: `snesrecomp/runner/src/common_rtl.c`
- Modify: `snesrecomp/runner/src/snes/apu.c`
- Modify: `snesrecomp/runner/src/snes/apu.h`
- Test: `snesrecomp/tests/runtime_dispatch/apu_port_transition_test.c`

**Interfaces:**
- Consumes: trace-proven command sequence from Task 1 and `apu_applyDuePortWrites` from Task 2.
- Produces: ordered, individually observable CPU-to-APU transition writes without environment-controlled shipping behavior.

- [ ] **Step 1: Make the failing test encode the measured target samples**

Use the trace’s `cpu_ap` sample indices to set the exact required schedule points. Distinct commands on one port must have separate apply points with enough interval for the observed SPC poll; identical writes may share a point.

- [ ] **Step 2: Implement the smallest shared timing correction**

Adjust only `RtlApuWrite`’s target selection and the APU queue state necessary to satisfy the measured schedule. Keep an upload handshake write at its current APU sample; delay only a later distinct command that would otherwise replace an unobserved command. Remove `SNESRECOMP_APU_IMMEDIATE_PORTS` as a shipping behavior selector if the corrected path makes it unnecessary.

- [ ] **Step 3: Run the focused test and verify green**

```powershell
pwsh snesrecomp/tests/runtime_dispatch/run_apu_port_transition_test.ps1
```

Expected: `apu_port_transition_test: PASS`.

- [ ] **Step 4: Run relevant framework checks**

```sh
python snesrecomp/tests/run_tests.py
```

Expected: no new test failures.

- [ ] **Step 5: Commit the runner fix**

```sh
git -C snesrecomp add runner/src/common_rtl.c runner/src/snes/apu.c runner/src/snes/apu.h tests/runtime_dispatch
git -C snesrecomp commit -m "fix: preserve audio commands across transitions"
```

### Task 4: Integrate and smoke-test SMW

**Files:**
- Modify: `snesrecomp` (submodule pointer)
- Test: `tools/sfx_probe.py`

**Interfaces:**
- Consumes: the runner commit from Task 3.
- Produces: SMW’s pinned framework revision containing the audio transport fix.

- [ ] **Step 1: Update SMW to the validated runner commit**

```sh
git add snesrecomp
git commit -m "deps: update audio transition fix"
```

- [ ] **Step 2: Build SMW from the updated worktree**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Expected: build succeeds without generated-source or link errors.

- [ ] **Step 3: Repeat the three trace captures**

```sh
python tools/sfx_probe.py chain 8000
```

Expected: SPC normal speed, MSU-1 normal speed, and turbo each report zero `LOST` commands for the transition sequence; music and SFX continue after the transition.

- [ ] **Step 4: Fix any reproduced adjacent blocker**

Add one focused failing test, implement one root-cause correction, and repeat Steps 2–3. Do not modify unrelated code.

- [ ] **Step 5: Commit the SMW integration**

```sh
git add snesrecomp
git commit -m "deps: update audio transition fix"
```
