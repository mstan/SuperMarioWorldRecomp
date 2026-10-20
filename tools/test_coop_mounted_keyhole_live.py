#!/usr/bin/env python3
"""Qualify stock Yoshi-mouth keyhole completion from a real swallowed-key state.

Source is the held-release-standard-native save8 state: the original Yoshi
mouth update has swallowed an actual stock key sprite (status 7), and the
mount record contains the native YoshiHasKey result. The fixture only moves
the two actors and the existing mount, changes which actor owns/rides that
mount, and adds a stock keyhole sprite at the target position. It never writes
YoshiHasKey, the mouth slot, key status, keyhole/end/secret flags, or an outcome.
The key remains in the mount's original mouth throughout every run.

`--expect pass` is the regression mode and fails on the old build without the
mounted-mouth adapter. `--expect current-gap` characterizes that
known failure for each rider; it succeeds only when the keyhole does not
activate, and is not acceptance evidence. A distant-hole control checks that
positioning alone cannot clear the level. The positive path snapshots the
first pending keyhole phase and requires exact WRAM/co-op-state replay.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type


SOURCE = (ROOT / 'build-adaptive/playtest/goal-20261019/mouth-ownership/'
          'acceptance-arbitration-first/held-release-standard-native/'
          'saves/native-coop-v1/save8.sav')
NO_ENTITY = 0xffffffff
YOSHI_HAS_KEY_MOUNT_OFFSET = 20  # coop_machine.c mount_fields[20] = $191C


def state(out, number):
    return Fixture(out / f'saves/native-coop-v1/save{number}.sav', game_mode=None)


def same(a, b):
    assert a.data[a.ram:a.ram+0x20000] == b.data[b.ram:b.ram+0x20000], 'WRAM differs after replay'
    assert a.data[a.machine:a.end] == b.data[b.machine:b.end], 'co-op payload differs after replay'


def actor_field(f, player, address, value):
    guest, _ = f.actors[player]
    f.data[guest + f.offsets[address]] = value
    if player == f.word(f.core + 20):
        f.data[f.ram + address] = value


def place_actor(f, player, x, y, mounted):
    guest, record = f.actors[player]
    # Preserve the source state's power/reserve and life. Only position and
    # mounted-body geometry are staged for contact eligibility.
    power = f.data[guest + f.offsets[0x19]]
    reserve = f.data[guest + f.offsets[0xdc2]]
    life = f.word(record + 8)
    recovery = f.word(record + 44)
    f.actor(player, x, y, power, reserve, life=life, recovery=recovery)
    actor_field(f, player, 0x187a, int(mounted))
    actor_field(f, player, 0x76, int(mounted))
    if mounted:
        f.put(record + 24, y + 32)
        f.put(record + 32, 32)


def source_mount(f):
    assert f.version == 6 and f.level == 0x106, (f.version, f.level)
    mounts = [(p, f.word(record + 52)) for p, (_, record) in f.actors.items()
              if f.word(record + 52) != NO_ENTITY]
    assert len(mounts) == 1 and mounts[0][0] == 0, mounts
    owner, identity = mounts[0]
    mount = f.entities[identity]
    assert mount[1:5] == (0, 0, 0x35, owner) and mount[6] == 2, mount
    mount_slot = mount[2]
    key_slot = f.data[f.ram + 0x160e + mount_slot]
    assert key_slot < 12 and f.data[f.ram + 0x14c8 + key_slot] == 7
    key = f.entities.get(next((i for i, e in f.entities.items()
                               if e[1] == 0 and e[2] == key_slot), NO_ENTITY))
    assert key and key[3] == 0x80 and key[4] == NO_ENTITY and key[6] == 0, key
    assert f.mounts[identity][YOSHI_HAS_KEY_MOUNT_OFFSET] == 1
    assert f.data[f.ram + 0x1434] == 0, 'source already has an active keyhole'
    assert all(e[3] != 0x0e for e in f.entities.values()), 'source already has a keyhole entity'
    return identity, mount_slot, key_slot


def make_fixture(source, output, owner, control=False):
    f = Fixture(source)
    identity, mount_slot, key_slot = source_mount(f)
    d, r = f.data, f.ram
    other = 1 - owner
    owner_record = f.actors[owner][1]
    other_record = f.actors[other][1]
    # Move the same serialized mount identity; this changes rider ownership,
    # not its mouth contents or YoshiHasKey. Keep owner/flags paired in ENT1.
    f.put(owner_record + 52, identity)
    f.put(other_record + 52, NO_ENTITY)
    ent = d.index(b'ENT1', f.core_end) + 16
    for index in range(len(f.entities)):
        at = ent + index * 28
        if f.word(at) == identity:
            f.put(at + 16, owner)
            break
    else:
        raise AssertionError('mount identity missing from ENT1')

    # The mounted actor is adjacent to the hole. The partner remains nearby
    # but has no key and cannot activate a hole while on foot.
    rider_x, rider_y = 40, 336
    place_actor(f, owner, rider_x, rider_y, True)
    place_actor(f, other, 128, 352, False)
    mount = f.entities[identity]
    slot = mount[2]
    for low, high, value in ((0xe4, 0x14e0, rider_x), (0xd8, 0x14d4, rider_y + 16)):
        d[r + low + slot], d[r + high + slot] = value & 255, value >> 8

    # The stock InitKeyHole adds eight to X. This active-state fixture stores
    # its post-init collision position directly; no end/secret field is set.
    hole_x = 232 if control else rider_x + 8
    hole_y = rider_y + 16
    hole_slot = next(i for i in range(12) if not d[r + 0x14c8 + i])
    loose_key(f, hole_x, hole_y, hole_slot)
    sprite_type(f, hole_slot, 0x0e)
    d[r + 0x14c8 + hole_slot] = 8
    d[r + 0x154c + hole_slot] = 0
    f.camera(0)

    # Guard all prohibited outcome sources after fixture edits. These are
    # assertions only: no tested success flag or mouth state is written.
    assert f.data[r + 0x160e + slot] == key_slot
    assert d[r + 0x14c8 + key_slot] == 7
    assert f.mounts[identity][YOSHI_HAS_KEY_MOUNT_OFFSET] == 1
    assert d[r + 0x1434] == d[r + 0x1435] == 0
    f.save(output)
    return dict(owner=owner, mount_id=identity, mount_slot=slot, key_slot=key_slot,
                keyhole_slot=hole_slot, hole_x=hole_x, hole_y=hole_y,
                control=control)


def make_pair_fixture(source, output, owner, control):
    """Keep both actual mounts/mouths; put the hole beside one chosen rider."""
    f = Fixture(source)
    assert len(f.mounts) == 2 and f.level == 0x106
    identities = {p: f.word(r+52) for p, (g, r) in f.actors.items()}
    assert {p for p, identity in identities.items()
            if f.mounts[identity][YOSHI_HAS_KEY_MOUNT_OFFSET]} == {owner}
    for p, identity in identities.items():
        e = f.entities[identity]
        assert e[3] == 0x35 and e[4] == p and e[6] == 2
        x = 40 if p == owner else 160
        place_actor(f, p, x, 336, True)
        for low, high, value in ((0xe4, 0x14e0, x), (0xd8, 0x14d4, 352)):
            f.data[f.ram+low+e[2]], f.data[f.ram+high+e[2]] = value & 255, value >> 8
    assert_live_mouth(f, owner, identities[owner])
    other_slot = f.entities[identities[1-owner]][2]
    assert f.data[f.ram+0x160e+other_slot] == 255
    hole_slot = next(i for i in range(12) if not f.data[f.ram+0x14c8+i])
    hole_x = 168 if control else 48
    loose_key(f, hole_x, 352, hole_slot)
    sprite_type(f, hole_slot, 0x0e)
    f.data[f.ram+0x14c8+hole_slot] = 8
    f.data[f.ram+0x154c+hole_slot] = 0
    assert not f.data[f.ram+0x1434]
    f.camera(0)
    f.save(output)
    return dict(owner=owner, mount_id=identities[owner], control=control,
                hole_x=hole_x, empty_mount_id=identities[1-owner])


def run(root, name, snapshot, script_text, frames, native, bounce, exe, rom):
    script = root / f'{name}.script'
    script.write_text(script_text, encoding='ascii', newline='\n')
    out = root / name
    command = [sys.executable, str(ROOT / 'tools/run_coop_probe.py'),
               '--exe', str(exe.resolve()), '--rom', str(rom.resolve()),
               '--snapshot', str(snapshot.resolve()), '--script', str(script),
               '--out', str(out), '--frames', str(frames), '--framedump', '120',
               '--wram-watch', '1434:1438', '--mouth-trace', '--timeout-seconds', '300']
    if native:
        command.append('--native')
    if bounce:
        command.append('--bounce')
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    report = json.loads((out / 'result.json').read_text())
    assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out'], report
    return out, report


def read_trace(path):
    rows = list(csv.DictReader(path.open(newline='')))
    assert len(rows) > 100 and len(rows) % 2 == 0, (path, len(rows))
    assert all(int(row['stack']) == 511 and int(row['pause']) == 0 for row in rows), path
    return rows


def assert_live_mouth(f, owner, identity):
    """Verify the live saved mount still owns the actual swallowed key."""
    _, record = f.actors[owner]
    assert f.word(record + 52) == identity, (owner, f.word(record + 52), identity)
    mount = f.entities[identity]
    assert mount[1] == 0 and mount[3] == 0x35 and mount[4] == owner and mount[6] == 2, mount
    slot = mount[2]
    key_slot = f.data[f.ram + 0x160e + slot]
    key = next((e for e in f.entities.values() if e[1] == 0 and e[2] == key_slot), None)
    assert key and key[3] == 0x80 and key[4] == NO_ENTITY and key[6] == 0, key
    assert f.data[f.ram + 0x14c8 + key_slot] == 7, key_slot
    assert f.mounts[identity][YOSHI_HAS_KEY_MOUNT_OFFSET] == 1
    guest, _ = f.actors[owner]
    assert f.data[guest + f.offsets[0x187a]] == 1, owner


def evaluate_positive(out, owner, identity, expected):
    rows = read_trace(out / 'coop.csv')
    lead = rows[::2]
    exits = [r for r in lead if int(r['exit_player']) != NO_ENTITY]
    timer_starts = [r for r in lead if int(r['keyhole_timer']) == 48]
    clears = [r for r in lead if int(r['outcome']) == 3]
    pending = state(out, 9)
    assert_live_mouth(pending, owner, identity)
    timer_at_replay = pending.data[pending.ram + 0x1434]
    if expected == 'pass':
        assert exits and {int(r['exit_player']) for r in exits} == {owner}, exits
        assert all(int(r['exit_flags']) & 1 for r in exits), exits
        assert all(int(r['keyhole_timer']) == 48 and int(r['exit_candidates']) == 1
                   for r in exits), exits
        assert 0 < timer_at_replay <= 48, (out, timer_at_replay)
        assert timer_starts and clears, (out, timer_starts, clears)
        assert int(clears[-1]['ow_exit']) == 2, clears[-1]
        status = 'passed'
    else:
        assert not exits and not timer_starts and not clears, (out, exits, timer_starts, clears)
        assert timer_at_replay == 0, (out, timer_at_replay)
        status = 'known-current-gap'
    same(state(out, 8), state(out, 7))
    return dict(status=status, trace_rows=len(rows), exit_events=len(exits),
                timer_starts=len(timer_starts), clear_rows=len(clears),
                replay_timer=timer_at_replay, exact_snapshot_replay=True,
                trace_replay_compared=False, live_mouth_owner_checked=True)


def evaluate_control(out):
    rows = read_trace(out / 'coop.csv')[::2]
    exits = [r for r in rows if int(r['exit_player']) != NO_ENTITY]
    assert not exits, (out, exits)
    assert not any(int(r['keyhole_timer']) for r in rows), out
    assert not any(int(r['outcome']) == 3 for r in rows), out
    same(state(out, 8), state(out, 7))
    return dict(status='passed', exit_events=0, keyhole_timers=0,
                exact_snapshot_replay=True, trace_replay_compared=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, default=SOURCE)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT / 'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT / 'smw.sfc')
    ap.add_argument('--actors', nargs='+', type=int, choices=(0, 1), default=(0, 1))
    ap.add_argument('--expect', choices=('pass', 'current-gap'), default='pass',
                    help='pass is acceptance; current-gap characterizes the known missing adapter')
    ap.add_argument('--skip-controls', action='store_true',
                    help='omit the distant-keyhole negative control')
    ap.add_argument('--pair-root', type=Path,
                    help='mouth arbitration output containing tie/nearest saves; adds two-mount isolation checks')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source = args.source.resolve()
    source_fixture = Fixture(source)
    source_mount(source_fixture)

    results = []
    snapshots = {}
    positive_script = ('wait 60\nloadstate 10\nwait 3\nsavestate 9\n'
                       'wait 140\nsavestate 8\nloadstate 9\nwait 140\n'
                       'savestate 7\nwait 5\n')
    control_script = ('wait 60\nloadstate 10\nwait 3\nsavestate 9\n'
                      'wait 140\nsavestate 8\nloadstate 9\nwait 140\n'
                      'savestate 7\nwait 5\n')

    for actor in args.actors:
        for narrow in (True, False):
            aspect = 'standard' if narrow else 'wide'
            fixture_path = root / f'actor{actor}-{aspect}.sav'
            staged = make_fixture(source, fixture_path, actor)
            for bounce in (False, True):
                mode = 'bounce' if bounce else 'native'
                name = f'actor{actor}-{aspect}-{mode}'
                out, report = run(root, name, fixture_path, positive_script, 355,
                                  narrow, bounce, args.exe, args.rom)
                result = evaluate_positive(out, actor, staged['mount_id'], args.expect)
                result.update(actor=actor, aspect=aspect, execution=mode,
                              output=str(out.relative_to(ROOT)), staged=staged,
                              run=report)
                results.append(result)
                snapshots[(actor, aspect, mode)] = out
                (root / 'summary.json').write_text(
                    json.dumps(dict(expect=args.expect, checks=results), indent=2) + '\n')

            native = snapshots[(actor, aspect, 'native')]
            bounce = snapshots[(actor, aspect, 'bounce')]
            for slot in (7, 8):
                same(state(native, slot), state(bounce, slot))

        if not args.skip_controls:
            control_path = root / f'actor{actor}-distant-hole.sav'
            staged = make_fixture(source, control_path, actor, control=True)
            out, report = run(root, f'actor{actor}-distant-hole-control', control_path,
                              control_script, 355, True, False, args.exe, args.rom)
            result = evaluate_control(out)
            result.update(actor=actor, aspect='standard', execution='native',
                          output=str(out.relative_to(ROOT)), staged=staged, run=report)
            results.append(result)
            (root / 'summary.json').write_text(
                json.dumps(dict(expect=args.expect, checks=results), indent=2) + '\n')

    if args.pair_root:
        for owner in args.actors:
            source = args.pair_root / (
                'tie-standard-native' if owner == 0 else 'nearest-standard-native')
            source /= 'saves/native-coop-v1/save8.sav'
            for control in (False, True):
                key = f'pair{owner}-{"empty-mouth" if control else "key-mouth"}'
                fixture_path = root/f'{key}.sav'
                staged = make_pair_fixture(source, fixture_path, owner, control)
                parity = {}
                for narrow in (True, False):
                    for bounce in (False, True):
                        name = f'{key}-{"standard" if narrow else "wide"}-{"bounce" if bounce else "native"}'
                        out, report = run(root, name, fixture_path, positive_script, 355,
                                          narrow, bounce, args.exe, args.rom)
                        assert_live_mouth(state(out, 9), owner, staged['mount_id'])
                        result = (evaluate_control(out) if control else
                                  evaluate_positive(out, owner, staged['mount_id'], args.expect))
                        if bounce:
                            for n in (7, 8, 9):
                                same(state(out, n), state(parity[narrow], n))
                        else:
                            parity[narrow] = out
                        result.update(actor=owner, case=name, staged=staged, run=report,
                                      output=str(out.relative_to(ROOT)))
                        results.append(result)
                        (root/'summary.json').write_text(json.dumps(dict(
                            expect=args.expect, checks=results), indent=2)+'\n')

    summary = dict(expect=args.expect, acceptance=(args.expect == 'pass'), checks=results,
                   source=str(source), controls=not args.skip_controls)
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if args.expect == 'current-gap':
        print(f"{sum(r['status'] == 'known-current-gap' for r in results)} mounted-mouth keyhole gaps reproduced; controls/replays checked")
    else:
        print(f"{sum(r['status'] == 'passed' for r in results)} mounted-mouth keyhole acceptance checks passed")


if __name__ == '__main__':
    main()
