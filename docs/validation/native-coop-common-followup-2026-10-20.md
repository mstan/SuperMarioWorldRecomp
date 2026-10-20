# Co-op alpha: enemies, abilities and common recovery follow-up

## Scope

Owner request, 2026-10-20: improve enemy interactions, abilities and hazards;
give recovery/camera and transitions a rudimentary pass. Use one or two
representative variations per area. Broad matrices, additional boss/bonus
qualification and unusual combinations remain deferred. The normal single
overworld party sprite and normal lead in special scenes are acceptable;
preserving player state when gameplay resumes remains required.

Starting point: `485d960` on `feat/native-simultaneous-coop`, preserved as
`checkpoint/native-coop-before-common-followup-20261020`. The tested alpha
executable and owner data were copied under
`build-adaptive/playtest/goal-20261019/common-followup/preserved/` before edits.
All probes use isolated saves/settings and muted output; the running guest is
never paused. Fixtures position players/enemies and load original ROM tweakers.
Staged geometry and bypassed traversal are not stock-route acceptance.

## Reproduced failures and implementation

### Chuck's custom contacts

The prior adapter selected one nearby actor for each complete sprite update.
Chuck uses custom clipping plus a private stomp counter, bypassing the default
multi-actor response collector. In the retained `before/chuck-side` encounter,
Mario becomes small while overlapping Luigi remains big. In `before/chuck-stomp`,
only Mario bounces; Luigi continues downward and later takes side damage.

The new `$02:C79D` adapter runs the original custom clipping for every playing
actor before responses. Side damage applies per contact. Distance and
primary/stable ties choose one shared attack; every valid stomper runs the
original contact graphics and bounce. Additional bounces use Chuck's native
already-hurt guard after the bounce, suppressing duplicate hit counts/rewards.
The full AI/movement update still runs once. A passive optional WRAM query audit
checks the original clipping prefix.

The existing serialized entity target now records the actual target during
charging Chuck's original windup/run and Thwomp's fall/return. Other enemy
behavior states are not assumed to be committed attacks. Ordinary custom
contacts remain independent of the AI target.

### Independent cape interactions

The original shared cape-contact stage `$02:94F5` saw only the restored primary
actor. With an ordinary Koopa placed within reach, Mario's real Y input executes
the native `$02:9406` response writer and stuns it; Luigi's real Y input produces
a cape swing but no enemy response. The first farther-away setup missed for
both players and is retained as a fixture limitation. The checker was corrected
to accept the original Koopa stun response rather than expecting a kill.

The stage now binds each playing actor's existing cape state in stable primary
order and executes the original enemy/extended-object/block interactions.
Shared sprite and extended-pool motion still runs once. Accepted cape hits join
the existing per-frame shared enemy damage ledger, avoiding duplicate responses
with another accepted attack.

### Common moving platforms and recovery

The original common platform contact routine also ran for one selected actor.
In `platform-before/platform-riders`, Mario stays at native Y=289 on a platform
at Y=320 while Luigi falls to Y=349. In `platform-before/platform-recovery`, the
survivor remains supported but Luigi's ready bubble never finds safe footing.

For the stock `$55..$59` platform family, `$01:B457` now applies native collision,
placement and movement delta to each active actor. Its carry return aggregates
occupancy; platform movement/AI remains a single update. Read-only recovery
recognizes that family's native Y+1 foot surface and ROM clipping bounds. The
complete returning body must fit; other sprites and actors still obstruct it.
Special/chained/falling/reactive-platform combinations remain deferred.

## Selected checks and results

| Sample | Current result |
|---|---|
| Boo watched/unwatched, nearest actor reversed | Pass on preserved alpha: original state 1/0 and selected actors 0/1. |
| Mario and Luigi cape swings | Pass on the enemy candidate: one original cape timer writer and native Koopa stun (status 9) each. |
| Two actors touching Chuck's side | Pass: both power states 1 to 0 through original damage, rather than only the selected AI actor. |
| Two simultaneous Chuck stomps | Pass: both original boosts produce Y speed -48; one shared hit count, primary tie winner, neither takes side damage. |
| Chuck target during windup/charge | Pass: native AI begins windup targeting Mario; loading that real attack with Luigi moved nearer retains target 0. |
| Shared star defeat | Pass: one primary native defeat and one reward event; both players remain alive. |
| Spiny spin-safe bounce | Pass: both original response records show Y speed -48; Spiny remains active. |
| Common moving platform, two riders | Pass on combined build: both native Y=289/standing=1 at platform Y=320; world phase remains 20, matching the original one-update control. |
| Recovery beside a platform survivor | Pass: Luigi returns beside Mario and both remain supported; stationary-survivor camera X remains 0. |
| Water recovery and actual swim input | Pass: both recover swimming/alive; subsequent real B input moves Mario Y=342 to 332 and Luigi Y=340 to 328. |
| Mario pipe / Luigi door | Pass: each native request produces one room transition to `$1CA`, completes the entrance, and retains separate big/fire equipment and reserves. |
| Expanded-margin fire hazard | Pass: audited native contact damages Luigi alone in the adaptive-width margin; Mario remains big. |
| Hammer with Mario's individual star | Pass: audited native contacts damage Luigi and preserve Mario, then the native star response destroys the hammer. |
| One interpreter-path Chuck stomp | Pass on combined build: both bounces and one shared hit, with executed WRAM clipping-query audits. |

The first enemy candidate precedes only the additional platform adapter; its
other code is retained in the combined build. No Cartesian cross-product is
intended. Portable core/state tests, including the new read-only platform
clearance checks, hooks, storage isolation and nonblocking storage queue pass.
Real snapshot preflight passes v9/v10, truncation, malformed schema/mouth and
atomic-staging checks. All accepted live probes finish with stack 511 and no
dispatch misses. Native platform and adaptive hazard captures were inspected;
their staged geometry is not a stock terrain/route audit.

The completed Windows Release executable is
`build-adaptive/SuperMarioWorldSNESRecomp.exe`, SHA256
`22d47a4951281435e538e61b714233f2143767592ecdb7bdb62ecb92a79aa93b`.
The frozen `combined-candidate` used for the final samples is byte-identical to
that artifact after all build/staging steps finish. Those samples began after
linking completed while unchanged launcher assets were still being staged;
the final artifact identity was checked again afterward.

Local evidence root:
`build-adaptive/playtest/goal-20261019/common-followup/`.
`before/`, `cape-control/` and `platform-before/` retain controls/failures;
`enemy-accepted/` retains the seven enemy/ability samples and target continuation;
`combined-accepted/` retains the platform/water/transition/hazard samples;
`chuck-bounce/` retains the interpreted sample and `swim-stroke/` the actual
post-recovery input. `swim-summary.json` records both stroke movements.

The initial checker had three observation mistakes: it required interpreted
IPC names even when the writer was compiled, expected a dead star victim still
to occupy its slot after 90 frames, and inspected only the AI-bound sprite trace
for secondary spin responses. The corrected checker recognizes the exact
compiled writer and uses actual arbitration events for defeat/reward/bounces.
`--analyze-existing` rechecks those retained observations without game launches
or fixture changes; the original checker results are retained separately.

`tools/test_coop_common_alpha.py --out <new-directory> --cases <selected-names>`
retains the exact fixture, original ROM/build hashes, input script, snapshots,
CSV contacts/arbitration, passive writes and one frame capture per encounter.
The arbitration stream is created lazily when a qualifying query occurs; an
absent stream is unavailable for that domain, not evidence of a hit. Cape checks
require the actual native response writer; star/spin checks require their own
executed arbitration rows.
The door sample substitutes an ordinary Map16 door at YI2's existing screen-15
pipe destination; its destination table is untouched. Water recovery uses
staged water tiles. These qualify native mechanics, not full stock routes.

An initial post-edit probe was started before the linker finished and failed
to launch a partial executable (`WinError 193`). It was stopped and retained
under `after-enemies/`; it is a setup failure, not a gameplay result. Accepted
probes must run only after a completed build.

One later build failed because the system temporary drive filled during GCC
assembly output. Compiler temporaries were redirected to the worktree's ignored
`build-adaptive/compiler-temp`; no unrelated files were deleted.

## Delivery and remaining work

The original source ROM, original menus, isolated co-op saves, M/L reserve art,
primary ties and roster-sized core remain intact. No snapshot layout/version or
framework pin changed. The additional native entity target reuses the existing
serialized target field; the real target-retention sample reloads it mid-attack.

The [deferral list](../NATIVE_COOP_DEFERRED.md) remains active for mixed attacks,
other custom contacts and producers, special platform families, combined
recovery geometry, unusual transitions and broad campaign/platform qualification.
This pass adds no new boss/bonus matrix. The known interrupted berry defect
remains deferred. Thwomp's target guard is source-audited but was not an added
live encounter; the retained-target live check qualifies charging Chuck.

Gameplay commit: `eae1bb16883c891693a44d4cf5e4f03aed394d1c`.
Both required Linux core/storage jobs pass at that revision:
[branch workflow](https://github.com/mstan/SuperMarioWorldRecomp/actions/runs/39472812445/job/118800981999)
and [PR workflow](https://github.com/mstan/SuperMarioWorldRecomp/actions/runs/39472818353/job/118801004044).
The final evidence-only update runs the same required checks on
[PR #19](https://github.com/mstan/SuperMarioWorldRecomp/pull/19).

The updated owner build was launched through `tools/run_adaptive_renderer.ps1`
with `--launcher`, retaining `build-adaptive/playtest` as its data directory.
The actual **Super Mario World — Launcher** window is visible, normal bordered,
neither minimized nor maximized. Its ROM-verified and player configuration
panels were inspected through an owned-window `PrintWindow` capture without
changing foreground focus. Audio remains enabled at 32040 Hz; the helper clears
dummy SDL audio/video drivers and passes no muted benchmark options.

`owner-handoff.json` and `owner-ui.png` retain the actual executable hash,
window observation, audio configuration and preservation proof. All **28**
preserved owner files (the previous 27 plus `config.local.ini`) retain their
hashes. The launcher remains available for the owner. PR #19 is updated and
ready for alpha review; no merge was performed.
