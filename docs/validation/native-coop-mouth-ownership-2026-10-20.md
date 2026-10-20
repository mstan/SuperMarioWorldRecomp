# Yoshi mouth ownership — 2026-10-20

This is a bounded ownership milestone. Full Yoshi sources, abilities, transport,
recovery and contested loose-object arbitration remain required.
Evidence: `build-adaptive/playtest/goal-20261019/mouth-ownership`.

## Reproduced divergences

`native-grab-tongue-before` starts with a natively mounted Mario, Luigi on foot,
and a staged loose key. One continuous `y+p2.y` press makes Luigi grab the key
while Mario extends Yoshi's tongue. The normal-sprite trace retains key status
`$0B`, Luigi's carry flag, and input `$40`. Nevertheless, the passive write log
records `TryEatSprite` assigning key slot 1 to Yoshi's `$160E` at host frame 71.
The key later becomes swallowed status 7 and Luigi loses its ownership.

`contested-key-before` extends both tongues toward a loose key. Mario's mount
claims slot 2 at host frame 71; Luigi's mount claims the **same** slot at frame
72. Both mouth indices remain 2 in the resulting snapshot. These contacts are
one frame apart; this control is not a test of same-frame tie arbitration.

`before-mouth.exe` preserves the exact control executable from `992894b`:
SHA256 `78a64dbfa353014229585fc3c1eb052020f937c24730e16f42cdd46275947cc8`.
The command/config/ROM hashes, screenshots, snapshots and native write logs
are retained per run.

## Source and fix

The original `$01:F524` tongue scan accepts normal-sprite status >= 8,
including carried status `$0B`. `$01:F58E` (`TryEatSprite`) checks clipping and
edibility, then `$01:F5FB` assigns the target slot to the mount's mouth. It has
no knowledge of multiple carriers or mounts.

The new `$01:F58E` eligibility guard returns through native `$01:F609` when:

- The candidate is held/attached to a player other than this mount's rider.
  An unoccupied mount has no player entitled to a carried object.
- Another live native Yoshi already references that object in its mouth.
  This covers tongue retraction and swallowed status-7 objects awaiting
  spit/swallow, including an unoccupied mount's retained contents.

The ordinary native clipping, edibility, tongue motion and swallowing code
remain authoritative for eligible objects. Releasing a carried object restores
its native eligibility immediately; no persistent inedible flag or invented
cooldown is added. Carrier ownership uses stable entity/player IDs. Existing
mouth ownership uses the original per-mount slot references. The scan is bounded
by the ROM's twelve normal-sprite slots, not a two-player roster limit.

## Validation

Reproduce from a copied CNR5 YI2 entrance:

```text
python tools/test_coop_mouth_live.py <entrance-copy> --out <new-directory>
```

The tool stages source Yoshis, runs their original initialization and mounting,
then positions the resulting actors and a loose key. It never sets successful
grab/eat outcomes or completion flags.

`acceptance-fixed` passes **8/8 execution checks** across standard/wide views
and native/bounce paths. Eight complete WRAM/co-op snapshot comparisons match.
Four mouth-state save/replay comparisons also match.

- Luigi keeps the held key through all 40 continuously held input frames;
  Mario's mouth stays empty. After release, the same key is eaten normally.
- One mouth retains the claimed key; the other stays empty. Save/load replay
  preserves that result and the subsequent native timers.
- No crash, timeout or dispatch miss occurs. The owner data remains isolated
  from every test run.

The first split-press probes (`held-key-before`, `held-key-continuous-before`)
were invalid ownership controls: the game runner inserts an idle frame between
presses, releasing Luigi's key. The corrected probe uses one continuous press.
An immediate post-press `savestate` is taken inline by the **game's**
`src/main.c::TickScript`; a delayed save/load consumes an entry. The first
automated replay mixed these forms and compared states one frame apart.
`acceptance-fixed` uses a delayed snapshot for equal replay intervals. Earlier
logs remain available and are not counted as passes.

## Remaining work

This guard does not resolve same-frame nearest/primary arbitration between
mouths competing for a loose object, or repair legacy snapshots that already
contain duplicate mouth references. Those remain open, along with baby-Yoshi
eating, berries, shell abilities, mouth-object transport, mounted keyholes,
and the complete source/save/recovery/campaign matrix. The native mouth indices
must be remapped when transport moves their referenced normal-sprite slots.
