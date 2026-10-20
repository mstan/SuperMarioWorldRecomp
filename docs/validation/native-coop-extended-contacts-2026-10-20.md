# Shared extended-object contacts — 2026-10-20

This milestone covers native extended-object contact mechanics. It does not
qualify their producers, full stages, or campaign routes. The complete contract
in `../NATIVE_COOP.md` remains open.

Local evidence: `build-adaptive/playtest/goal-20261019/extended-contacts`.

## First divergences

At checkpoint `a38810e`, `secondary-before` places a stock type-2 Reznor
fireball at (96,360), Mario at (40,352), and Luigi at (88,352). After two
scripted waits both players remain big. `primary-control` swaps the players'
positions; Mario correctly shrinks and starts animation 1. The projectile
position and original clipping geometry are otherwise identical. Neither
run has a dispatch miss.

The shared extended loop runs once with primary actor WRAM bound. Its
`$02:A3F6` contact routine therefore could not see Luigi.

After adding all-actor contact queries, `coin-first` exposes a second divergence:
Luigi's real cloud-coin response increments `$18E3`, but restoring Mario's
actor image replaces that counter with zero. The old actor image incorrectly
owned the shared game-cloud challenge counter.

## Original boundaries and implementation

`$02:A3F6` compares the actor's scenery/fence plane with the extended sprite's
priority, calls the original player and extended clipping routines, then the
original overlap test. `$02:A40B` is the first accepted-contact instruction,
before any effect. `$02:A468` is the balanced return.

The adapter queries each active actor, stopping at that accepted boundary.
Optional query audits compare all WRAM; only the native scratch bytes and
observed call-stack area may change. No damage, reward, sprite movement,
gravity, timer or graphics stage runs during a query.

Contacts are collected before any response:

- Every vulnerable actor runs the original damage/mount response against the
  still-original hazard. Invulnerability and transformations retain the
  original `HurtMario` checks.
- A star contact destroys the shared hazard once, after the other contacts
  receive their valid responses. Multiple star contacts select the closest
  actor, with primary/stable ID breaking exact ties.
- A cloud coin has one collector, selected from the original eligible
  contacts by distance and primary/stable ID. Its native reward, counter,
  sparkle, score popup and erasure run once.
- Mounted damage binds that mount's private globals, then captures them and
  releases ownership after the native dismount. Stable IDs are resolved again
  after the call because a reserve drop may allocate a normal sprite.

Only the contact subroutine is repeated. The caller's world motion, timers
and graphics still execute once. Candidate storage scales with the roster;
there is no two-player simulation array. Caller registers, scratch and actor/
mount binding are restored around the adapter.

The original shared routine serves Reznor fire (`02`), flame remnants (`03`),
hammers (`04`), bones (`06`), cloud coins (`0A`), Piranha fire (`0B`), Lotus
fire (`0C`) and baseballs (`0D`). The unused `09` also calls it, but has an
additional bespoke screen-coordinate contact later in its routine; that
unused behavior is **unverified**. Wiggler flowers (`0E`) explicitly bypass
the coin contact branch and remain decorative.

### Shared cloud challenge and snapshot compatibility

Source ownership is explicit: the game cloud initializes `$18E3` at
`$02:EEB5` and reads it at `$02:EF67`; cloud-coin contact increments it and
`HurtMario` resets it. It belongs to the shared world.

The old actor image byte remains at its existing offset as a legacy mirror.
`coop_guest_bind` never writes that byte to WRAM. Its serialized size and
layout hash therefore remain unchanged for CNR2–6. The guest WRAM snapshot
is authoritative. This fixes the ownership source for every actor binding,
including cloud AI and damage, rather than only copying the coin result.
It cannot retroactively recover progress already lost in an older build.

## Validation and reproduction

Run using a copied version-5 YI2 entrance with no mounts:

```text
python tools/test_coop_extended_live.py <entrance-copy> --out <new-directory> --mounted <two-mounted-copy>
python tools/test_native_coop.py
python tools/test_coop_hooks.py
```

The live runner stages positions and hazards offline, then uses finite scripts
and original collision/response code. It retains snapshots, buffered contact
traces, positive query-audit counts, images and run reports. The fixture never
sets a successful contact, damage result, coin count or completion flag.
`--cases` selects a bounded subset for an affected rerun.

Checks include each stock contact family; primary/secondary/dual hits;
individual/team lethal hits; mixed and simultaneous stars; invulnerability;
scenery-plane mismatch; bubbles; exclusive cloud-coin ties/proximity; mounted
dismount ownership; and actual left/right widescreen margins. Standard and
widescreen runs exercise both normal and forced-bounce execution. Comparisons
use complete WRAM and serialized co-op payloads. Primary-only and dual damage
also compare every shared extended-table byte to reject per-actor world updates.

The first attempt's `fence-standard-native` setup was invalid: setting
`PlayerBehindNet=1` on ordinary YI2 terrain causes native `$00:EA32` terrain
handling to clear it. That result does **not** establish a fence failure or
pass. The replacement scenery-plane predicate check uses plane 2; real fence
climbing/flipping acceptance remains unverified.

The cloud-coin observer follows the actual `$05:B34A` bookkeeping: it queues
`CoinAdder` first, and the HUD stage applies that credit later. Assertions
count both the pending and applied shared coin credit.

### Results on the combined build

- `acceptance-final`: **88/88 execution checks passed**, with **44 complete
  WRAM/co-op snapshot comparisons** matching between native and bounce modes.
  Every run reports no dispatch miss, crash or timeout. All accepted contacts
  have positive query-audit counts.
- `margins-fixed`: 8/8 focused margin/mounted checks and their comparisons
  passed before the final matrix; these overlap its coverage.
- `fireball-regression`: 8/8 allowance/lifecycle and actual feather-freeze
  checks pass, with complete WRAM/co-op snapshot parity.
- ROM-free dynamic-roster tests (2, 3, 4 and 17), snapshot corruption/rejection,
  ownership checks, legacy/current snapshot preflight and hook coverage pass.
  The build contains 60 compiled hook sites and four explicit interpreter sites.
- `regression-summary.json`: all **27 owner save/settings hashes unchanged**.

Earlier outcomes remain available. `acceptance-first` stops at the invalid
fence fixture. `acceptance-counter` passes 41 checks, then its first left-margin
fixture fails offline construction because it attempted to encode a negative
unsigned actor X. The fixture now places the distant control actor to the
right when necessary; the complete corrected matrix passes. Neither fixture
error is reclassified as an engine pass. The first preflight command omitted
its required source argument; `preflight-fixed.*.log` records the corrected,
passing invocation.

## Remaining coverage

This is not acceptance of the cloud's ten-coin reward sequence, projectile
producers/aiming, full fence behavior, every mounted mouth/ability state,
cluster/minor sprites, custom normal-sprite responses, or stock campaign
terrain/routes/clears. Those remain on PR #19's full checklist.
