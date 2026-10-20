#!/usr/bin/env python3
"""Finite, muted native fireball and boss-adapter checks using copied states.

Requires the original ROM and a CNR5 YI2 entrance. Loader cheats bypass stage
prerequisites; these checks do not qualify campaign routes, terrain or clears.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key
from make_coop_projectile_fixture import make

# Actual stock content tables: C2Sub4, C5Sub1, C4Sub4, C1Sub2, C7Sub2.
BOSSES = {'morton': (0x0e5, 7), 'roy': (0x0cc, 0x20),
          'ludwig': (0x0d9, 0x0e), 'iggy': (0x1f6, 0x25), 'larry': (0x1eb, 0x34)}


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav')


def shots(f, player):
    return [s for s in f.fireballs[player] if s['guest'][0]]


def same(a, b):
    assert a.data[a.machine:a.end] == b.data[b.machine:b.end], 'Co-op payload differs'
    assert a.data[a.ram:a.ram+0x20000] == b.data[b.ram:b.ram+0x20000], 'WRAM differs'


def fire_contacts(out, pc):
    with (out/'projectiles.csv').open() as file:
        rows = list(csv.DictReader(file))
    assert len(rows) == 2, rows
    assert [r['pc'] for r in rows] == [pc, pc], rows
    assert [r['actor'] for r in rows] == ['0', '1'], rows
    assert [r['accepted'] for r in rows] == ['1', '0'], rows
    assert rows[0]['frame'] == rows[1]['frame'], rows
    return rows


def lifecycle(out):
    saves = {n: state(out, n) for n in (11, 9, 8, 7, 6, 5, 4)}
    for player in (0, 1):
        assert len(shots(saves[11], player)) == 1
        assert len(shots(saves[9], player)) == len(shots(saves[8], player)) == 2
        ids = [s['id'] for s in saves[9].fireballs[player]]
        assert ids == [s['id'] for s in saves[8].fireballs[player]]
        for n, timer in ((9, 8), (8, 0)):
            f = saves[n]
            assert f.data[f.actors[player][0]+f.offsets[0x149c]] == timer
        assert not shots(saves[5], player)
        fresh = shots(saves[4], player)
        assert len(fresh) == 1 and fresh[0]['id'] not in ids
        # Original spawn retains the empty slot's age, rather than resetting it.
        previous = saves[5].fireballs[player][1]['guest'][9]
        assert fresh[0]['guest'][9] == (previous+3) & 255
    assert len({s['id'] for bank in saves[9].fireballs.values() for s in bank}) == 4
    for f in saves.values():
        assert f.data[f.ram+0x1713:f.ram+0x1715] == bytes(2), 'Owned shot leaked to shared slots'
    same(saves[7], saves[6])
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    parser.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    parser.add_argument('--legacy', type=Path, help='CNR5 state with both physical fireball slots occupied')
    parser.add_argument('--suite', choices=('all', 'base', 'freeze'), default='all')
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    results, parity = [], {}

    def run(name, snapshot, script_text, frames, native, bounce, capture=80):
        script = root/f'{name}.script'
        script.write_text(script_text, encoding='ascii', newline='\n')
        out = root/name
        command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(snapshot.resolve()), '--script', str(script),
                   '--out', str(out), '--frames', str(frames), '--framedump', str(capture),
                   '--projectile-trace']
        if native:
            command.append('--native')
        if bounce:
            command.append('--bounce')
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out']
        return out

    def passed(key, out, slots, bounce, details=None):
        for slot in slots:
            f = state(out, slot)
            identity = (key, slot)
            if bounce:
                same(f, parity[identity])
            else:
                parity[identity] = f
        results.append({'case': key, 'bounce': bounce, 'status': 'passed',
                        'evidence': str(out.relative_to(root)), 'details': details})
        (root/'summary.json').write_text(json.dumps({
            'checks': results, 'scope': 'Focused mechanics; stage prerequisites bypassed',
            'campaign_coverage': 'unverified'}, indent=2)+'\n')

    allowance = root/'allowance.sav'
    script = make(args.source, allowance)
    chuck = root/'chuck.sav'
    chuck_script = make(args.source, chuck, 'chuck')
    for bounce in (False, True):
        path = 'bounce' if bounce else 'native'
        for narrow in (True, False):
            aspect = 'standard' if narrow else 'wide'
            out = run(f'lifecycle-{aspect}-{path}', allowance, script, 260, narrow, bounce, 99)
            lifecycle(out)
            passed(f'lifecycle-{aspect}', out, (11, 9, 8, 7, 6, 5, 4), bounce)
            if args.suite != 'base':
                f = state(out, 9)
                f.data[f.ram+0x14c8] = 0
                loose_key(f, 40, 368, slot=0)
                rom = args.rom.read_bytes()
                for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                            (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                            (0x1686, 0x3f590), (0x190f, 0x3f659)):
                    f.data[f.ram+table] = rom[source_table+0x77]
                f.data[f.ram+0x9e] = 0x77
                f.data[f.ram+0x14c8] = 8
                fixture = root/f'freeze-{aspect}-{path}.sav'
                f.save(fixture)
                freeze = run(f'freeze-{aspect}-{path}', fixture,
                             'wait 60\nloadstate 10\nwait 10\nsavestate 11\nwait 30\nsavestate 9\nwait 5\n',
                             115, narrow, bounce)
                frozen, resumed = state(freeze, 11), state(freeze, 9)
                assert frozen.data[frozen.ram+0x9d] and not resumed.data[resumed.ram+0x9d]
                assert resumed.word(resumed.actors[0][1]+12) == 2
                for player in f.actors:
                    before = [s['guest'] for s in f.fireballs[player]]
                    assert before == [s['guest'] for s in frozen.fireballs[player]]
                    assert before != [s['guest'] for s in resumed.fireballs[player]]
                passed(f'freeze-{aspect}', freeze, (11, 9), bounce)
            if args.suite == 'freeze':
                continue
            out = run(f'chuck-{aspect}-{path}', chuck, chuck_script, 125, narrow, bounce, 83)
            contacts = fire_contacts(out, '02a0f5')
            f = state(out, 11)
            assert f.data[f.ram+0x1528] == 1 and f.data[f.ram+0x14c8] == 8
            assert not any(shots(f, p) for p in f.actors)
            passed(f'chuck-{aspect}', out, (11,), bounce, contacts)
        if args.suite == 'freeze':
            continue
        for side in ('left', 'right'):
            f = Fixture(allowance)
            x, y, camera = (40, 352, 128) if side == 'left' else (300, 304, 0)
            for player in f.actors:
                f.actor(player, x, y, 3, 0)
            f.camera(camera)
            f.put(f.core+92, 448)
            for offset, value in ((4, camera+128), (12, x+8), (16, y+19), (20, 2), (24, 1), (28, 1)):
                f.put(f.focus+offset, value)
            fixture = root/f'margin-{side}-{path}.sav'
            f.save(fixture)
            text = 'wait 60\nloadstate 10\nwait 2\npress y+p2.y 1\nwait 5\nsavestate 11\nwait 10\n'
            out = run(f'margin-{side}-{path}', fixture, text, 95, False, bounce, 70)
            f = state(out, 11)
            for player in f.actors:
                live = shots(f, player)
                assert len(live) == 1 and live[0]['visible'] == 1
                position = live[0]['guest'][2] + live[0]['guest'][4]*256-camera
                assert position < 0 if side == 'left' else position >= 256
            passed(f'margin-{side}', out, (11,), bounce)
        for boss, (room, translevel) in BOSSES.items():
            f = Fixture(allowance)
            for at, value in ((0x100, 15), (0x141a, 1), (0x1b93, 0), (0x19b8, room & 255),
                              (0x1f11, room >> 8), (0xdd6, 0), (0x13bf, translevel)):
                f.data[f.ram+at] = value
            fixture = root/f'{boss}-loader-{path}.sav'
            f.save(fixture)
            entry = run(f'{boss}-entry-{path}', fixture,
                        'wait 60\nloadstate 10\nwait 160\nsavestate 11\nwait 5\n',
                        235, True, bounce, 218)
            f = state(entry, 11)
            assert f.level == room
            assert all(f.word(record+8) == 0 for _, record in f.actors.values())
            passed(f'{boss}-entry', entry, (11,), bounce)
            source = entry/'saves/native-coop-v1/save11.sav'
            delay, tail = (120, 80) if boss in ('morton', 'roy') else (2, 55)
            if boss == 'ludwig':
                # Teleport to the chamber's real scroll-trigger threshold.
                # Do not set the boss state, hit count, success flag or exit.
                for player in f.actors:
                    f.actor(player, 280, 144, 3, 0)
                f.camera(126)
                for offset, value in ((4, 288), (12, 288), (16, 163), (20, 2), (24, 1), (28, 1)):
                    f.put(f.focus+offset, value)
                source = root/f'ludwig-approach-{path}.sav'
                f.save(source)
                delay, tail = 45, 70
            text = f'wait 60\nloadstate 10\nwait {delay}\npress y+p2.y 1\nwait {tail}\nsavestate 11\nwait 5\n'
            out = run(f'{boss}-fire-{path}', source, text, 80+delay+tail, True, bounce)
            contacts = fire_contacts(out, '01fd36' if boss in ('iggy', 'larry') else '01d3e9')
            if boss not in ('iggy', 'larry'):
                f = state(out, 11)
                assert f.data[f.ram+0x1626+int(contacts[0]['sprite'])] == 1
            passed(f'{boss}-fire', out, (11,), bounce, contacts)
        if args.legacy:
            text = ('wait 60\nloadstate 10\nwait 2\npress y+p2.y 1\nwait 1\nsavestate 11\n'
                    'wait 100\npress y+p2.y 1\nwait 1\nsavestate 9\nwait 5\n')
            out = run(f'legacy-{path}', args.legacy, text, 195, True, bounce)
            old, new = state(out, 11), state(out, 9)
            assert old.data[old.ram+0x1713:old.ram+0x1715] == bytes((5, 5))
            assert not any(s['id'] for bank in old.fireballs.values() for s in bank)
            assert all(len(shots(new, p)) == 1 for p in new.actors)
            passed('legacy-ownership', out, (11, 9), bounce)
    print(f'{len(results)} projectile checks passed; native/bounce WRAM and co-op snapshots match')


if __name__ == '__main__':
    main()
