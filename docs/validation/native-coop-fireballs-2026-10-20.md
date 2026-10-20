# Owned native fireballs — 2026-10-20

This is a focused implementation/validation milestone, not stock-campaign
acceptance. The full contract in `../NATIVE_COOP.md` remains in force.
Local evidence is under `build-adaptive/playtest/goal-20261019/projectiles`.

## Source and ownership

The unmodified US ROM's `$00:FEA8` searches extended slots 9 then 8. Other
extended producers use the shared 7..0 pool. The audited producers include
water bubbles, hopping flame, Yoshi fire/stomp smoke, lava splash, Torpedo Ted
arm, baseball, hammer, Lotus/Piranha fire, cloud coins, Wiggler flowers, Reznor
fire, bones and spin-jump stars. `$01:8EEF`'s shared allocator also uses 7..0.
Direct fireball readers at `$01:D3B1` and `$01:FD0A` require boss adapters.

Each dynamic actor owns the original two-slot allowance. Two is a per-actor
game rule, not a roster limit. Each slot stores all twelve bytes: number,
low Y/X, high Y/X, Y/X speed, Y/X subpixel, age, timer and priority.
Empty-slot residue remains owned: the ROM spawn routine does not reset every
field. Stable projectile IDs share the machine's monotonic entity allocator.

Spawning temporarily binds the firing actor's bank to 8/9, calls the original
routine, captures the result and restores the shared physical bank. At the
original extended-sprite stage, owned slots update through `$02:9B16` in
primary/stable-player order, with slot 9 preceding 8. The shared pool then
runs once. Terrain response, clipping, motion, puff, timer, freeze, fence
priority and shooting sound remain ROM behavior. Scoped calls preserve caller
registers, scratch, sprite index and OAM; each shot/puff has one captured
native picture, latched with the existing player/mount presentation.

Shots remain world objects when their owner dies or bubbles. Room initialization
clears the owned banks. Cross-room/clear/death edge acceptance and the rest of
the ability system are still required; these architectural rules alone do
not establish that every transition works.

## Shared damage

`$02:A0F5` follows valid native fire contact and the fire-immunity check.
Ordinary projectiles have already become puffs there; Yoshi flames retain
their original behavior. A frame-local ledger records the normal sprite's
stable identity, allowing one enemy response. It includes common stomp/spin/
star/slide winners, so a later fireball cannot add another damage event to
the same surviving enemy. Slot reuse receives a new identity.

This prevents Chuck's unguarded `$1528` increment from counting coincident
shots twice. It does not assert coverage of every custom enemy response,
cape attack, shell interaction or boss body-contact path.

Morton/Roy/Ludwig's `$01:D3B1` and Iggy/Larry's `$01:FD0A` directly inspect
the physical bank. Their adapters run only the original fireball-contact
loop per owned bank, then the legacy physical bank once; boss AI is not
replayed. Owned scans start at slot 9, rather than the ROM's extra slot-10
probe outside the ten-entry array. A duplicate valid contact at `$01:D3E9`
or `$01:FD36` retains the puff/timer/bonk writes and skips only the duplicate
hit count or knockback response. Health thresholds and ensuing state changes
remain native.

`SMW_COOP_PROJECTILE_TRACE` records buffered CSV evidence of accepted/rejected
fire contacts, actors, sprite identities, original counters and boundary PCs.
`run_coop_probe.py --projectile-trace` enables it in an isolated muted process.

## Snapshot format and legacy states

CNR version 6 appends `PJT1` after the existing `YSH1` source records:

- Header: magic, actor count, slots per actor, initialized flag.
- Each actor: stable player ID, then two projectile records.
- Each record: stable projectile ID, twelve guest bytes, pending/visible
  counts, followed by those native pictures.

Lengths, counts, allowed kinds (empty/fireball/puff), picture bounds, identities
and complete payloads are validated before replacing a running machine.
Projectile IDs cannot duplicate another projectile or a normal/extended entity.
The ROM-free tests exercise rosters 2/3/4/17, distinct input/player identities,
corruption, every truncation, valid-checksum malformed records and round trips.

CNR2..5 cannot identify the shooters of in-flight physical fireballs. Their
shots continue once in the shared world. Each occupied legacy slot temporarily
reserves that allowance slot for every actor until it expires; no shooter is
invented. Empty residue is adopted into new owned banks. Owner files are not
rewritten as a migration step.

## Reproduction

```text
python tools/test_native_coop.py
python tools/test_coop_preflight.py <copied-v9-CNR5-YI2-entrance>
python tools/test_coop_hooks.py
python tools/test_coop_projectiles_live.py <copied-CNR5-YI2-entrance> \
  --out <new-directory> --legacy <old-CNR5-state-with-two-live-shots>
```

The live suite uses finite real input scripts and native collision responses,
without pausing. It records launch inputs/hashes, final snapshots, frame images,
contact CSVs and a per-check summary. Native/bounce comparisons use complete
WRAM and co-op payloads, not merely matching pictures.

Boss loader fixtures bypass the stage route by choosing native destination
inputs. Ludwig's fixture also teleports to the chamber's normal scroll
threshold. Neither sets boss damage, victory flags or exits. These are
boss-contact checks, not boss defeats or destination-transition acceptance.

## Additional failures exposed

- `morton-entry`, `roy-entry`, `ludwig-entry` failed with the old
  “Invalid native co-op fence side” assertion. `GM12PrepareLevel` deliberately
  sets `$13F9=3` in these rooms. Native drawing uses OAM `$03E0`; capture now
  accepts this fourth selector. All three `*-entry-fixed` reruns load.
- `iggy-entry` and `larry-entry` falsely bubbled Mario while both players were
  visible. The policy read camera `$FF90` as 65424 rather than -112.
  `*-entry-fixed` preserves both active actors. Projectile projection also
  uses the original signed camera difference.
- Early Morton/Roy fire attempts occurred during the real introductory
  freeze and produced no shots. Early Ludwig shots did not reach his
  untriggered chamber. Those `*-fire-first` runs remain as unexercised
  controls, not successful boss damage tests. Later tests wait for the real
  intro or enter the real scroll-trigger area.

## Acceptance status

`acceptance-first` passes 34/34 base checks. `freeze-acceptance` passes 8/8
supplemental checks (including four repeated lifecycle controls): a real feather
pickup freezes all owned projectiles and they resume when transformation ends.
Both suites match full WRAM and co-op payloads across native/interpreter bounce.
`contact-regression` passes 10/10 prior interaction cases and 20/20 trace/image
comparisons. The owner-data verification retains 27/27 preserved file hashes.

Missing checks remain unverified. Broader abilities, custom contacts, room
lifecycle, full boss defeats/transitions, platforms and campaign acceptance
remain required by the product contract. In particular, negative actor
coordinates near the top of a room, Mode 7 recovery placement and combined
boss body/fire damage still need targeted acceptance; the signed-camera
correction above proves only the recorded false-catch-up case.
