#!/usr/bin/env python3
"""Native carry/release and existing-mouth exclusivity, using copied YI2 states.

Positions and source sprites are staged; mounting, grabbing and eating occur
through original routines and inputs. These checks do not qualify campaign
routes or arbitration between tongues contacting a loose item in one frame.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type


def state(out, number):
    return Fixture(out/f'saves/native-coop-v1/save{number}.sav')


def same(a, b):
    assert a.data[a.ram:a.ram+0x20000] == b.data[b.ram:b.ram+0x20000], 'WRAM differs'
    assert a.data[a.machine:a.end] == b.data[b.machine:b.end], 'Co-op payload differs'


def set_actor(f, player, x, y, mounted):
    f.actor(player, x, y, 1, 0)
    guest, record = f.actors[player]
    for address, value in ((0x76, int(mounted)), (0x1490, 0), (0x1497, 0)):
        f.data[guest+f.offsets[address]] = value
        if player == 0:
            f.data[f.ram+address] = value
    if mounted:
        f.put(record+24, y+32)
        f.put(record+32, 32)


def seed(source, output, two):
    f = Fixture(source)
    assert f.version == 5 and not f.mounts and f.level == 0x106
    d, r = f.data, f.ram
    d[r+0x14c8:r+0x14d4] = bytes(12)
    d[r+0x170b:r+0x1783] = bytes(120)
    d[r+0xef8] = 1  # bypass the already-seen Yoshi tutorial message
    for player in (0, 1):
        x, y = (40, 280) if player == 0 else (160, 280) if two else (88, 352)
        set_actor(f, player, x, y, False)
        if player == 0 or two:
            slot = loose_key(f, x, 352, player)
            sprite_type(f, slot, 0x35)
            d[r+0x14c8+slot] = 1  # original InitYoshi, not fabricated ownership
    f.camera(0)
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    f.save(output)


def encounter(source, output, two):
    f = Fixture(source)
    assert len(f.mounts) == (2 if two else 1)
    for player in (0, 1):
        mounted = player == 0 or two
        set_actor(f, player, 40 if mounted else 88, 336 if mounted else 352, mounted)
        if not mounted:
            continue
        identity = f.word(f.actors[player][1]+52)
        e = f.entities[identity]
        assert e[3] == 0x35 and e[4] == player and e[6] == 2
        for address, value in ((0xe4, 40), (0x14e0, 0), (0xd8, 96), (0x14d4, 1),
                               (0x157c, 0), (0x1594, 0), (0x151c, 0), (0x1558, 0)):
            f.data[f.ram+address+e[2]] = value
        assert f.data[f.ram+0x160e+e[2]] == 255
    loose_key(f, 76 if two else 96, 368, 2 if two else 1)
    f.save(output)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    results, parity = [], {}

    def run(name, snapshot, text, frames, native=True, bounce=False, capture=85):
        script = root/f'{name}.script'
        script.write_text(text, encoding='ascii', newline='\n')
        out = root/name
        command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(snapshot.resolve()), '--script', str(script),
                   '--out', str(out), '--frames', str(frames), '--framedump', str(capture),
                   '--wram-watch', '14a3:1619']
        if native:
            command.append('--native')
        if bounce:
            command.append('--bounce')
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out'], name
        return out

    fixtures = {}
    for two in (False, True):
        source = root/f'seed-{int(two)}.sav'
        seed(args.source, source, two)
        out = run(f'setup-{int(two)}', source,
                  'wait 60\nloadstate 10\nwait 90\nsavestate 11\nwait 5\n', 165)
        setup = state(out, 11)
        assert len(setup.mounts) == (2 if two else 1)
        fixtures[two] = root/f'encounter-{int(two)}.sav'
        encounter(out/'saves/native-coop-v1/save11.sav', fixtures[two], two)

    for bounce in (False, True):
        for narrow in (True, False):
            for two in (False, True):
                key = f'{"existing-mouth" if two else "held-release"}-{"standard" if narrow else "wide"}'
                name = f'{key}-{"bounce" if bounce else "native"}'
                if two:
                    # The game runner takes an immediate post-press snapshot
                    # inline, whereas a delayed snapshot/load consumes its
                    # own entry. Use the delayed form for equal replay spans.
                    script = ('wait 60\nloadstate 10\npress y+p2.y 40\nwait 1\nsavestate 9\n'
                              'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n')
                else:
                    script = ('wait 60\nloadstate 10\npress y+p2.y 40\nsavestate 9\n'
                              'wait 12\npress y 40\nsavestate 8\nwait 5\n')
                out = run(name, fixtures[two], script, 170, narrow, bounce)
                first, last = state(out, 9), state(out, 8)
                key_slot = 2 if two else 1
                if two:
                    starts = re.findall(r'^f(\d+)\s+01:14A3=12 w1.*?\bX=([0-9A-F]{4})\b',
                                        (out/'wram-writes.log').read_text(), re.M)
                    assert len(starts) == 2 and {x for _, x in starts} == {'0000', '0001'}, (name, starts)
                    assert starts[0][0] == starts[1][0], f'{name}: secondary tongue input delayed'
                    mouth_slots = [e[2] for e in first.entities.values() if e[3] == 0x35]
                    mouths = [first.data[first.ram+0x160e+slot] for slot in mouth_slots]
                    assert mouths.count(key_slot) == 1 and mouths.count(255) == 1, (name, mouths)
                    assert first.data[first.ram+0x14c8+key_slot] == 7
                    same(last, state(out, 7))
                else:
                    held = first.word(first.actors[1][1]+56)
                    assert first.entities[held][2:5] == (1, 0x80, 1)
                    assert first.entities[held][6] == 1
                    assert first.data[first.ram+0x160e] == 255
                    assert first.data[first.ram+0x14c9] == 11
                    rows = list(csv.DictReader((out/'contacts.csv').open()))
                    held_updates = [r for r in rows if r['slot'] == '1' and r['phase'] == '1'
                                    and r['status'] == '11' and r['input'] == '64']
                    assert len(held_updates) == 40, (name, len(held_updates))
                    assert last.data[last.ram+0x160e] == 1 and last.data[last.ram+0x14c9] == 7
                    assert last.word(last.actors[1][1]+56) == 0xffffffff
                if bounce:
                    for f, expected in zip((first, last), parity[key]):
                        same(f, expected)
                else:
                    parity[key] = first, last
                results.append(dict(case=key, bounce=bounce, status='passed', evidence=name))
                (root/'summary.json').write_text(json.dumps(dict(
                    checks=results, campaign_coverage='unverified',
                    simultaneous_loose_object_arbitration='unverified'), indent=2)+'\n')
    print(f'{len(results)} mouth ownership checks passed; native/bounce WRAM and co-op snapshots match')


if __name__ == '__main__':
    main()
