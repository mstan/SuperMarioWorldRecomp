# Native co-op alpha: deferred work

Owner direction, 2026-10-20: prioritize a playable alpha and a high-level first
pass through common interactions. Record corner cases here and move on.
Do not investigate, implement fixes or build new test matrices for these entries
during the alpha pass. Revisit an entry if an observed failure makes it an
ordinary-play blocker or risks saves, or the owner requests it.

**Known defect** means an actual failure has evidence. **Unverified** identifies
a coverage gap, rather than a claim that the behavior is broken. Keep existing
evidence and add concise reproduction notes when a new case is encountered.

Owner follow-up, 2026-10-20: enemy interactions, abilities/hazards and a small
recovery/camera/transition pass are active again, with a few representative
checks. See the [follow-up audit](validation/native-coop-common-followup-2026-10-20.md).
The original single party sprite on the overworld and single lead in special
scenes are accepted; adding a visible second participant is no longer required.

The follow-up qualifies Chuck side/stomp contacts and one committed charge,
two Boo facing arrangements, both cape users, common star/spin/swim/hazard
samples, ordinary `$55..$59` platform handling with a live `$55` rider/recovery
sample, water recovery, and rudimentary pipe/door transitions. D07-D10 below
retain the remaining combinations and special families, rather than erasing
those narrower passes or implying their full area is now complete.

## Corner cases

| ID | Status | Deferred case | Existing reference |
|---|---|---|---|
| D01 | Known defect | A Yoshi body berry bite interrupted by a recovery bubble can remain unconsumed and uncredited after return. | [Berry evidence and reproduction](validation/native-coop-berries-alpha-2026-10-20.md); local `mount-abilities/berry-bubble-before/` |
| D02 | Unverified | A body bite and tongue reaching the same berry in the same frame; ownership of the native berry-created sprite. | [Berry evidence](validation/native-coop-berries-alpha-2026-10-20.md) |
| D03 | Unverified | Every berry color, counter threshold/reward, mouth timing and rider ordering combination. | Same berry report; the three common red-berry checks pass. |
| D04 | Unverified | Fresh baby/adult mouth competition and growth during unusual simultaneous food or ownership changes. | [Baby/growth evidence](validation/native-coop-baby-yoshi-2026-10-20.md) |
| D05 | Unverified | Mount grants, mouth work and room transport under full native sprite-slot pressure and exceptional entrance combinations. | [Placed sources](validation/native-coop-placed-sources-2026-10-20.md), [mount transport](validation/native-coop-mount-transport-2026-10-20.md) |
| D06 | Unverified | Yoshi wings destinations, every stock mount source, unusual mount abilities and their special-scene combinations. | [Mount/ability status](NATIVE_COOP_STATUS.md) |
| D07 | Unverified | Mixed cape/shell/star/fire attacks on the same enemy in the same frame, minor/cluster hazards and shared effect-pool saturation. | [Projectile evidence](validation/native-coop-fireballs-2026-10-20.md), [extended contacts](validation/native-coop-extended-contacts-2026-10-20.md) |
| D08 | Unverified | Exact simultaneous reactive-platform/ON-OFF/custom-contact conflicts and target changes during committed enemy attacks. | [Contact implementation/status](NATIVE_COOP.md) |
| D09 | Unverified | Recovery with combined slopes, moving platforms, water/air, Layer 2, vertical/autoscroll rooms and live viewport changes; every mounted-clearance permutation. | [Recovery evidence](validation/native-coop-mount-transport-2026-10-20.md#ownership-aware-mounted-recovery) |
| D10 | Unverified | Conflicting room/clear requests and all carried-object, bubble, mount, death and secret-exit combinations at transition boundaries. | [Transition contract and evidence](NATIVE_COOP.md) |
| D11 | Unverified | Remaining boss targeting, simultaneous-hit and all-dead timing; every bonus pattern/reward and castle/ending gameplay-state continuity combination. Single-lead scene presentation is accepted. One ordinary Morton defeat/transition and one two-player bonus completion pass. | [Alpha evidence](validation/native-coop-alpha-2026-10-20.md), [full-contract backlog](NATIVE_COOP_STATUS.md) |
| D12 | Unverified | Every fence-side/darkness/Mode 7 overlap and expanded-margin cape-smoke/presentation permutation. | [Presentation status](NATIVE_COOP_STATUS.md) |
| D13 | Unverified | Save/load at every intermediate frame of mouths, bubbles, transformations, bosses, exits and queued rewards; all legacy/corrupt variants. | Existing atomic preflight and focused replay evidence in [the contract](NATIVE_COOP.md) |
| D14 | Unverified | Controller disconnect/reassignment during exceptional transitions, held-input timing and repeated device churn. | [Input/lifecycle status](NATIVE_COOP_STATUS.md) |

## Deferred qualification breadth

- The complete stock-content audit: every stage, sublevel, alternate route,
  normal/secret exit and destination. Retain the [content inventory](validation/SMW_STOCK_CONTENT.md)
  for later work; inventory entries are not gameplay passes.
- Cartesian combinations of actor, backend, aspect ratio, timing and save/load
  for each mechanic. Use a few representative common cases for alpha.
- Broad campaign parity for solo/disabled modes, every supported platform/audio
  device and sustained stress/performance runs. Keep required build/CI checks
  and a small solo/disabled/save-isolation smoke check in the alpha pass.

## Handling new findings

Add an ID, known-defect/unverified status, a plain description and any already
available reproduction/evidence. Update the owning Beads issue and PR limitations
at a meaningful checkpoint. Do not expand the current task to characterize every
variation. A newly observed ordinary-play crash, softlock, loss of control,
progression blocker or save corruption should be fixed with a focused check.

Missing ordinary behavior needed for a playable alpha belongs in the active
first pass. The [full-product backlog](NATIVE_COOP_STATUS.md) also contains
unfinished features; being in that backlog alone does not establish a corner
case or a confirmed bug.
