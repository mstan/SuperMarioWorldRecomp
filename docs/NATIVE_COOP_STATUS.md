# Native co-op acceptance status

Updated 2026-10-20. Contract: [NATIVE_COOP.md](NATIVE_COOP.md).
Delivery: [PR #19](https://github.com/mstan/SuperMarioWorldRecomp/pull/19),
still draft. Latest validated, pushed milestone: `de2d9ee`.

## Immediate target: alpha first pass

Owner direction, 2026-10-20: test a handful of common interactions, make a
high-level pass through the major systems, and move on. Some bugs in unusual
interactions are accepted for the alpha. Extreme interaction matrices and a
complete campaign audit are deferred. This supersedes the previous requirement
to finish all nine areas below before offering the alpha.

Alpha acceptance uses representative checks for movement/collisions,
pickups/reserves, ordinary Yoshi use, death/recovery, room/stage transitions,
saves/input and presentation/audio. Existing meaningful evidence counts;
repeat or expand checks when a change or observed failure creates a concrete
risk. Keep obvious ordinary-play blockers and save corruption out of the
handoff. Record limited observations accurately and retain failed/unverified
checks. The full product contract remains the direction for later work.

Before the alpha handoff:

- [x] Finish and check the current small berry change with a few common cases.
- [ ] Make a broad representative pass through remaining major systems and
  document visible blockers, limitations and unverified areas.
- [ ] Complete required builds/CI and preserve owner saves/settings.
- [ ] Update PR #19 for alpha review and deliver the normal-window,
  normal-audio owner build.

## Longer-term burndown

Nine full-contract areas remain open. They differ greatly in size, so the
count is not a completion percentage or an effort estimate. Focused fixture
passes qualify their stated mechanics; they do not complete campaign records.
These are the longer-term backlog, rather than nine mandatory alpha gates.
Close an area only when its remaining implementation and evidence are complete.

| Acceptance area | Landed and qualified in focused checks | Work remaining |
|---|---|---|
| Contacts and enemy targeting | Ordinary contact/pickup arbitration, shared stomps/rewards, extended hazards, primary ties | Custom contacts/platforms, mixed attacks, committed targeting, Boos |
| Projectiles and abilities | Per-actor fireballs, direct boss fireball contacts, shell ability matrix | Full cape/star/spin/swim behavior, minor hazards, effect damage and shared pool pressure |
| Mounts | Independent riders/mouths, tongue arbitration, block/placed grants, baby growth, transport/parking, mounted recovery/keyholes | Berry fixes/acceptance, wings destinations, fresh baby/adult competition, source locations and special scenes |
| Recovery and camera | Individual/team death, bubbles, adjacent placement, mounted clearance, widescreen visibility | Slopes/platforms, water/air, vertical rooms, Layer 2, autoscroll and live viewport changes |
| Transitions and progression | Focused pipes, checkpoints, retry, goal/keyhole arbitration and rewards | Every entrance/transition variant, conflicting events, game-over/continue and actual exit destinations |
| Special scenes and presentation | Original reserve artwork/badges, focused native/wide drawing and Mode 7 contact checks | Full boss defeats/targeting, bonus participation, overworld follower, castle/ending, fences/darkness |
| Persistence, input and inert modes | Separate SRAM/state namespaces, atomic preflight, legacy migration, focused replay and solo/disabled controls | Campaign round trips, controller disconnect/reassignment, broad solo/disabled parity |
| Audio health | Reproduced sample-loss/menu-stall fixes, fresh YI2 and post-turbo captured checks | Broader campaign/device/platform qualification |
| Integration and release | Rebase preserving newer main, Windows build, both Linux core/storage jobs | Additional framework PR review, complete platform/package/performance checks, campaign audit, final owner build/UI |

The foundation includes simultaneous Mario/Luigi, separate controls/equipment,
original menus, shared world/session policy and stable roster/entity identities.
Portable core/state tests exercise 2, 3, 4 and 17 actors. Playable input/UI
support remains limited to two.

## Recent completed milestones

| Commit | Result |
|---|---|
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

The first pass will sample common mount, enemy/ability and scene behavior.
The systematic campaign audit is deferred from the alpha. Inventory extraction has
identified **238 room/variant coverage records and 240 screen-exit edges**;
it grants **zero gameplay passes**. See the
[stock inventory](validation/SMW_STOCK_CONTENT.md). Actual mechanic checks must
exercise outcomes and transitions without setting their success flags.

All 27 preserved owner save/settings files match their original hashes at the
latest validated milestone. Final owner handoff and marking PR #19 ready remain
pending the alpha first pass and required build checks above.
