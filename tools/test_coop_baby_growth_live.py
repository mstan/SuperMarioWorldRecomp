#!/usr/bin/env python3
"""Native baby feeding/growth while another mount retains private state.

Source sprites, positions and berry counters are staged offline. Food counts,
consumption, growth, carrier release and rewards must arise from native code.
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
from test_coop_mouth_live import same, set_actor

NONE = 0xffffffff


def transfer(f, rider):
    identity = f.word(f.actors[0][1]+52)
    assert identity in f.mounts and f.entities[identity][4] == 0
    if rider:
        f.put(f.actors[0][1]+52, NONE)
        f.put(f.actors[1][1]+52, identity)
        at = f.data.index(b'ENT1', f.core_end)+16
        for index, key in enumerate(f.entities):
            if key == identity:
                f.put(at+index*28+16, rider)
    for actor in (0, 1):
        mounted = actor == rider
        set_actor(f, actor, 40 if mounted else 200, 336 if mounted else 352, mounted)
        guest, _ = f.actors[actor]
        for addr, value in ((0x187a, int(mounted)), (0x188b, 6 if mounted else 0)):
            f.data[guest+f.offsets[addr]] = value
            if actor == 0:
                f.data[f.ram+addr] = value
    at = f.mount_records[identity]+12
    f.data[at+13:at+15] = bytes((7, 1))
    f.data[f.ram+0x18d4:f.ram+0x18d6] = bytes((7, 1))
    return identity


def seed(source, output, rider, kind):
    f = Fixture(source)
    identity = transfer(f, rider)
    assert f.data[f.ram+0x160e+f.entities[identity][2]] == 1
    foods = [(3, 0x74)] if kind == 'carried' else [(3, int(kind, 16))] if len(kind) == 2 else (
        [(slot, 0x78) for slot in (2, 3, 5, 6, 7)] if kind == 'five'
        else [(7, 0x74), (3, 4)])
    for slot, value in [(4, 0x2d), *foods]:
        loose_key(f, (104 if slot == 4 else 140) if kind == 'carried' else 80, 368, slot)
        sprite_type(f, slot, value)
        f.data[f.ram+0x14c8+slot] = 1 if value == 0x2d else 9 if value == 4 else 8
    if kind == 'carried':
        carrier = 1-rider
        set_actor(f, carrier, 88, 352, False)
        guest, _ = f.actors[carrier]
        f.data[guest+f.offsets[0x76]] = 1
        if carrier == 0:
            f.data[f.ram+0x76] = 1
    f.save(output)
    return identity, [slot for slot, _ in foods], f.data[f.ram+0xdbf]


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='one native Mario mount with key slot 1 in its mouth')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--cases', nargs='+', choices=('74', '75', '76', '77', 'five', 'fast', 'carried'),
                    default=('74', '75', '76', '77', 'five', 'fast', 'carried'))
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    script = root/'growth.script'
    script.write_text('wait 60\nloadstate 10\nwait 35\nsavestate 9\nwait 90\nsavestate 8\n'
                      'loadstate 9\nwait 90\nsavestate 7\nwait 5\n')
    carried_scripts = {}
    for carrier in (0, 1):
        prefix = 'p2.' if carrier else ''
        path = root/f'carried{carrier}.script'
        path.write_text(f'wait 60\nloadstate 10\npress {prefix}y+{prefix}right 90\n'
                        'wait 1\nsavestate 9\nwait 90\nsavestate 8\n'
                        'loadstate 9\nwait 90\nsavestate 7\nwait 5\n')
        carried_scripts[carrier] = path
    results, parity, fixtures = [], {}, {}
    for rider in (0, 1):
        for kind in args.cases:
            path = root/f'{rider}-{kind}.sav'
            fixtures[rider, kind] = (path, *seed(args.source, path, rider, kind))
    for bounce in (False, True):
        for narrow in (True, False):
            for (rider, kind), (source, identity, foods, coins) in fixtures.items():
                key = f'{rider}-{kind}-{"standard" if narrow else "wide"}'
                out = root/f'{key}-{"bounce" if bounce else "native"}'
                frames = 360 if kind == 'carried' else 300
                input_script = carried_scripts[1-rider] if kind == 'carried' else script
                cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                       '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                       '--snapshot', str(source), '--script', str(input_script), '--out', str(out),
                       '--frames', str(frames), '--framedump', str(frames-10), '--wram-watch', '1570:157b']
                if narrow:
                    cmd.append('--native')
                if bounce:
                    cmd.append('--bounce')
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
                report = json.loads((out/'result.json').read_text())
                assert not report['returncode'] and not report['dispatch_misses'] and not report['timed_out']
                growing, done, replay = (state(out, slot) for slot in (9, 8, 7))
                for f in (growing, done):
                    assert len(f.mounts) == 2, out
                    assert f.word(f.actors[rider][1]+52) == identity
                    assert list(f.mounts[identity][13:15]) == [7, 1]
                    old = f.entities[identity][2]
                    assert f.data[f.ram+0x160e+old] == 1 and f.data[f.ram+0x14c9] == 7
                    assert 0 < f.mounts[identity][5] < 249
                    newborn = next(i for i in f.mounts if i != identity)
                    assert f.entities[newborn][2] == 4 and f.entities[newborn][4] == NONE
                    assert f.mounts[newborn][0] == 0 and f.mounts[newborn][13:15] == bytes(2)
                    assert f.data[f.ram+0x1612] == 255
                    for slot in foods:
                        status, value = f.data[f.ram+0x14c8+slot], f.data[f.ram+0x9e+slot]
                        if kind == 'fast' and slot == 3:
                            assert status == 9 and value == 4
                        else:
                            food_type = 0x78 if kind == 'five' else 0x74 if kind in ('fast', 'carried') else int(kind, 16)
                            # Screen spawning can legitimately reuse a consumed
                            # food slot for an approaching native enemy.
                            assert status == 0 or value != food_type
                    assert f.data[f.ram+0xdbf] == coins+(4 if kind == 'five' else 0)
                assert 0 < growing.mounts[newborn][19] < 64 and done.mounts[newborn][19] == 0
                if kind == 'five':
                    counts = re.findall(r'^f\d+\s+0[13]:1574=0([1-5]) w1',
                                        (out/'wram-writes.log').read_text(), re.M)
                    assert counts == list('12345'), (out, counts)
                same(done, replay)
                if bounce:
                    for a, b in zip((growing, done), parity[key]):
                        same(a, b)
                else:
                    parity[key] = growing, done
                rows = list(csv.DictReader((out/'coop.csv').open()))
                assert all(row['life'] == '0' for row in rows)
                if kind == 'carried':
                    assert sum(r['player'] == str(1-rider) and r['held_slot'] == '4' for r in rows) >= 40
                    assert done.word(done.actors[1-rider][1]+56) == NONE
                results.append(dict(case=key, bounce=bounce, status='passed', evidence=out.name))
                (root/'summary.json').write_text(json.dumps(dict(
                    checks=results, campaign='unverified'), indent=2)+'\n')
    print(f'{len(results)} native growth cases passed with full state parity and replay')


if __name__ == '__main__':
    main()
