#!/usr/bin/env python3
"""Finite muted mount entrance/parking checks, with native/bounce parity.

Input is a two-rider YI2 state produced by test_coop_mouth_live's independent
case. Positions/colors and the primary castle loader request are staged.
Dismounts, pipes, the no-Yoshi script and overworld reentry use native inputs.
Castle completion is bypassed solely to permit Start+Select; no clear, terrain,
audio or complete campaign qualification is claimed.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from test_coop_mouth_live import same, set_actor

NONE = 0xffffffff


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav', game_mode=None)


def ids(f):
    return {p: f.word(record+52) for p, (_, record) in f.actors.items()
            if f.word(record+52) != NONE}


def color_seed(source, output):
    f = Fixture(source)
    assert f.level == 0x106 and set(ids(f)) == {0, 1}
    for p, identity in ids(f).items():
        color = 6+2*p
        slot = f.entities[identity][2]
        f.data[f.mount_records[identity]+13] = color
        f.data[f.ram+0x15f6+slot] = (f.data[f.ram+0x15f6+slot] & ~14) | color
        if p == 0:
            f.data[f.ram+0x13c7] = color
    f.save(output)


def pipe(source, output, entrant):
    f = Fixture(source)
    riding = ids(f)
    for p in f.actors:
        x = 3880 if p == entrant else 3760
        y = (304 if p == entrant else 336) + (0 if p in riding else 16)
        set_actor(f, p, x, y, p in riding)
        if p in riding:
            slot = f.entities[riding[p]][2]
            for lo, hi, value in ((0xe4, 0x14e0, x), (0xd8, 0x14d4, y+16)):
                f.data[f.ram+lo+slot] = value & 255
                f.data[f.ram+hi+slot] = value >> 8
    f.camera(3744)
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    f.save(output)


def castle(source, output):
    f = Fixture(source)
    # Original primary-loader override: translevel $25 -> submap room $101.
    # The ROM selects the no-Yoshi intro; RemoveYoshiFlag is not forced.
    for address, value in ((0x100, 15), (0x109, 0x25), (0x13bf, 0x25),
                           (0x1f11, 1), (0x1f12, 1), (0x141a, 0),
                           (0x141d, 0), (0x141f, 0), (0xdd6, 0)):
        f.data[f.ram+address] = value
    f.camera(0)
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    f.save(output)


def active_mounts(f, expected, level):
    assert f.data[f.ram+0x100] == 0x14 and f.level == level
    assert ids(f) == expected, (ids(f), expected)
    assert all(identity == 0 for identity, _ in f.stored_mounts.values())
    assert not f.entrance_prepared
    slots = set()
    for p, identity in expected.items():
        e = f.entities[identity]
        assert e[3:5] == (0x35, p) and e[6] == 2
        slot = e[2]
        assert slot not in slots
        slots.add(slot)
        assert f.data[f.ram+0x14c8+slot] == 8 and f.data[f.ram+0xc2+slot] == 1
        assert f.data[f.ram+0x160e+slot] == 255, 'native entrance must clear mouth'
        assert f.mounts[identity][1] & 14 == 6+2*p, 'mount color transferred between actors'
    for p, (guest, _) in f.actors.items():
        assert bool(f.data[guest+f.offsets[0x187a]]) == (p in expected)


def parked_mounts(f, expected, mode):
    assert f.data[f.ram+0x100] == mode and not ids(f)
    assert not f.mounts
    assert f.stored_mounts == {p: (expected[p], 6+2*p) if p in expected else (0, 0)
                               for p in f.actors}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--cases', nargs='+', choices=('pipe', 'castle', 'return', 'retry'),
                    default=('pipe', 'castle', 'return', 'retry'))
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    results, parity = [], {}

    def run(name, snapshot, text, frames, narrow=True, bounce=False):
        script = root/f'{name}.script'
        script.write_text(text, encoding='ascii', newline='\n')
        out = root/name
        command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(snapshot.resolve()), '--script', str(script),
                   '--out', str(out), '--frames', str(frames),
                   '--framedump', str(frames-10)]
        if narrow:
            command.append('--native')
        if bounce:
            command.append('--bounce')
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out'], name
        return out

    def record(key, name, out, slots, bounce):
        snapshots = [state(out, slot) for slot in slots]
        if bounce:
            for a, b in zip(snapshots, parity[key]):
                same(a, b)
        else:
            parity[key] = snapshots
        results.append(dict(case=key, bounce=bounce, status='passed', evidence=name,
                            parity_snapshots=slots))
        (root/'summary.json').write_text(json.dumps(dict(
            checks=results, campaign_coverage='unverified',
            bypasses=['staged positions/colors', 'castle loader request',
                      'castle completed flag for early-exit permission']), indent=2)+'\n')

    colored = root/'colors.sav'
    color_seed(args.source, colored)
    setups = {'both': colored}
    for kind, buttons, expected in (('primary', 'p2.a+p2.right', {0}),
                                    ('secondary', 'a+left', {1})):
        out = run(f'dismount-{kind}', colored,
                  f'wait 60\nloadstate 10\npress {buttons} 20\nwait 15\nsavestate 9\nwait 5\n', 110)
        assert set(ids(state(out, 9))) == expected, kind
        setups[kind] = out/'saves/native-coop-v1/save9.sav'

    for bounce in (False, True):
        for narrow in (True, False):
            width = 'standard' if narrow else 'wide'
            backend = 'bounce' if bounce else 'native'
            for kind, source in setups.items():
                expected = ids(Fixture(source))
                if 'pipe' in args.cases:
                    for entrant in (0, 1):
                        key = f'pipe-{kind}-entrant{entrant}-{width}'
                        name = f'{key}-{backend}'
                        fixture = root/f'{name}.sav'
                        pipe(source, fixture, entrant)
                        button = 'down' if entrant == 0 else 'p2.down'
                        out = run(name, fixture,
                                  f'wait 60\nloadstate 10\npress {button} 80\nwait 1\nsavestate 6\n'
                                  'wait 160\nsavestate 9\nloadstate 6\nwait 160\nsavestate 8\nwait 5\n',
                                  480, narrow, bounce)
                        active_mounts(state(out, 9), expected, 0x1ca)
                        same(state(out, 9), state(out, 8))
                        if kind == 'both':
                            assert state(out, 6).entrance_prepared, name
                        rows = list(csv.DictReader((out/'coop.csv').open()))
                        requests = {int(r['room_request']) for r in rows if int(r['room_request']) != NONE}
                        assert requests == {entrant}, (name, requests)
                        record(key, name, out, (6, 9), bounce)

                if not any(c in args.cases for c in ('castle', 'return', 'retry')):
                    continue
                key = f'castle-{kind}-{width}'
                name = f'{key}-{backend}'
                fixture = root/f'{name}.sav'
                castle(source, fixture)
                out = run(name, fixture,
                          'wait 60\nloadstate 10\nwait 680\nsavestate 9\n'
                          'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n',
                          785, narrow, bounce)
                parked = state(out, 9)
                parked_mounts(parked, expected, 0x14)
                assert parked.level == 0x101
                assert parked.data[parked.ram+0x1b9b] == 1 and parked.data[parked.ram+0x141d] == 1
                rows = list(csv.DictReader((out/'coop.csv').open()))
                assert any(r['animation'] == '10' and r['riding'] == '1' for r in rows)
                same(state(out, 8), state(out, 7))
                record(key, name, out, (9, 8), bounce)
                parked_source = out/'saves/native-coop-v1/save9.sav'

                if 'return' in args.cases:
                    key = f'return-{kind}-{width}'
                    name = f'{key}-{backend}'
                    fixture = root/f'{name}.sav'
                    f = Fixture(parked_source)
                    f.data[f.ram+0x1ea2+0x25] |= 0x80  # bypass completion prerequisite, not a tested clear
                    f.data[f.ram+0x109] = 0  # stop the staging loader override
                    f.save(fixture)
                    out = run(name, fixture,
                              'wait 60\nloadstate 10\npress start 1\nwait 3\npress select 1\n'
                              'wait 240\nsavestate 6\npress b 1\nwait 240\nsavestate 9\n'
                              'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n',
                              590, narrow, bounce)
                    parked_mounts(state(out, 6), expected, 0x0e)
                    active_mounts(state(out, 9), expected, 0x106)
                    same(state(out, 8), state(out, 7))
                    record(key, name, out, (6, 9, 8), bounce)

                if 'retry' in args.cases:
                    key = f'retry-{kind}-{width}'
                    name = f'{key}-{backend}'
                    fixture = root/f'{name}.sav'
                    f = Fixture(parked_source)
                    lives = f.word(f.core+36)
                    f.data[f.ram+0xf30:f.ram+0xf34] = bytes((0, 0, 0, 1))
                    f.save(fixture)
                    out = run(name, fixture,
                              'wait 60\nloadstate 10\nwait 850\nsavestate 9\n'
                              'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n',
                              955, narrow, bounce)
                    final = state(out, 9)
                    assert final.data[final.ram+0x100] == 0x14
                    assert final.level == 0x101 and final.word(final.core+36) == lives-1
                    assert not ids(final) and not final.mounts
                    assert all(value == (0, 0) for value in final.stored_mounts.values())
                    same(state(out, 8), state(out, 7))
                    record(key, name, out, (9, 8), bounce)
    print(f'{len(results)} mount transport checks passed; snapshots match native/bounce and replay')


if __name__ == '__main__':
    main()
