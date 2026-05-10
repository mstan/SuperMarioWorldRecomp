# CODEX Analysis

## Debug Loop

- Branches: `codex/smw-evidence-debug` in both `SuperMarioWorldRecomp` and `snesrecomp`.
- Avoided visual pause/step loops after the stutter report. The useful workflow is to run continuously, pause once after the window of interest, and query history rings.
- Added TCP debug controller commands so scripted input does not depend on `--script`:
  - `set_controller <buttons|0xmask> [p2]`
  - `get_controller`
  - `clear_controller`
- Added history queries for sprite state over time:
  - Recomp: `sprite_timeseries <slot> [from] [to] [changes_only=1] [limit=512]`
  - Oracle: `emu_sprite_timeseries <slot> [from] [to] [changes_only=1] [limit=512]`
- Added frame-tagged CPU trace events and `trace_get_v2 frame_lo=... frame_hi=...` filters so WRAM writers can be attributed without frame-stepping.

## Attract Demo Koopa Slope Divergence

Observed bug:

- In the recomp attract demo, the shellless Koopa near the slope did not slide down the slope.
- Mario/Yoshi stepped on it instead of bouncing off the sliding Koopa, causing the demo state to diverge.

Evidence before the fix:

- Recomp sprite slot 9 spawned as sprite `BD` at frame 754.
- Its Y speed became `05` at frame 759 and then stayed clamped at `05`.
- It never acquired the expected slope/block state before being killed.
- Oracle sprite slot 9 spawned later in its frame timeline, accelerated normally, hit the slope with `bl=04 sl=fd`, slid down, and was killed at the expected slope position.

Root cause:

- Cross-bank callers in bank 02/03 named `$01:802A` as `HandleNormalSpriteGravity`.
- `$01:802A` is actually the `UpdateSpritePos` wrapper:
  - `PHB`
  - `PHK`
  - `PLB`
  - `JSR $9032`
  - `PLB`
  - `RTL`
- The real gravity/body code starts at `$01:9032`.
- Generated bank 03 code skipped the wrapper and called the body directly while `DB` was still the caller bank (`03`).
- Absolute gravity/cap table reads at `$01902E/$019030` therefore read bank 03 data instead of bank 01 data, producing the bogus Y-speed clamp.

Fix:

- Added `func UpdateSpritePos 802A sig:void(uint8_k)` to `recomp/bank01.cfg`.
- Renamed cross-bank `name 01802a` entries in `recomp/bank02.cfg` and `recomp/bank03.cfg` from `HandleNormalSpriteGravity` to `UpdateSpritePos`.
- Regenerated ignored V2 output locally with `snesrecomp/tools/v2_regen.py`.
- Refreshed ignored `recomp/funcs_v2.h` locally with `snesrecomp/tools/v2_sync_funcs_h.py`.

Evidence after the fix:

- Recomp slot 9 now accelerates as `03, 06, 09, 0c, 0f, 12, 15, 18, 1b`.
- It reaches the slope with `bl=04 sl=fd`, slides down left, and is killed at the same position/speed pattern as the oracle, shifted by the existing attract-demo frame offset.
- WRAM writer trace shows gravity writes running with `DB=01 PB=01`, confirming the wrapper is no longer skipped.

## Validation

- `MSBuild.exe smw.sln /p:Configuration=Oracle /p:Platform=x64 /m /v:minimal`: passed with existing unreferenced-label warnings.
- `python snesrecomp\tests\test_attract_demo_regression.py`: passed.
- `python snesrecomp\tests\v2\run_tests.py`: 134/137 passed. The three failures are existing V2 ABI-shape expectations unrelated to this fix.
- `python snesrecomp\tests\l3\_probe_state_sync_correct_lockstep.py`: command passed; it still reports the known GM07 raw/meaningful diffs from the existing baseline.
