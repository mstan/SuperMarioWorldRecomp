# Placed eggs and baby-Yoshi food ownership

Tracking: `beads-8wg.3.30`; approved contract: [NATIVE_COOP.md](../NATIVE_COOP.md).
This is a focused implementation milestone. Repeated placed-source grants,
fresh competing mouths, growth/abilities and campaign acceptance remain open.
The later growth milestone below qualifies bounded feeding/growth behavior.

## Reproduced failures and native boundaries

Evidence root: `build-adaptive/playtest/goal-20261019/mount-abilities`.
`before-abilities.exe` preserves the tested `265d4e5` build, SHA256
`df4a974bffbede74ba793f0d1f24ea369f50dbe94b1c2c05db722a4217f8b6c8`.

The original `InitYoshiEgg` sets the placed-egg flag `$187B,X`. At
`$01:F764`, the egg checks proximity to the bound actor and starts its ordinary
hatching animation. Immediately before `$01:F78A` writes its contents to
`$151C,X`, the ROM selects baby `$2D` or 1-up `$78` using singleton
`YoshiIsLoose`. `placed-before` records the `$78` write at host frame 61
(`$01:F78A`) while only one adult exists. Its final state has collected the
1-up and increased lives from four to five.

The co-op hook replaces that selection with the existing room mount count
against the session roster size. Adults, babies, pending mount eggs and
bubble-owned mounts count; the allocating egg excludes itself. Original
proximity checks, hatching, initialization, motion and alternate rewards still
execute. This does not yet create a second placed-source grant.

Baby food uses a separate original routine, `$02:EA4E`, with its own descending
sprite scan, parity, type and growth filters. `$02:EA8A` performs native sprite
clipping. A successful contact starts `$163E,X = $37`, stores the target index
in `$160E,X` and moves the food toward the baby. Consumption occurs when the
timer reaches `$20`; subsequent smaller timer values retain index residue.

`baby-held-shell-stunned-before` uses actual Luigi pickup and continuous held
input. The baby reserves shell slot 1 while that shell remains status 11 and
owned by Luigi. The pending timer and carrier coexist in the saved state.

The new `$02:EA8A` guard uses the shared mouth-availability rule before native
clipping. Another actor's carried or attached object and another mouth's
reserved food are unavailable. Released food returns to ordinary eligibility.
No cooldown or global inedible bit is added. The shared helper now recognizes
baby reservations with native status 9–11 and a timer above `$20`, as well as
live adult mouths. Adult tongue queries therefore also respect an existing
baby reservation.

Snapshot preflight uses that same active-reservation definition. A pending
baby meal must have a real, in-range, non-self, exclusive food index and cannot
claim another actor's held object. Post-consumption residue is ignored.
There is no snapshot format change.

## Reproduction and results

```text
python tools/test_coop_baby_live.py <unowned-CNR5-YI2-state> \
  --single <one-rider-state> --two <two-rider-state> --out <new-directory>
python tools/test_coop_mouth_preflight_live.py <two-mount-one-mouth-state> \
  --legacy-bad <baby-held-shell-stunned-before/save9.sav> --out <new-directory>
```

The recorded matrix uses `projectiles/allowance.sav`, the mouth arbitration
`setup-0/save11.sav`, and `independent-standard-native/save9.sav` as sources.
All are copied fixtures. Actors, source sprites and equipment are staged
offline. Pickup, release, hatching, eating and rewards execute native routines;
no successful eat or reward flag is injected.

`baby-acceptance-second` passes **44 execution checks**, **40 full WRAM/co-op
snapshot parity comparisons** and **36 exact save/load replays**:

- Standard and wide view, native and bounce execution.
- Placed eggs with Mario riding, Luigi riding, or one existing baby grant a
  baby. The passive write log must show the original contents write as `$2D`.
- Two adults, two babies, adult plus pending egg, or a second mount retained
  in a bubble select native `$78` at the cap.
- Either actor picks up and continuously holds a stunned green shell while
  approaching a baby. At least 40 trace rows must retain its carrier/slot;
  the baby cannot reserve it.
- Releasing that actual held state lets the baby reserve and consume the
  shell, increment its food count once, and award one shared coin. Replaying
  the pending meal reproduces the complete WRAM/co-op state.

`baby-preflight-second` passes ten running native/bounce checks, including the
actual old invalid held-shell state. Rejection leaves the complete guarded
snapshot byte-identical to the missing-slot control.

`solo-parity.json` records six original-mode runs for placed eggs and baby
feeding: preserved checkpoint controls, current co-op-disabled builds, and
current enabled-One-Player builds. All four current complete snapshots match
their respective checkpoint controls exactly. Portable tests for rosters
2/3/4/17, snapshot preflight and hook installation pass. Build succeeds;
every live probe checks dispatch misses, exit status and timeout. All 27
preserved owner files remain unchanged (`verification.json`).

Tested executable SHA256:
`7e822d5c510f60194e74c8fdcbc0b22b2f6bf9fb1a52e97fd93d52d53882e621`.
All launches use isolated data and muted dummy output. Throughput tests do
not qualify audio.

## Invalid exploratory fixtures and remaining work

The first matrix stopped before execution because it incorrectly required
zero entity records; the source has one valid unowned enemy. The corrected
precondition checks absent player ownership and entity flags.
The first preflight invocation supplied two occupied mouths where its
malformed-state builder requires one empty mouth; it also stopped before a
game launch. Both attempts are retained.

Earlier baby probes are not accepted theft evidence: one released its carried
object on the load frame, one used a key rejected by the native edible-type
filter, and one initialized a live Koopa instead of a stunned shell. The
corrected negative control explicitly stages status 9 and proves actual
status-11 carrier ownership before the conflicting reservation.

Remaining work includes repeated placed-source grants, fresh baby/baby and
baby/adult competition, five-food and power-up growth, private growth state,
shell/berry/wing/key abilities, slot pressure and complete stock-content
acceptance. None of these passes follows from the bounded checks above.

## Independent growth and completion of a baby food scan

`before-growth.exe` preserves `182222b` (SHA256 `7e822d5c510f60194e74c8fdcbc0b22b2f6bf9fb1a52e97fd93d52d53882e621`).
`growth-berries-before` reproduces a newborn adult inheriting the nearby
rider's carry flag, color and berry counts. Its source stages seven red and one
pink berry on the existing mount and uses an actually retained key/mouth state.
Both saved mounts incorrectly have those berry counts after native growth.

The ordinary timer-completion path (`$01:A2B5`) and the early meal-completion
path (`$03:C05C`) both change the baby to adult `$35` and call the original
`InitSpriteTables`. The adapter detects that `$2D` → `$35` change at its existing
`$07:F722` initializer boundary and binds fresh mount globals. Native code then
sets the newborn's empty mouth, pose and growth timer. The existing mount keeps
its saved globals. After capturing the newborn, the adapter restores the
actual rider's mount binding, including the actor wing bytes.

Binding fresh globals only at entry to the baby's update was insufficient:
the nested actor-contact adapter legitimately binds player/mount state before
the later growth response. `growth-berries-after` retains that failed attempt.
`growth-berries-init` verifies the corrected initialization boundary:
the old mount keeps `[7,1]`; the newborn has `[0,0]`, no carry flag and its own
growth timer.

A second failure appeared when a baby met another food while already eating
a power-up. The original `$03:C023` finishes the earlier meal immediately,
grows the baby, and returns into the descending food scan. The remaining
lower slots can still reach `$02:EA8A`, now with an adult as the subject.
`growth-acceptance-foods/0-fast-standard-native` preserves the old guard's
fatal rejection of that valid native continuation.

Co-op now recognizes this transition and rejects further **baby** food claims
after growth. The new adult's ordinary mouth update begins on its next frame.
An exploratory permissive continuation (`growth-fast-loop`) demonstrates why
simply allowing further claims is wrong: the new adult retains a baby food
reference to a shell that remains a loose status-9 world object. An original-mode
control (`growth-fast-solo-control`) reproduces that native quirk. The co-op
adapter closes that reservation transition; disabled/One-Player behavior is
unchanged. No generated function or original ROM is edited.

### Growth reproduction and evidence

```text
python tools/test_coop_baby_growth_live.py <one-rider-with-key-slot-1-state> \
  --out <new-directory>
```

Optional `--cases 74 75 76 77 five fast carried` selects a subset. The recorded
source is `mouth-ownership/acceptance-arbitration-first/held-release-standard-native/saves/native-coop-v1/save8.sav`.
The existing rider can be staged as either player; a held baby must be picked
up through actual gameplay input.

`growth-acceptance-slots` and `growth-carried-short` together pass **56 live
cases**, **56 full WRAM/co-op parity comparisons**, and **56 exact replays**:

- Four original power-up food types, with either player as the nearby rider.
- Five non-growth food items consumed through native routines. Passive writes
  must show the food counter advance `1,2,3,4,5`; the first four award exactly
  four shared coins and the fifth causes growth.
- A second contact finishes an earlier power-up meal and grows the baby inside
  the scan; the remaining shell stays loose and the newborn has an empty mouth.
- Either player picks up and carries a baby into food. At least 40 actual
  carrier trace rows precede native growth, and the carried-object identity
  clears. The other mount retains its identity, mouth/key and berry counts.
- Native growth timer starts, survives a snapshot/replay and finishes. The
  new mount starts without the other mount's berry/carry state.
- Every case runs in standard/wide view and native/bounce execution.

Two additional disabled/One-Player runs match the preserved original-mode
control in **six complete snapshots** (`growth-solo-parity.json`). Portable
roster and hook checks pass; all 27 owner files are unchanged.
The tested executable and file hashes are in `growth-verification.json`.

Retained fixture corrections: overlapping stunned shells destroyed one another,
so the five-food test uses five native 1-ups, which follow the ordinary
non-growth food-count branch. Widescreen spawning legitimately reused an eaten
food's slot for a Koopa; assertions now verify removal of the food rather than
permanent slot emptiness. The first carried route ran far enough to trigger
ordinary catch-up; moving the food closer and shortening that route isolates
growth while keeping both actors active.

This milestone does not qualify berries acquired through gameplay, simultaneous
fresh competing mouths, repeated placed-source grants, shell/wing/key abilities,
all growth scene combinations or campaign routes.
