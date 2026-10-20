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

## Current input at the mouth stage

The actual game loop (`$00:A2A9`) calls `DrawMarioAndYoshi` **before**
`AdvancePlayerPosition` and `$00:C47E/$00:C569`. The latter is where the
co-op actor-motion adapter previously supplied each actor's current input.
The primary's WRAM already had decoded controller input at draw time, but a
secondary actor image still contained the prior frame's buttons.

`input-before` records original `$01:F30B` writes starting Mario's tongue at
host frame 62 and Luigi's at frame 63 for the same input press. The preserved
`before-mouth-input.exe` is checkpoint `9efb62c`, SHA256
`5a525a80a2802736f175115d4f1e791e98ffcce7c26a61b08ddac1133d121e99`.

`draw_mounts` now binds current held/pressed input from the actor's existing
input lifecycle before the original mouth call. It does not create another
input edge or another mouth update. `input-after` records **both** original
timer writes at frame 62.

`acceptance-input` passes the eight ownership/execution checks, eight complete
WRAM/co-op comparisons and four save/replays again. The test now additionally
requires both original tongue-start writes on the same frame in each of its
four two-mounted executions. This establishes equal input timing; it does not
establish correct arbitration when two tongues reach an object together.

With timing corrected, `reverse-owner-tie-before` swaps the two mounts' player
owners while leaving the actors and tongue geometry coincident. Luigi's
earlier native slot takes the key. This control exposed the primary-tie failure
resolved by the following adapter.

## Simultaneous tongue arbitration

`$01:F59B` is the first instruction after the original clipping check succeeds.
For edible objects, the adapter records the accepted native CPU/scratch context
and returns through `$01:F609`, allowing the remaining mouths to query the same
world state. Inedible contacts retain the original bonk-and-continue response.
Each mount's motion, drawing and tongue timers still execute once.

After all mouths have updated, the resolver considers objects in the ROM's
descending sprite-slot order. For each object, eligible mouths compete by
actor-to-object distance, primary/stable actor order, and stable mount identity.
A mouth that wins one object cannot win another in that frame. Losing mouths
can still take another contacted object. Rechecking live stable identities,
status, carrier ownership and existing mouth ownership prevents committing a
stale candidate.

The winner resumes the **original** response at `$01:F59B` with its captured
registers and clipping scratch. Its already-returned query stack is replaced
with the enclosing draw caller's stack before a fresh scoped JSR. The ordinary
return-balance check remains active. Mount/actor state is bound and captured
around the response. Pokey can allocate a segment, so the resolver reacquires
the winning mount by stable ID afterward. The collected array grows dynamically;
the twelve-object scan is the original normal-sprite capacity, not a roster limit.
Candidates exist only inside the frame; no snapshot format change is required.

The preserved `before-mouth-arbitration.exe` is `cb7dc70`, SHA256
`c415970b9c36e764b55d5b8e68ff942e675b769ec3dfdefe54311a0210074346`.
The tested candidate is SHA256
`2127afb2a2f47c03387cc45331fd6c2d704a86676bc6fb8bc070594f0a6e4b53`.
`SMW_COOP_MOUTH_TRACE` / probe `--mouth-trace` records accepted candidates,
selected commits and completed native responses with stable IDs and distances.
This buffered diagnostic does not pause the guest.

`acceptance-arbitration-first` passes **24/24** checks:

- A partner's continuously held key remains protected and becomes edible after
  release.
- Equal simultaneous contacts favor Mario, including reversed mount slots.
- A closer Luigi wins over Mario when both original clips succeed that frame.
- Separated mouths claim their respective objects independently.
- Two coincident mouths contacting two coincident objects obtain one each,
  preserving the original descending object scan.

Standard/wide and native/bounce runs match in **24 full WRAM/co-op snapshot
comparisons**, with **20 exact save/load replays**. Every two-mounted run also
requires both original tongue-start writes in the same frame. There are no
dispatch misses, crashes or timeouts. Portable core checks for rosters 2/3/4/17
and the hook installer checks pass; 27 owner files still match the preserved
hashes (`arbitration-verification.json`).

The Pokey fixture initially used a key's origin height, placing its 80-pixel
body below the floor. Its native offscreen removal invalidated the later
live-parent assertion (`acceptance-pokey`); this is not a passed terrain test.
`pokey-footing` corrects only the initial position to y=304 on the y=384 floor:
the original initialization creates five segments, Mario wins the coincident
tie despite owning slot 1, and Luigi removes a distinct segment on the next
frame. Both original spawned segments are consumed and the parent retains
three segments. This exercises the native allocation response, not a fabricated
successful eat or defeat.

`acceptance-pokey-footing` passes all four standard/wide/native/bounce runs,
four full WRAM/co-op comparisons and four save/load replays. Combined with the
key cases, this milestone has **28 execution checks, 28 parity comparisons and
24 save/load replays**. These are focused adapter checks, not campaign coverage.

## Remaining work

Legacy snapshots that already contain duplicate mouth references still need
validation/handling. Other open work includes cross-ability outcomes, baby-Yoshi
eating, berries, shell abilities, mouth-object transport, mounted keyholes,
and the complete source/save/recovery/campaign matrix. The native mouth indices
must be remapped when transport moves their referenced normal-sprite slots.
