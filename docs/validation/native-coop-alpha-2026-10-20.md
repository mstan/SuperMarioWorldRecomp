# Playable co-op alpha: common-path first pass

Scope follows the owner's 2026-10-20 alpha goal. This is a small first pass
through ordinary behavior, using existing valid evidence and a few additional
checks. The full product contract and exhaustive campaign/interaction coverage
remain follow-up work. See [the deferral list](../NATIVE_COOP_DEFERRED.md).

## Build and integration

- Branch: `feat/native-simultaneous-coop`; starting checkpoint `7744afe`, also
  preserved as `checkpoint/native-coop-alpha-20261020`.
- Gameplay milestone: `3d4d2ea`. The first-pass work described here adds
  reproduction tools and documentation to that runtime.
- Windows Release executable: `build-adaptive/SuperMarioWorldSNESRecomp.exe`,
  SHA256 `3006dcd4a3306a1001445c2a6a9da8788f7398a1167c8aa505fc2c5c267c5c46`.
- Original ROM SHA256:
  `0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b`.
- Main integration remains based on game `8dedb28`, framework `fa018f3` and UI
  `01bff94`. [Framework PR #191](https://github.com/RetroPortingToolKit/snesrecomp/pull/191)
  is a public, available, CI-passing dependency that remains unmerged.

## Common-path observations

| Area | Result and evidence |
|---|---|
| Movement, enemies, items and abilities | Reuse the existing actual contact/pickup, transformation, reserve, projectile and common Yoshi evidence linked from [the contract](../NATIVE_COOP.md). No new matrix was run. |
| Recovery, camera, pipes and ordinary goals | Reuse existing native death/bubble/retry, checkpoint, pipe, goal and overworld reentry checks. An additional current-build owner-state load below shows the survivor moving during an already-started death. |
| Ordinary Yoshi/berry use | Three red-berry checks and one ordinary two-tongue/key regression pass on this executable. [Evidence and limits](native-coop-berries-alpha-2026-10-20.md). |
| Representative boss | Pass: twelve original fire hits from Mario and Luigi defeat Morton; native castle completion sets its actual overworld tile flag and increments exits once; both players return alive with unchanged lives. |
| Solo/inactive mod | Pass: enabled One Player and disabled-mod stock Two Player reach the original overworld, retain normal state storage and leave sentinel co-op SRAM unchanged. |
| Native/widescreen presentation | Native boss/overworld captures and a current 21:9 owner-state capture were inspected. The latter retains the original reserve artwork/M/L badges and shows Luigi continuing to move while Mario's saved death animation runs. |
| Audio | Reuse the captured normal-entry, individual-death, namespace-switch and post-turbo recovery evidence in [the integration report](native-coop-2026-10-20.md). Those fixes remain in this runtime; the ordinary-entry/switch checks recorded sustained audio and zero audible drops/underflows. Broad device/campaign qualification is deferred. |
| Bonus game | Pass: Mario stops three blocks and Luigi five through native head contacts; all eight stop once, shared lives increase once, and the game leaves the bonus and resumes normal room `$103`. |

All accepted new probes run muted in isolated settings/save directories and
complete without dispatch misses. They exercise actual native outcomes.
Position/protection cheats and bypassed prerequisites are identified below.
Capture-heavy throughput runs are not sustained-performance measurements.

## One Morton encounter

`tools/test_coop_boss_alpha.py` uses an existing original-loader entry into room
`$0E5`. Stage traversal is bypassed; it does not set boss health, phase, reward
or completion flags. Both players already have fire power. Short alternating
fire-input intervals refresh only player damage protection. Twelve accepted
original fire-contact responses include both actors, reaching native phase 4.

The final native death/clear/castle scene returns to mode `$0E`, sets translevel
7's tile setting to `$88` and changes `ExitsCompleted` from 0 to 1. Both actors
remain playing/fire-powered, and the shared life count is unchanged.

Local evidence under `build-adaptive/playtest/goal-20261019/alpha-first-pass/`:

- `boss-morton-retry1/shoot-{0,1}/`: actual alternating shots and defeat state.
- `boss-map-context.json`: prerequisite correction for the party's map position.
- `boss-morton-correct-map-transition/`: original scene, watched native flag
  writes and final overworld snapshot/capture.
- `boss-summary.json`: checked outcomes and artifact chain.

Two setup limitations were retained, then corrected: an initial script ended
before its trailing save command, and the copied entry had no corresponding
castle map position. The corrected transition changes only overworld party X/Y
to the stock node derived from the ROM's tile table. It continues the genuinely
defeated state without changing boss health/phase or success fields. The reusable
tool now prepares this map context before the encounter and budgets script
advance frames. These setup attempts are not game failures or acceptance passes.

Reproduce with the original ROM and retained entry state, using a new output
directory:

```powershell
python tools/test_coop_boss_alpha.py --out build-adaptive/playtest/alpha-morton
```

## Small inactive-mode/save-isolation checks

`tools/coop_alpha_solo.script` selects enabled One Player through the original
menus. `tools/coop_alpha_disabled_two.script` selects original Two Player with
the mod disabled. Each uses a new normal SRAM image of 2,048 zero bytes and a
separate co-op SRAM sentinel of 2,048 `$5A` bytes.

Both finish in original overworld mode `$0E`; the native two-player flag `$0DB2`
is respectively 0 and 1. Slot 9 is written in `saves/`, with a normal RTLS v10
header; no co-op slot 9 is created. The co-op SRAM sentinel remains byte-identical.
This is a basic menu/storage smoke check, rather than full alternating-play or
campaign parity.

Evidence: `alpha-first-pass/{solo,disabled-two}/` and
`alpha-first-pass/inert-mode-summary.json`. Run the existing probe runner for
2,460 frames with the appropriate script, `--players 1` or `--disabled`, and
`--normal-sram` / `--coop-sram` pointing to the generated sentinel files.

## Owner-state widescreen sample

`alpha-first-pass/wide-owner/` loads a copy of the owner's F1 CNR5 state at 21:9.
Mario is already dying in the source; this is not a new death caused by the
check. Forty-five frames of right input move surviving Luigi from X=24 to X=83
while Mario continues his original death animation. Lives remain unchanged.
The original owner snapshot/settings are not modified by the probe.

The inspected `frames/frame_000125.bmp` shows the wider scene, Luigi, Mario's
death pose and original reserve art with M/L badges. This sample is not a new
campaign-terrain or all-recovery acceptance claim.

## One bonus game

`tools/test_coop_bonus_alpha.py` starts from the retained native bonus-room
entry. The earlier keyhole/tape fixture bypassed the earned-star prerequisite;
this check qualifies bonus gameplay, rather than earning entry through a full
route. It only positions the players in separate columns, retaining the actual
native standing Y and equipment, then uses held jumps and release intervals.
It never sets block symbols/stops, completion, lives or exit outcomes.

Eight original `$01:DE7C` stop writes cover slots 1 through 8 exactly once.
Native before/after contact responses attribute three stops to Mario and five
to Luigi. Shared lives change from 8 to 9 once at frame 1999 while still in room
`$100`. The game exits the bonus and, with subsequent normal B input, resumes
room `$103` with both players alive. Only this observed pattern/reward is
qualified; every possible bonus outcome remains deferred.

Evidence: `alpha-first-pass/bonus-aligned/` and `bonus-summary.json`. Earlier
`bonus-contact/` and `bonus-held/` attempts are retained as setup limitations:
short taps did not reach the blocks, and passing collision-center Y to a helper
that expects native top-left Y put the players into the floor and shifted them
away from their intended columns. Corrected inputs/positions establish that
native participation already works. No runtime fix was required.

Bonus fields/C2 bytes are reused after changing rooms. The checker therefore
uses original stop writes and live contact/reward observations, and labels
post-exit bonus-only fields unavailable instead of treating the next room's
bytes as bonus results.

```powershell
python tools/test_coop_bonus_alpha.py --room-snapshot build-adaptive/playtest/goal-20261019/alpha-first-pass/bonus-run/saves/native-coop-v1/save9.sav --out build-adaptive/playtest/alpha-bonus
```

## Remaining limitations and delivery

The [deferral list](../NATIVE_COOP_DEFERRED.md) retains the known interrupted
berry bite and unverified corner cases. The overworld follower and other
unfinished special-scene behavior remain alpha limitations in the full backlog.
Boss/bonus breadth, rare simultaneous events and the complete stock inventory
are not alpha gates. No full-product completion claim is made.

## Build checks and owner handoff

The Windows Release artifact above is unchanged by this evidence/documentation
pass. Python probe syntax checks pass. Both Linux core/storage jobs pass at
the representative validation commit `2de950d`:

- [Branch workflow](https://github.com/mstan/SuperMarioWorldRecomp/actions/runs/39460653607/job/118758940379)
- [PR workflow](https://github.com/mstan/SuperMarioWorldRecomp/actions/runs/39460662355/job/118758967084)

The final documentation commit runs the same required checks; the current
results are attached to [PR #19](https://github.com/mstan/SuperMarioWorldRecomp/pull/19),
which is ready for alpha review and remains unmerged.

Owner build:
`F:/Projects/snesrecomp/_wt-smw-native-coop/build-adaptive/SuperMarioWorldSNESRecomp.exe`.
It uses the original verified `build-adaptive/smw.sfc` and the existing owner
data directory `build-adaptive/playtest`. Launch from the worktree with:

```powershell
& ./tools/run_adaptive_renderer.ps1 -DirectRomPath ./build-adaptive/smw.sfc -GameArguments @('--launcher')
```

The actual visible window was verified as **Super Mario World — Launcher**,
neither minimized nor maximized, with a normal bordered window. ROM verification
and both player configuration panels are visible. Audio is enabled at 32040 Hz;
the owner helper clears dummy SDL audio/video drivers, and no muted test options
are passed. The launcher is left idle for the owner's playtest; sound health is
qualified by the captured gameplay evidence linked above.

The final `alpha-first-pass/owner-handoff.json` records the actual executable
path/hash, window PID/title/rectangle and preservation checks. All **27** owner
save/settings files retain their baseline hashes, and `config.local.ini` retains
SHA256 `e2dce8d1880849f8bf5e5f68b202f8bf25622bac3565b6f58cb1e781dbae7cae`.
Existing local input overrides remain intact. `owner-ui.png` retains the visible
launcher image. These private artifacts remain in the ignored evidence directory.

Launch note: redirecting the detached hidden PowerShell helper's native output
through its short-lived parent stalled the first attempt during configuration
logging, before a game window existed. That startup process was stopped and the
same wrapper was launched from a hidden helper without detached stream redirects;
the normal UI then opened immediately. No runtime fix or owner-data mutation was
needed. Future handoffs should use the wrapper directly or keep the helper's
console hidden without those redirects.
