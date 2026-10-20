# Placed eggs and baby-Yoshi food ownership

Tracking: `beads-8wg.3.30`; approved contract: [NATIVE_COOP.md](../NATIVE_COOP.md).
This is a focused implementation milestone. Repeated placed-source grants,
fresh competing mouths, growth/abilities and campaign acceptance remain open.

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
