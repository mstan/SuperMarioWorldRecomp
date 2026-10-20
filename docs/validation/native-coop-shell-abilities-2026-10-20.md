# Independent Yoshi shell abilities

Contract: [NATIVE_COOP.md](../NATIVE_COOP.md); tracking: `beads-8wg.3.30`.
This qualifies the bounded shell behavior below on implementation `aa37c3f`.
It does not complete the mount or campaign contract.

## Native behavior

The original mouth routine at `$01:F14B` derives abilities independently from
the current Yoshi color and swallowed shell. Blue Yoshi or a blue shell supplies
wings; yellow Yoshi or a yellow shell supplies the landing stomp; red Yoshi or
a red shell produces fire when spitting. The actor/mount binding supplies the
corresponding private globals to the original code.

Wing motion uses fresh jump presses in `$00:D8CD..D924`. Holding jump once is
not evidence that repeated flapping works. The world Yoshi routine later
copies `$141E` to `$1410` and clears `$141E` and `$18E7`; zero values in those
end-of-frame transient fields do not establish missing abilities.

The original stomp creates two extended sprites of type `$0F` through
`$02:9BE4/$02:9BF5`. Red-shell fire uses `$01:F27C/$01:F295`, removes the
swallowed shell, and creates three original extended `$11` flames. The test
observes actual native effect writes, movement and saved object state rather
than assigning successful ability flags.

## Reproduction

```text
python tools/test_coop_shell_abilities_live.py <two-separated-riders-state> \
  --out <new-directory>
```

The recorded input is
`build-adaptive/playtest/goal-20261019/mouth-ownership/acceptance-arbitration-first/independent.sav`.
Actors, mount colors and loose shells are staged offline in copies. Tongues
acquire the shells through gameplay input. The script then spits, or performs
repeated jump presses and lands. Both checks reload a retained intermediate
state and repeat their actions.

Evidence:
`build-adaptive/playtest/goal-20261019/mount-abilities/shell-acceptance-first`.
The executable SHA256 is
`751387893a52d12c867a44f770db5319a55ce2e97e641f7563c4587d67206d25`.

## Results

**128 execution cases pass**, covering each actor's sixteen combinations of
four Yoshi colors and four shell colors, both spit and flight/landing checks,
standard and wide views, and native/bounce execution. Partner combinations
deliberately differ. The checks include:

- Actual exclusive shell acquisition by both mouths.
- Removal of each swallowed shell when fire is produced; otherwise ordinary
  kicked-shell status after spitting.
- Three flames per eligible mount, including six simultaneous flames.
- Sustained airborne motion only for the combinations with wings.
- Paired native landing effects only for combinations with stomp ability.
- Both actors remain active throughout every trace.
- 128 complete WRAM/co-op parity comparisons and 128 exact save/load replays.
- Zero dispatch misses, abnormal exits or probe timeouts.

All launches use isolated data and muted output. The retained exploratory
`shell-blue-yellow-flap` and `shell-red-red-first` probes independently establish
the motion and effect-write assertions used by the matrix.

## Still unverified

These tests do not qualify flame or stomp damage against enemies, saturation
of the shared extended-sprite pool, gold-shell combined abilities, berries
acquired through play, wing bonus-room destinations, mounted keyhole
completion, or campaign routes. Those remain separate acceptance work.
