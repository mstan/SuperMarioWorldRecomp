# Placed-egg visibility and mounted keyholes

Tracking: `beads-8wg.3.30`; contract: [NATIVE_COOP.md](../NATIVE_COOP.md).
The two adapters pass focused acceptance. Full mount and campaign acceptance
remain incomplete; these staged probes do not qualify stock routes.

## Waiting eggs in the expanded view

Original `YoshiEgg` calls `IsSprOffScreen` at `$01:F769`, then branches at
`$01:F76C` before testing the player's horizontal proximity. The horizontal
flag `$15A0,X` still describes the original 256-pixel view and supplies OAM's
ninth X bit. Changing that stored flag would affect rendering consumers.

`mount-abilities/placed-margin-waiting-before` retains the observed failure:
an existing waiting egg at world X=288 remains unhatched beside Mario, with
camera X=19, `$15A0=1`, `$186C=0`, and a zero-use pending source record.
The captured frame shows the egg inside the expanded view.

The renderer hook changes this branch's horizontal visibility
result using the actual selected viewport. It preserves the vertical
`$186C,X` result, OAM flags, original proximity test and hatching routines.
The hook is installed for compiled and interpreted execution; it is inactive
at native width. It applies to ordinary widescreen gameplay as well as co-op.

A newly initialized egg is a poor negative control: its initially clear
offscreen flags can allow a hatch before its first draw updates those flags.
The test therefore starts from an actual waiting-egg save with an intact
source identity.

## A key carried in Yoshi's mouth

Original `$01:E1DB` selects the mounted-mouth branch using `$187A` and
`$191C`, then uses rider clipping at `$01:E1ED`. The on-foot path at
`$01:E1F3` instead requires a status-11 key and uses the key's clipping.
The previous collector filtered candidates to owned hand-carried keys and
entered directly at `$01:E1F3`, excluding the mounted path.

`mount-abilities/mounted-keyhole-before` reproduces that failure in four
execution/view combinations for Mario. Its source contains an actual
swallowed key from the native tongue test, with key status 7 and the native
mount-owned `$191C=1`. No completion or key-availability flag is supplied by
the fixture. The keyhole never starts while the key remains in that mouth.

The collector binds each actor and that actor's own mount globals,
then enters `$01:E1DB` with its eligible held-key slot or `$FF`. Original
riding/key predicates, clipping and hole cooldown decide eligibility. Existing
query hooks still stop before shared scene effects; the shared exit collector
chooses and commits once. The caller's actor and mount bindings are restored.

## Reproduction tools and scope

```text
python tools/test_coop_placed_margin_live.py <CNR8-waiting-egg-state> \
  --exe <candidate> --out <new-directory>
python tools/test_coop_mounted_keyhole_live.py --exe <candidate> \
  --pair-root <mouth-arbitration-output> --out <new-directory>
```

Both tools support explicit old-build failure characterization. It is not
acceptance evidence. The keyhole suite also uses actual two-rider saves with
one occupied and one empty mouth, placing the hole beside each rider in turn.
These are staged contact checks, not stock route or destination acceptance.
Snapshot replay compares equal input intervals, and native/bounce parity
compares the complete WRAM and co-op payload.

The margin harness checks actual contact offsets and viewport dimensions.
Native-width camera motion can bring an initially offscreen egg into view;
that is correct native behavior, not an offscreen activation failure. The
initial twenty-frame negative was invalid for that reason and is retained.
Native captures provide width when adaptive renderer diagnostics are absent.
The corrected left-margin fixture also moves the serialized camera focus,
preserving its measured offset from the RAM camera. Its held focus is an
explicit prerequisite, not evidence for normal camera behavior. A RAM-only
relocation was invalid because the old focus immediately moved the camera.
The native-width left scenario is excluded because the native camera brings
the nearby actor into view; the centered control covers that inactive hook.

## Acceptance evidence

| Evidence directory | Result |
|---|---|
| `placed-margin-focus-baseline` | Fourteen old-build probes reproduce both expanded horizontal gates, while retaining centered and vertical controls. |
| `placed-margin-acceptance` | Fourteen checks pass: both expanded margins hatch, the original-width right gate remains closed, centered eggs hatch, and vertical offscreen eggs remain waiting. Fourteen exact snapshot replays and seven native/bounce comparisons pass. |
| `mounted-keyhole-acceptance` | Twenty-six checks pass: sixteen mounted-mouth exits and ten distant/empty-mouth controls. Both actors, both widths, real one/two-rider mouth snapshots, 26 exact replays and 32 complete-WRAM/co-op native/bounce snapshot comparisons. |
| `mounted-keyhole-foot-{native,bounce}` | Ordinary hand-carried key still produces the original shared keyhole scene: 320 identical actor trace records, 122 clear-phase rows, one secret exit, correct equipment and lives. |
| `mounted-keyhole-goal-{native,bounce}` | A competing ordinary goal loses to the secret keyhole exit; 244 identical actor records retain both candidates, 50 stars and the resulting three shared lives exactly once. |
| `visibility-keyhole-verification.json` | Six original-width control scenarios match the old build's complete WRAM/co-op payload at both saved points; all 27 preserved owner files retain their hashes. |

The passing candidate executable SHA256 is
`013c9f0c3192d01f7a54a6867228f697b05f7be6e63ae8351be0d17b49d99314`.
All launches use isolated saves and muted output, and report zero dispatch
misses. Portable core/serialization tests for rosters 2, 3, 4 and 17 and hook
installer checks pass. These runs do not qualify audio, actual keyhole
destinations, fresh tongue acquisition beside a hole, or campaign play.

Private evidence root:
`build-adaptive/playtest/goal-20261019/mount-abilities`.
The preserved pre-fix binary is `before-margin-keyhole.exe`, SHA256
`f1935409bebd028110f3bbc9ee811f2e4718122b854908d08ed1c767cc5d5a0a`.

The broader 44-case baby regression, against the preserved placed-source
binary, encountered a process deadline during replay in
`placed-combined-baby-regression/release1-wide-native`. Its result remains
failed/unverified. The separate `release1-wide-native-retry1` completed and
passed, and the full 44-case suite finished with replay and native/bounce
parity. The retry uses an explicit longer process deadline and preserves the
original output. Resume rechecks successful evidence only when
executable, ROM, source snapshot and script hashes all match.
