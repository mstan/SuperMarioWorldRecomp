#!/usr/bin/env python3
"""Original Yoshi shell acquisition, spitting, wing motion and landing effects.

Two existing riders, their colors and loose shells are staged. Tongue claims,
shell abilities, spitting, movement and effect production run native gameplay.
This does not qualify projectile damage, wing exits or campaign routes.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_keyhole_fixture import sprite_type
from test_coop_mouth_live import mouth_by_player, same

PALETTES = (4, 6, 8, 10)  # yellow, blue, red, green


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav')


def births(path):
    pending, found = {}, []
    for line in path.read_text().splitlines():
        match = re.match(r'f(\d+)\s+[0-9A-F]{2}:([0-9A-F]{4})=([0-9A-F]{2}) w1\b', line)
        if not match:
            continue
        frame, address, value = int(match[1]), int(match[2], 16), int(match[3], 16)
        if 0x170b <= address < 0x1715 and value in (15, 17):
            pending[address-0x170b] = dict(frame=frame, type=value, fields={})
            continue
        if not 0x1715 <= address < 0x1779:
            continue
        table, slot = divmod(address-0x170b, 10)
        if slot not in pending:
            continue
        event = pending[slot]
        event['fields'][table] = value
        if table == 10:
            fields = event.pop('fields')
            assert all(i in fields for i in (1, 2, 3, 4, 6)), (path, event, fields)
            event.update(slot=slot, x=fields[2]+256*fields[4],
                         y=fields[1]+256*fields[3], vx=fields[6])
            event['actor'] = min((0, 1), key=lambda p: abs(event['x']-(40 if p == 0 else 160)))
            found.append(event)
            del pending[slot]
    assert not pending, (path, pending)
    return found


def seed(source, output, palette_index, shell):
    f = Fixture(source)
    expected = {}
    for player in (0, 1):
        palette = PALETTES[(palette_index+player) % 4]
        kind = 4+(shell-4+player) % 4
        identity = f.word(f.actors[player][1]+52)
        mount = f.entities[identity][2]
        assert f.data[f.ram+0x160e+mount] == 255
        f.data[f.ram+0x15f6+mount] = (f.data[f.ram+0x15f6+mount] & 0xf1) | palette
        f.data[f.mount_records[identity]+13] = palette
        sprite_type(f, player+2, kind)
        f.data[f.ram+0x14ca+player] = 9
        expected[player] = dict(palette=palette, shell=kind,
                                wings=palette == 6 or kind == 6,
                                stomp=palette == 4 or kind == 7,
                                fire=palette == 8 or kind == 5)
    f.save(output)
    return expected


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='two separated native riders and loose keys in slots 2/3')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    scripts = {
        'spit': ('wait 60\nloadstate 10\npress y+p2.y 40\nwait 1\nsavestate 9\n'
                 'wait 8\npress y+p2.y 1\nwait 1\nsavestate 8\n'
                 'loadstate 9\nwait 8\npress y+p2.y 1\nwait 1\nsavestate 7\nwait 5\n'),
        'flight': ('wait 60\nloadstate 10\n'+('press b+p2.b 1\nwait 13\n'*4)+
                   'press b+p2.b 1\nwait 1\nsavestate 9\nwait 100\nsavestate 8\n'
                   'loadstate 9\nwait 100\nsavestate 7\nwait 5\n'),
    }
    for kind, content in scripts.items():
        (root/f'{kind}.script').write_text(content)
    fixtures, results, parity = {}, [], {}
    for index, palette in enumerate(PALETTES):
        for shell in range(4, 8):
            key = f'y{palette}-s{shell}'
            path = root/f'{key}.sav'
            fixtures[key] = path, seed(args.source, path, index, shell)

    def run(kind, source, out, narrow, bounce):
        frames = 175 if kind == 'spit' else 365
        cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
               '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
               '--snapshot', str(source), '--script', str(root/f'{kind}.script'),
               '--out', str(out), '--frames', str(frames),
               '--framedump', '125' if kind == 'flight' else '115',
               '--wram-watch', '170b:177f', '--mouth-trace']
        if narrow:
            cmd.append('--native')
        if bounce:
            cmd.append('--bounce')
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert not report['returncode'] and not report['dispatch_misses'] and not report['timed_out']

    def record(key, out, bounce, snapshots, events):
        if bounce:
            for a, b in zip(snapshots, parity[key]):
                same(a, b)
        else:
            parity[key] = snapshots
        rows = list(csv.DictReader((out/'coop.csv').open()))
        assert all(row['life'] == '0' for row in rows), out
        results.append(dict(case=key, bounce=bounce, status='passed', evidence=out.name,
                            effect_births=events))
        (root/'summary.json').write_text(json.dumps(dict(
            checks=results, campaign='unverified', projectile_damage='unverified',
            wing_destination='unverified'), indent=2)+'\n')

    for bounce in (False, True):
        for narrow in (True, False):
            suffix = f'{"standard" if narrow else "wide"}-{"bounce" if bounce else "native"}'
            for case, (source, expected) in fixtures.items():
                out = root/f'{case}-spit-{suffix}'
                run('spit', source, out, narrow, bounce)
                eaten, spit, replay = (state(out, slot) for slot in (9, 8, 7))
                assert mouth_by_player(eaten) == {0: 2, 1: 3}
                assert mouth_by_player(spit) == {0: 255, 1: 255}
                assert all(eaten.data[eaten.ram+0x14ca+p] == 7 for p in (0, 1))
                events = births(out/'wram-writes.log')
                # The input script repeats the spit after loading the retained
                # mouth state. Both executions must emit the same allowance.
                for player in (0, 1):
                    fire = expected[player]['fire']
                    assert sum(e['type'] == 17 and e['actor'] == player for e in events) == (6 if fire else 0), (out, events)
                    assert spit.data[spit.ram+0x14ca+player] == (0 if fire else 10), out
                assert sum(x == 17 for x in spit.data[spit.ram+0x170b:spit.ram+0x1715]) == 3*sum(x['fire'] for x in expected.values())
                same(spit, replay)
                key = f'{case}-spit-{"standard" if narrow else "wide"}'
                record(key, out, bounce, (eaten, spit), events)
                source = out/'saves/native-coop-v1/save9.sav'
                out = root/f'{case}-flight-{suffix}'
                run('flight', source, out, narrow, bounce)
                flying, landed, replay = (state(out, slot) for slot in (9, 8, 7))
                assert mouth_by_player(landed) == {0: 2, 1: 3}
                for player in (0, 1):
                    assert (flying.word(flying.actors[player][1]+24) < 320) == expected[player]['wings'], out
                    assert landed.word(landed.actors[player][1]+24) == 368, out
                events = births(out/'wram-writes.log')
                for player in (0, 1):
                    waves = [e for e in events if e['type'] == 15 and e['actor'] == player]
                    assert bool(waves) == expected[player]['stomp'], (out, player, waves)
                    assert len(waves) % 2 == 0
                same(landed, replay)
                key = f'{case}-flight-{"standard" if narrow else "wide"}'
                record(key, out, bounce, (flying, landed), events)
    print(f'{len(results)} shell acquisition/spit/wing/landing cases passed with parity and replay')


if __name__ == '__main__':
    main()
