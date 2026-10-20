#!/usr/bin/env python3
"""Muted, finite extended-object contacts with native collision responses.

The copied fixtures position hazards and actors directly. They qualify contact
mechanics, not the producer, campaign route, or stage completion.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT


def field(f, player, address):
    return f.data[f.actors[player][0]+f.offsets[address]]


def set_field(f, player, address, value):
    f.data[f.actors[player][0]+f.offsets[address]] = value
    if player == f.word(f.core+20):
        f.data[f.ram+address] = value


def projectile(f, kind, x, y):
    f.data[f.ram+0x170b:f.ram+0x1783] = bytes(120)
    for table, value in enumerate((kind, y & 255, x & 255, y >> 8, x >> 8,
                                   0, 0, 0, 0, 1, 0x90, 0)):
        f.data[f.ram+0x170b+table*10] = value


def make(source, output, kind=2, scenario='secondary', margin=None):
    f = Fixture(source)
    assert not f.mounts and f.version == 5 and f.level == 0x106
    f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
    x, y, camera = (40, 352, 128) if margin == 'left' else (300, 304, 0) if margin else (88, 352, 0)
    for player in f.actors:
        distant = (player == 0 and scenario in ('secondary', 'death-second')) or (player == 1 and scenario == 'primary')
        power = 0 if scenario == 'death-both' or (scenario == 'death-second' and player == 1) else 1
        separated_x = x-48 if x >= 48 else x+48
        f.actor(player, separated_x if distant else x, y, power, 0,
                life=3 if scenario == 'bubble' and player else 0,
                recovery=999 if scenario == 'bubble' and player else 0)
        for a in (0x1497, 0x1490, 0x187a, 0x13f9):
            set_field(f, player, a, 0)
    if scenario == 'nearest':
        f.actor(0, x-4, y, 1, 0)
    if scenario in ('star-first', 'both-star'):
        set_field(f, 0, 0x1490, 120)
    if scenario in ('star-second', 'both-star'):
        set_field(f, 1, 0x1490, 120)
    if scenario == 'invulnerable':
        set_field(f, 1, 0x1497, 60)
    if scenario == 'plane':
        # Test the native priority predicate with a persistent behind-scenery
        # plane. A real fence-side fixture needs climbable terrain; setting
        # plane 1 in ordinary YI2 correctly resets it during terrain contact.
        set_field(f, 1, 0x13f9, 2)
    projectile(f, kind, x+8, y+(24 if scenario.startswith('death-') else 8))
    f.camera(camera)
    f.put(f.core+92, 448 if margin else 256)
    for offset, value in ((4, camera+128), (12, x+8), (16, y+19),
                          (20, 2), (24, int(bool(margin))), (28, int(bool(margin)))):
        f.put(f.focus+offset, value)
    f.save(output)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--mounted', type=Path)
    ap.add_argument('--cases', nargs='+', help='Run only these named cases')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    results, parity = [], {}
    cases = [(f'hazard-{kind:02x}', kind, 'secondary', None)
             for kind in (2, 3, 4, 6, 0xb, 0xc, 0xd)]
    cases += [(s, 2, s, None) for s in
              ('primary', 'both', 'death-second', 'death-both', 'star-first',
               'star-second', 'both-star', 'invulnerable', 'plane', 'bubble')]
    cases += [(f'coin-{s}', 0xa, s, None) for s in ('secondary', 'both', 'nearest')]
    cases += [(f'margin-{s}', 2, 'secondary', s) for s in ('left', 'right')]
    if args.mounted:
        cases.append(('mounted', 2, 'mounted', None))
    if args.cases:
        assert set(args.cases) <= {case[0] for case in cases}
        cases = [case for case in cases if case[0] in args.cases]
    script = root/'contact.script'
    script.write_text('wait 60\nloadstate 10\nwait 2\nsavestate 11\nwait 5\n',
                      encoding='ascii', newline='\n')
    for bounce in (False, True):
        for narrow in (True, False):
            for name, kind, scenario, margin in cases:
                if narrow and margin:
                    continue
                key = f'{name}-{"standard" if narrow else "wide"}'
                stem = f'{key}-{"bounce" if bounce else "native"}'
                snapshot, out = root/f'{stem}.sav', root/stem
                if scenario == 'mounted':
                    f = Fixture(args.mounted)
                    assert len(f.mounts) == 2
                    g, rec = f.actors[1]
                    projectile(f, kind, f.word(rec+20), f.word(rec+24)-4)
                    for player in f.actors:
                        set_field(f, player, 0x1497, 0)
                        set_field(f, player, 0x1490, 0)
                    f.save(snapshot)
                else:
                    make(args.source, snapshot, kind, scenario, margin)
                before = Fixture(snapshot)
                command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                           '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                           '--snapshot', str(snapshot), '--script', str(script), '--out', str(out),
                           '--frames', '75', '--framedump', '62', '--extended-trace', '--audit-queries']
                if narrow:
                    command.append('--native')
                if bounce:
                    command.append('--bounce')
                subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
                run = json.loads((out/'result.json').read_text())
                assert run['returncode'] == 0 and not run['dispatch_misses'] and not run['timed_out'], stem
                f = Fixture(out/'saves/native-coop-v1/save11.sav')
                rows = list(csv.DictReader((out/'extended.csv').open()))
                assert rows and all(int(r['query_audits']) > 0 for r in rows), stem
                responses = [r for r in rows if r['phase'] == '0']
                first = [r for r in responses if r['frame'] == responses[0]['frame']]
                players = [int(r['player']) for r in first]
                if kind == 0xa:
                    expected = 0 if scenario == 'both' else 1
                    assert players == [expected], (stem, players)
                    assert f.data[f.ram+0x170b] == 0
                    assert f.data[f.ram+0x18e3] == (before.data[before.ram+0x18e3]+1) & 255
                    # CODE_05B34A queues CoinAdder; the HUD stage consumes it.
                    # Count both halves so the assertion is independent of
                    # which completed frame processed the pending credit.
                    before_coins = before.data[before.ram+0xdbf]+before.data[before.ram+0x13cc]
                    after_coins = f.data[f.ram+0xdbf]+f.data[f.ram+0x13cc]
                    assert after_coins == (before_coins+1) % 100, (stem, before_coins, after_coins)
                elif scenario == 'mounted':
                    assert players == [1], (stem, players)
                    assert field(f, 0, 0x187a) and not field(f, 1, 0x187a)
                    old_mount = before.word(before.actors[1][1]+52)
                    # Stable entity identity remains, but the rider is released.
                    assert old_mount in f.entities, (old_mount, f.entities)
                    assert f.entities[old_mount][4] == 0xffffffff
                elif scenario.startswith('death-'):
                    assert players == ([0, 1] if scenario == 'death-both' else [1]), (stem, players)
                    for player, (_, rec) in f.actors.items():
                        dying = scenario == 'death-both' or player == 1
                        assert f.word(rec+8) == int(dying), (stem, player, f.word(rec+8))
                else:
                    stars = {0} if scenario == 'star-first' else {1} if scenario == 'star-second' else {0, 1} if scenario == 'both-star' else set()
                    skipped = {1} if scenario in ('invulnerable', 'plane', 'bubble') else set()
                    touched = {1} if scenario == 'secondary' else {0} if scenario == 'primary' else {0, 1}
                    for p in (0, 1):
                        damaged = p in touched-stars-skipped
                        assert field(f, p, 0x19) == int(not damaged), (stem, p, field(f, p, 0x19))
                    if stars:
                        assert f.data[f.ram+0x170b] == 1
                        assert len(first) == (1 if len(stars) == 2 else 2)
                        assert players == ([0] if len(stars) == 2 else [1, 0] if 0 in stars else [0, 1])
                    if scenario in ('plane', 'bubble'):
                        assert players == [0], (stem, players)
                    if margin:
                        sx = f.data[f.ram+0x171f]+256*f.data[f.ram+0x1733]
                        camera = int.from_bytes(f.data[f.ram+0x1a:f.ram+0x1c], 'little')
                        assert sx < camera if margin == 'left' else sx-camera >= 256
                if name in ('primary', 'both'):
                    control_key = (narrow, bounce)
                    bank = bytes(f.data[f.ram+0x170b:f.ram+0x1783])
                    if name == 'primary':
                        parity[control_key] = bank
                    elif control_key in parity:
                        assert bank == parity[control_key], f'{stem}: world projectile updated per actor'
                payload = (bytes(f.data[f.machine:f.end]), bytes(f.data[f.ram:f.ram+0x20000]))
                if bounce:
                    assert payload == parity[key], f'{stem}: full WRAM/CNR mismatch'
                else:
                    parity[key] = payload
                results.append(dict(case=key, bounce=bounce, status='passed', evidence=stem,
                                    first_response_order=players))
                (root/'summary.json').write_text(json.dumps(dict(
                    checks=results, campaign_coverage='unverified',
                    limitations='Actors and hazards placed offline; producers and routes bypassed.'), indent=2)+'\n')
    print(f'{len(results)} extended-contact checks passed; native/bounce WRAM and co-op snapshots match')


if __name__ == '__main__':
    main()
