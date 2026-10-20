# Native co-op acceptance status

Updated 2026-10-20. Contract: [NATIVE_COOP.md](NATIVE_COOP.md).
Delivery: [PR #19](https://github.com/mstan/SuperMarioWorldRecomp/pull/19).
The owner accepted this as an **Experimental playable prototype** and authorized
integration into game main, with README instructions and gameplay screenshots.
Latest validated gameplay milestone:
`eae1bb1`; the earlier representative alpha pass is committed in `2de950d`.
The [playable-alpha first pass](validation/native-coop-alpha-2026-10-20.md)
adds representative boss/bonus completion and current inactive-mode/presentation
checks to the existing common-path evidence.

The owner requested a bounded enemy/ability/hazard and recovery/transition
follow-up after that handoff. See the [follow-up audit](validation/native-coop-common-followup-2026-10-20.md)
for reproduced failures, implementation and current validation. This does not
reopen the exhaustive campaign or combination matrices.
The owner accepts one normal overworld party sprite and one normal lead in
special scenes; drawing both participants there is no longer required.

## Immediate target: alpha first pass

Owner direction, 2026-10-20: test a handful of common interactions, make a
high-level pass through the major systems, and move on. Some bugs in unusual
interactions are accepted for the alpha. Extreme interaction matrices and a
complete campaign audit are deferred. This supersedes the previous requirement
to finish all nine areas below before offering the alpha.
Maintain the [corner-case deferral list](NATIVE_COOP_DEFERRED.md) as new findings
arise; do not work those entries during the alpha pass unless they become an
observed ordinary-play/save blocker or the owner requests them.

Alpha acceptance uses representative checks for movement/collisions,
pickups/reserves, ordinary Yoshi use, death/recovery, room/stage transitions,
saves/input and presentation/audio. Existing meaningful evidence counts;
repeat or expand checks when a change or observed failure creates a concrete
risk. Keep obvious ordinary-play blockers and save corruption out of the
handoff. Record limited observations accurately and retain failed/unverified
checks. The full product contract remains the direction for later work.

Before the alpha handoff:

- [x] Finish and check the current small berry change with a few common cases.
- [x] Make a broad representative pass through remaining major systems and
  document visible blockers, limitations and unverified areas.
- [x] Complete required builds/CI and preserve owner saves/settings.
- [x] Update PR #19 for alpha review and deliver the normal-window,
  normal-audio owner build.

## Longer-term burndown

Nine full-contract areas remain open. They differ greatly in size, so the
count is not a completion percentage or an effort estimate. Focused fixture
passes qualify their stated mechanics; they do not complete campaign records.
These are the longer-term backlog, rather than nine mandatory alpha gates.
Close an area only when its remaining implementation and evidence are complete.

| Acceptance area | Landed and qualified in focused checks | Work remaining |
|---|---|---|
| Contacts and enemy targeting | Ordinary contact/pickup arbitration, shared stomps/rewards, extended hazards, primary ties; Chuck custom side/stomp contacts and committed charge target, two Boo facing arrangements, common platform riders | Other custom contacts/platforms, mixed attacks and remaining committed/aimed producers |
| Projectiles and abilities | Per-actor fireballs, direct boss fireball contacts, shell ability matrix; independent native cape contact, common star/spin/swim and hazard samples | Broader ability combinations, minor hazards, effect damage and shared pool pressure |
| Mounts | Independent riders/mouths, tongue arbitration, block/placed grants, baby growth, transport/parking, mounted recovery/keyholes, ordinary body berries | Interrupted bites/rare berry combinations, wings destinations, fresh baby/adult competition, source locations and special scenes |
| Recovery and camera | Individual/team death, bubbles, adjacent placement, mounted clearance, widescreen visibility; common moving-platform and water recovery, stationary survivor framing | Slopes/special platforms, supported air, vertical rooms, Layer 2, autoscroll and live viewport changes |
| Transitions and progression | Focused pipes, checkpoints, retry, goal/keyhole arbitration and rewards | Every entrance/transition variant, conflicting events, game-over/continue and actual exit destinations |
| Special scenes and presentation | Original reserve artwork/badges, focused native/wide drawing and Mode 7 contacts; one actual Morton defeat/castle completion and one two-player bonus completion; single-lead overworld/scenes accepted | Other bosses/targeting and bonus variants, scene state-continuity breadth, fences/darkness |
| Persistence, input and inert modes | Separate SRAM/state namespaces, atomic preflight, legacy migration, focused replay and solo/disabled controls | Campaign round trips, controller disconnect/reassignment, broad solo/disabled parity |
| Audio health | Reproduced sample-loss/menu-stall fixes, fresh YI2 and post-turbo captured checks | Broader campaign/device/platform qualification |
| Integration and release | Rebase preserving newer main, Windows build, both Linux core/storage jobs, tested owner build and normal-window UI handoff | Additional framework PR review, complete platform/package/performance checks, campaign audit |

The foundation includes simultaneous Mario/Luigi, separate controls/equipment,
original menus, shared world/session policy and stable roster/entity identities.
Portable core/state tests exercise 2, 3, 4 and 17 actors. Playable input/UI
support remains limited to two.

## Recent completed milestones

| Commit | Result |
|---|---|
| `eae1bb1` | Bounded follow-up fixes Chuck contacts, independent cape interactions and common platform riders/recovery; Boo/star/spin/swim/hazard and pipe/door samples pass; original single-lead overworld/scenes accepted |
| `2de950d` | Representative alpha pass: both actors defeat Morton with native fire hits and complete its castle; both participate in one bonus/reward/exit; current solo/disabled storage and wide legacy-state checks pass; both Linux jobs pass |
| `3d4d2ea` | Body-berry arbitration: three common checks and one ordinary tongue regression pass; Windows/core/hooks and both Linux CI jobs pass; alpha scope recorded |
| `de2d9ee` | Expanded-margin egg hatching and mounted keyholes: 40 focused cases, replay/backend comparisons and ordinary-key regressions; both Linux CI jobs pass |
| `2dca5d8` | Roster-sized placed-source grants, room history, slot-pressure deferral, atomic preflight and independent child pickup |
| `a0dd6ce` | 128 shell-color/ability execution cases, including independent flames, flight and landing effects |
| `aa37c3f` | Independent baby growth state and completion of food scans |
| `265d4e5` | Ownership-aware mounted recovery and combined rider/mount clearance |
| `439c81e` | Per-rider room transport, no-Yoshi parking, overworld return and retry |
| `c3f83f2` | Simultaneous tongue contact arbitration with current per-player input |
| `992894b` | Per-actor extended hazards/cloud-coin contacts with shared outcomes |
| `a38810e` | Independent native projectile banks and shared fire-hit arbitration |
| `6457cb6`, `7fa5e8e` | Namespace-write/audio-stall and producer-pacing fixes |
| `da6bd91` | Ordinary simultaneous native contacts and shared transformation ownership |

Detailed counts, negative controls and limitations are in the milestone reports
linked from the contract.

## Latest alpha change and next work

One physical berry previously credited both Yoshi counters on the same frame.
The body-claim fix now passes three common checks: single rider, primary tie,
and two separate berries. Duplicate pending bites are rejected by snapshot
preflight. See the [alpha evidence and known limitations](validation/native-coop-berries-alpha-2026-10-20.md).
An interrupted bite during bubble recovery can remain unconsumed/uncredited;
that unusual interaction is deferred with reproducible evidence.

The first pass samples common mount, enemy/ability and scene behavior. One
Morton encounter reaches actual defeat/castle completion and overworld return;
one bonus game includes both players, one shared extra life and resumed normal
play. Current solo/disabled-two-player checks retain stock mode flags and normal
storage while preserving sentinel co-op SRAM.
The systematic campaign audit is deferred from the alpha. Inventory extraction has
identified **238 room/variant coverage records and 240 screen-exit edges**;
it grants **zero gameplay passes**. See the
[stock inventory](validation/SMW_STOCK_CONTENT.md). Actual mechanic checks must
exercise outcomes and transitions without setting their success flags.

The original alpha handoff preserved all 27 owner save/settings files and the
additional local input override. The current follow-up build is SHA256
`22d47a4951281435e538e61b714233f2143767592ecdb7bdb62ecb92a79aa93b`;
its [follow-up report](validation/native-coop-common-followup-2026-10-20.md)
records the focused checks and refreshed delivery. The
[prototype presentation record](validation/native-coop-prototype-2026-10-20.md)
documents experimental labeling and the README captures. The owner handoff
uses a normal window and normal audio; all 28 files in that preservation
checkpoint were unchanged. Full-contract work remains open after prototype
integration.
