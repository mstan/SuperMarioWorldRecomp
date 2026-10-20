# Repeated placed-Yoshi source grants

Contract: [NATIVE_COOP.md](../NATIVE_COOP.md); tracking: `beads-8wg.3.30`.
The repeated source adapter passes the focused checks below. This is not
campaign acceptance; expanded-margin hatching has a separate reproduced defect.

## Stock source inventory

The original sprite streams contain five placed `$2C` eggs and no directly
placed `$2D` babies or `$35` adult Yoshis. The extraction is retained in
`build-adaptive/playtest/goal-20261019/mount-abilities/placed-source-inventory.json`,
derived from the [complete content inventory](SMW_STOCK_CONTENT.md).

| Room | Level | Sprite-list index | Position | ROM entry |
|---|---|---:|---|---|
| `$130` | Star World 2 | 1 | 144,368 | `$07:E225` |
| `$132` | Star World 3 | 0 | 96,368 | `$07:E29F` |
| `$134` | Star World 1 | 23 | 192,2240 | `$07:E20B` |
| `$135` | Star World 4 | 0 | 256,304 | `$07:E2B0` |
| `$136` | Star World 5 | 35 | 2528,368 | `$07:E39F` |

These are source locations, not passes for visiting or completing those rooms.
The vertical position is a full world coordinate.

## Native implementation

A placed egg enters original `InitYoshiEgg` with status 1, which selects its
palette from its X coordinate and sets `$187B`. The `$01:F764` update waits
for original proximity/visibility and starts its native hatch. `$01:F78A`
commits the baby or alternate 1-up selection; `$01:F83D` later turns the egg
into the selected sprite through the original initializer.

Each placed source now has a dynamic record keyed by full room and original
X/Y. It stores the original sprite-load index, consumed grants and the stable
identity of its currently allocated egg. This is separate from Map16 block
source records. The grant is consumed at the accepted hatch decision,
including a cap-limited alternate reward. The count is bounded by the session
roster, not a constant two.

Once an egg becomes its child, its old identity retires and the pending
reference clears. Before a subsequent shared sprite pass, an available physical
slot receives the next original egg at the source position. Original
`InitSpriteTables` initializes it; the original status-1 initializer, palette,
proximity, hatch animation and reward code then run. If all twelve native
slots are occupied, the record retains the unspent grant and retries later.
No occupied slot is overwritten.

Children use native load index `$FF` so their later removal cannot release the
original source's load flag. An original loader revisit is matched against the
saved source; a duplicate pending egg or an exhausted source is suppressed.
Room cleanup retires pending identities but retains grant history. A new
attempt clears source history.

The first implementation test exposed a native single-mount assumption:
the first baby considered the next egg edible. `placed-grants-after` records
`$02:EA8A` reserving slot 11 at frame 95, followed by the egg starting its
second hatch at frame 96. The first baby consumed that egg; only one baby
remained. The co-op mouth eligibility rule now excludes eggs, babies and adult
mounts from food. Snapshot preflight rejects an active mouth claiming one.
Ordinary edible objects and alternate 1-ups retain native behavior.

## Snapshot contract

CNR8 adds a `PEG1` chunk after the existing `MTR1` chunk: an eight-byte
signature/count header followed by 24-byte records containing six explicit
32-bit fields: room, X, Y, original load index, consumed grants, pending egg ID.
Zero means no pending egg. No host pointers or transient call state are saved.

Structural validation checks lengths, coordinates, room/load-index bounds,
roster-sized grant limits, unique origins and pending references. Each pending
identity must identify an unowned normal egg in the current room. Preflight
also checks actual WRAM type, status, origin and hatch contents before applying
any guest bytes. Corrupt records cannot partly replace the live session.

CNR2–7 remain readable with empty placed-source history. Older states did not
record consumed placed grants; their missing history is not invented. An
unhatched legacy egg can be adopted when its original initialization/waiting
state is observed. A legacy hatch already in progress is allowed to finish.
Normal SRAM and the separate co-op save namespace are unchanged.

## Reproduction

```text
python tools/test_coop_placed_sources_live.py <unowned-YI2-state> \
  --single <one-rider-state> --two <two-rider-state> --out <new-directory>
python tools/test_coop_placed_lifecycle_live.py <first-hatch-CNR8-state> \
  --out <new-directory>
python tools/test_coop_placed_preflight_live.py <first-hatch-CNR8-state> \
  --out <new-directory>
python tools/test_coop_placed_pickup_live.py <two-children-CNR8-state> \
  --out <new-directory>
```

The source matrix stages eggs, actor positions/colors, occupied slots and the
later release of a slot offline. Hatching and grant decisions execute actual
native routines. The lifecycle suite uses actual YI2/cave pipe inputs and
actual timeout/retry code; its copied approach positions, camera changes and
one-second timer are explicit setup. They do not qualify streaming visuals or
normal campaign traversal.

Private evidence root:
`build-adaptive/playtest/goal-20261019/mount-abilities`.

## Focused results

- `placed-source-acceptance-replay`: **56 execution cases**, **76 full WRAM/CNR
  parity comparisons**, **56 exact save/load replays**. Both triggering actors,
  all four original X-derived palettes, one rider, two-rider cap, exhausted
  loader revisits and full native sprite-pool deferral/release are covered.
- `placed-lifecycle-references`: **16 execution cases**, **eight full WRAM/CNR
  parity comparisons**, **16 exact replays**. Native YI2-to-cave entry, return,
  the remaining grant and actual timeout/retry retain/reset the expected source
  history. The approach positions and one-second timer are staged.
- `placed-preflight-first`: **18 checks**, comprising eight malformed records
  plus a missing-slot control in each execution mode. Every rejected load leaves
  the complete resulting guarded snapshot identical to its control. The parent
  independently checked the files and rejection logs after the worker's run.
- `placed-pickup-acceptance`: **four execution cases**, **two WRAM/CNR parity
  comparisons** and **four exact replays**. Mario and Luigi approach with held
  pickup input and acquire distinct actual babies from the completed source.
- `placed-inert-parity.json`: disabled co-op and enabled One Player each produce
  the same complete 303,968-byte snapshot as the preserved original-mode control.
  SHA256: `1f443b773cbdaa893c5a8d5c49407ee5393b88cdcad5bc14f1d01cddfbeaa46f`.
- Portable core cases include roster sizes 2/3/4/17, round trips, legacy CNR7,
  source retirement, malformed fields and mount-as-food rejection. Core,
  hook-installation and snapshot preflight checks pass.

All probes check exit status, timeout and dispatch misses. The 27 preserved
owner files remain unchanged (`placed-verification.json`). Automated output is
muted; these throughput probes do not qualify audio.

Tested executable SHA256:
`f1935409bebd028110f3bbc9ee811f2e4718122b854908d08ed1c767cc5d5a0a`.

Retained test-tool corrections:

- `placed-source-acceptance-first` used different wait/save partitions for its
  replay, producing a two-frame timing difference. The corrected script repeats
  the same partitions and intermediate save action.
- `placed-lifecycle-first` changed an in-memory comparison reference when
  preparing its next fixture. The native and bounce files themselves were
  identical. The corrected comparison reloads the untouched saved reference.
- The first pickup fixture overlapped the babies on the load frame; the next
  released held input before reaching them. `placed-pickup-approach-held`
  holds Y while approaching for 50 frames and produces two distinct held baby
  identities, one per actor. These failed setup attempts are retained.

## Outstanding source coverage

`placed-margin-waiting-before` reproduces a distinct visibility gate: an
already initialized egg at X=288 remains pending with zero consumed grants
beside Mario. Its camera is X=19, `$15A0=1`, `$186C=0`, and it is visibly inside
the expanded viewport. Original `$01:F76C` branches on `IsSprOffScreen` before
the hatch proximity test. A newly initialized egg is not a reliable negative
control because its initially clear offscreen flags can allow an early hatch.
No margin fix is claimed by this source-lifecycle milestone. The subsequent
[visibility/keyhole milestone](native-coop-mount-visibility-keyholes-2026-10-20.md)
fixes that gate and records fourteen passing viewport checks.

The full stock locations, source/ability competition and campaign mechanics
still need their own evidence. The broader baby regression was rerun against
this source-lifecycle build: all 44 cases pass, with the retained deadline
failure and separate successful retry described in the follow-up report.
