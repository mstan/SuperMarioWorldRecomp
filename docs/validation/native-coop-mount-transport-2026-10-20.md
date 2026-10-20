# Native mount entrances and parking

This is a bounded implementation milestone. The complete mount, transition,
recovery and campaign contract remains open.

## Original-code boundary

The original `$02:A751` loads shared room sprites once, then calls
`SpawnMountedYoshiOnLevelLoad` (`$00:FC7A`) for the singleton carry flag.
That routine chooses a native sprite slot, applies the entrance position and
rider offset, initializes Yoshi, restores its color and empties its mouth.
The original room cleanup does not transport swallowed objects.

The retained solo control verifies that behavior through the actual YI2 down
pipe into room `$1CA`. The co-op control on `663fab4` loses Luigi's mount.
It keeps Mario's mount only, with a new identity.

The adapter now:

1. Retains each departing rider's stable mount ID and native color before
   shared cleanup retires physical slots.
2. Executes `$02:A751` once with its singleton mount spawn suppressed.
   Shared sprites and the existing held-object transport still run once.
3. Calls the original `$00:FC7A` separately for every eligible transported
   mount. Its preferred slot is kept when free; another free native slot is
   selected before `$00:FC98` if that preference would overwrite an object.
4. Rebinds the original stable identity and owner to the newly initialized
   sprite. Native mouth clearing and entrance offsets apply individually.
5. Preserves each actor's prepared entrance image through loading/fade-in.
   Initializing gameplay no longer replaces all of them with Mario's image.

The three new hook sites are in `coop_hooks.def`; generated bank code is not
hand-edited. Scoped calls retain shared clocks/bus state, restore caller
registers/scratch and verify native return-stack balance.

Twelve is the original normal-sprite capacity, not a roster size. Full physical
slot pressure and special room allocation still require campaign qualification;
the adapter currently fails explicitly if no slot is available.

## No-Yoshi entrances

The original castle/ghost-house script dismounts a rider, then reloads the
playable room with `RemoveYoshiFlag` set. The initial adapter mistakenly treated
that automatic dismount as a player abandoning Yoshi. Its ownership association
now remains until the loader parks it. Native C2/riding bytes still control
the dismounted movement and pose.

Every actor receives the mounted or on-foot entrance script according to its
own mount. While prohibited from entering, a parked mount has an ID and color
but no native sprite slot. The original primary carry flag reflects its parked
mount when the overworld reads it. A later permitted entrance reconstructs each
mount through the original routine. A failed team attempt clears all parked
mounts as well as active owned entities.

The focused castle test uses original primary-loader override `$0109=$25`,
selecting stock room `$101`; it does not force the no-Yoshi result. Native input
drives the complete introduction. For the return test, the completed-level
prerequisite is explicitly bypassed to allow Start+Select. The original
overworld and subsequent B-button entry then restore the mounts in YI2.
This does not establish a castle defeat, a completed overworld route, or audio
correctness; the override affects native music selection.

## CNR7 snapshot records

`MTR1` follows the CNR6 projectile records:

| Field | Encoding |
|---|---|
| Header | Magic, roster count, prepared-entrance boolean, zero reserved word |
| Actor record | Stable player ID, parked mount ID (zero means absent), native color |

The record count is dynamic. Checked sizing, exact roster IDs, flags, reserved
bits, color range and complete lengths are validated before replacing state.
A parked identity cannot alias another actor's parked mount, a world entity,
a projectile, or the actor's active mount. CNR2–6 loads default these new
fields to absent/false; no old secondary mount ownership is invented.

The portable tests exercise rosters 2/3/4/17, reordered/noncontiguous IDs,
round trips, malformed records, aliases and CNR6/CNR5 compatibility.
Live replay includes snapshots taken after the room loader prepares distinct
actor images but before gameplay initialization, plus parked and overworld
states.

## Reproduction and evidence

Local evidence root:
`build-adaptive/playtest/goal-20261019/mount-transport`.
Every probe has isolated settings/saves, source hashes, scripts, logs and
passive traces. Launches use dummy output; no runtime is paused.

```text
python tools/test_coop_mount_transport_live.py <two-rider-YI2-state> --out <new-directory>
python tools/test_native_coop.py
python tools/test_coop_preflight.py <legacy-v9-CNR5-YI2-state>
python tools/test_coop_hooks.py
```

The first input is the saved `independent` case from
`test_coop_mouth_live.py`. The tool gives the mounts distinct colors and
creates single-rider variants through actual dismount input. It stages only
positions/equipment and the documented loader/prerequisite bypasses.

`acceptance-loader` passes **60/60 execution checks**, **66 complete WRAM/co-op
snapshot parity comparisons**, and **60 save/load replays**:

- Both riders, Mario only and Luigi only.
- Either player entering the actual YI2 pipe; one shared transition.
- Correct stable identities, independent colors, native mouth clearing and
  mounted/on-foot arrival states.
- The original no-Yoshi intro, parked ownership, actual early exit, overworld
  storage and native reentry.
- An actual timer expiry, one shared life charged, small actors on retry and
  no mount retained from the failed attempt.
- Standard/widescreen and native/bounce execution.

`held-continuous-native` and `held-continuous-bounce` additionally preserve
both actors' held keys through the pipe, including their native slot relocation,
equipment and reserves. More than 100 arrival rows per carrier retain stable
identities; complete WRAM/co-op snapshots match. The initial `held-pipe-*`
scripts released Y between adjacent `press` entries, legitimately dropping
the keys. The corrected script holds Y continuously. The old standalone
checker also assumes five starting lives; this fixture starts with four.
The retained verification checks four unchanged lives, not that stale constant.

`solo-after` (disabled mod) and `one-player-after` (enabled mod, One Player)
produce **byte-identical entire snapshots** to `solo-control`, SHA256
`32d24dcbb65029e17962dfd7dcd87004761a17f153bced21b55bf5c8249479c8`.
Portable core, legacy/current preflight and hook checks pass. All 27 owner
save/settings hashes remain unchanged (`verification.json`).

Control executable `before-transport.exe` (`663fab4`) SHA256:
`ad0e85d42fae6d6c6200bca31efd9c6a3e8839d8c87202c62f4e6b55be438df5`.
Tested candidate SHA256:
`a98d42a06540dfd4222732c3f24c7e98dcfaec8425b9015b8504eb80912208d0`.

Retained unsuccessful investigations:

- `castle-first`: staging override retained the preceding room's camera.
  `castle-origin` corrects its position and exposes the scripted-dismount
  ownership loss. `castle-park` verifies the ownership correction.
- `acceptance-first`: retry cleared the staging override while the real
  overworld node was still YI2. The second native intro load correctly used
  that node. `acceptance-loader` keeps the castle override for the staged
  retry and removes it only for the explicit overworld-return test.
- The first portable-test edit assigned a parked mount to a reordered actor
  that already owned an active mount. The existing validation rejected it;
  test setup now selects by ownership rather than array position.

## Remaining acceptance

The exploratory `bubble0-first` carries a waiting mounted Mario through Luigi's
pipe entry, preserving its countdown and mount. After the delay expires it
cannot find a position beside the mounted survivor: the generic conservative
sprite rectangle overlaps every candidate, even where actual rider/mount
bodies would fit. This was an open recovery defect at `439c81e`; the correction
and its bounded acceptance are recorded below.

Other open work includes slot pressure, all entrance forms/special scenes,
the full baby/source/berry/shell/wing/keyhole matrix, in-room bubble mouth-timer
preservation, campaign persistence, complex recovery and complete campaign
mechanics/exit coverage. Native pipe mouth clearing above does not waive the
contract to preserve mouth contents during in-room catch-up.

## Ownership-aware mounted recovery

The terrain query now receives a read-only view of the stable native entity
records. A live Yoshi slot is handled as a ridden mount only when its type,
status, entity kind, owner and owner's mount ID agree.

An active rider and mount use one combined clearance body (32 pixels wide,
48 tall, aligned to the rider's feet). The candidate also checks this combined
body for overlap. Its native slot is not additionally counted with the broad
generic sprite rectangle. A bubble's retained mount is out of play and does
not obstruct recovery at its old native position. An unowned Yoshi or missing/
inconsistent ownership still receives conservative sprite clearance. Other
actors, objects and terrain remain obstacles.

No guest state is written during the safe-placement query. A successful query
still goes through the existing recovery action and native actor/mount
placement. No new immunity, forced recovery, changed delay or fallback overlap
is introduced.

Evidence root: `build-adaptive/playtest/goal-20261019/mount-recovery`.
`before-recovery.exe` preserves the `439c81e` transport build.
The same `bubble0-pipe.sav` and input script change from permanently waiting
at zero delay to Mario recovering at x=70 beside Luigi at x=30, both with
center y=368 and their original mount IDs. This is room `$1CA`; the original
pipe entrance has already cleared their mouths.

```text
python tools/test_coop_mounted_recovery_live.py <two-rider-YI2-state> --out <new-directory>
python tools/test_native_coop.py
```

`acceptance-first` passes **32/32 execution cases**, **40 complete WRAM/co-op
snapshot parity comparisons** and **32 exact save/load replays**:

- Either actor returning beside a mounted survivor in standard/wide and
  native/bounce execution.
- Both mounted bodies separated with matching footing, the same mount IDs
  and equipment, and no added catch-up protection.
- In-room waiting retains the actual mouth object/status and swallow timer
  while the survivor's timer keeps advancing. Recovery resumes the returning
  mount's timer. Sixteen waiting snapshots check that independent behavior.
- Deliberately obstructed candidates keep the bubble after its delay expires.
  Removing only those staged obstruction tiles allows the existing recovery
  code to place the actor safely.
- A waiting bubble travels through the other actor's actual pipe entry.
  Prepared entrance state preserves ownership/delay; recovery occurs after
  arrival. The normal room-entry mouth-clearing rule still applies.

These tests stage the catch-up state and delay; they do not prove the edge
warning/catch-up trigger. Obstruction tiles are offline collision fixtures,
not rendered-terrain acceptance. The portable regression covers the original
failure with absent ownership, corrected placement, unowned-Yoshi exclusion,
a third actor blocking placement, noncontiguous player IDs and query
nonmutation. All 27 owner file hashes remain unchanged.

Tested executable SHA256:
`df4a974bffbede74ba793f0d1f24ea369f50dbe94b1c2c05db722a4217f8b6c8`.
Broader slope/water/air/Layer 2/autoscroll recovery and mount/campaign acceptance
remain open.
