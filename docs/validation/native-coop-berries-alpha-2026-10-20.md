# Body berries: alpha check, 2026-10-20

One red Map16 berry could credit both mounted Yoshis. The preserved old build
shows two original `$18D4` reward writes for tile `$C963` in the same frame.
The single-rider control awards one berry. Evidence is retained locally under
`build-adaptive/playtest/goal-20261019/mount-abilities/berries-before/`.

Accepted native body contacts at `$02:D1CA` are now collected before committing
a bite. Each physical tile chooses the nearest eligible actor, then the
primary/stable player order. The winner executes the original animation,
delayed tile removal and reward routine. Another mount's pending body bite
also excludes a later body/tongue claim of that tile. Snapshot preflight rejects
legacy duplicate pending body bites atomically; existing serialized mount
coordinates, berry type and native timer provide the reservation state.

## Common-interaction check

Following the owner's revised alpha scope, this milestone runs only three
berry cases with the normal execution path at native width:

| Check | Result |
|---|---|
| One rider eats a red berry | Pass: one counter increment; tile becomes stock tree tile `$49` |
| Both riders reach the same berry | Pass: Mario receives one increment; Luigi receives none; tile changes once |
| Riders eat separate berries | Pass: each receives one increment; both tiles become `$49` |

All three processes exit successfully without dispatch misses. Copied encounter
states and actual Map16 berries provide the inputs; the harness does not set
eating timers, counters or reward flags. These are focused interactions, rather
than acceptance of an entire room or the full Yoshi contract.

One ordinary two-tongue/key regression also passes on this build: one original
key enters Mario's mouth, Luigi's remains empty, and there is one committed
claim. Evidence is in `goal-20261019/alpha-first-pass/tongue/behavior.json`.
Portable core/atomic reservation checks and compiled/interpreter hook checks
pass. The Windows build completes successfully.

Reproduction from the repository root:

```powershell
python tools/test_coop_berries_live.py --exe build-adaptive/SuperMarioWorldSNESRecomp.exe --out build-adaptive/playtest/goal-20261019/mount-abilities/berries-alpha-smoke --cases single-p1 tie separate --views standard --execution native --expect pass
```

The output directory must be new. Inputs, checksummed snapshots, scripts,
captures, reward-write observations and `observations.json` remain in that
directory. Tested executable SHA256:
`3006dcd4a3306a1001445c2a6a9da8788f7398a1167c8aa505fc2c5c267c5c46`.
The default harness selection is these three cases on one view/backend.
Additional diagnostic options have not received alpha qualification.

## Known limitations and deferred work

- A pending body bite interrupted by a waiting/recovery bubble can strand the
  bite: the native `$1564` timer expires while its Yoshi routine is skipped,
  leaving the berry unconsumed and uncredited after recovery. The actual pending
  state, waiting run and uninterrupted control are retained in
  `mount-abilities/berry-bubble-before/`. This unusual interruption is deferred
  under the alpha scope; ordinary uninterrupted bites pass above.
- Same-frame body-versus-tongue priority, all berry colors/reward thresholds,
  berry-created sprite ownership and every player/view/backend permutation
  remain unverified. A tongue can remove a tile during the native draw loop;
  the later body resolver rechecks that the original berry still exists.
- Full stock-source and campaign coverage remains deferred. Existing broader
  Yoshi evidence is retained for targeted follow-up.
