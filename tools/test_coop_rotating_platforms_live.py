#!/usr/bin/env python3
"""Native rotating-platform regression; requires a copied, unmounted YI2 state.

Stage the real brown platform and let its original contact/rotation code run.
These are isolated mechanic fixtures, not an end-to-end campaign claim.
An optional prior executable demonstrates that the multi-rider checks fail.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from test_coop_common_alpha import field, sprite


def run(args, exe, source, script, out):
    script_path = out.with_suffix('.script')
    script_path.write_text(script, encoding='utf-8')
    subprocess.run([
        sys.executable, str(ROOT/'tools/run_coop_probe.py'),
        '--exe', str(exe), '--rom', str(args.rom), '--snapshot', str(source),
        '--script', str(script_path), '--out', str(out), '--frames', '180',
        '--capture', '62,80,90',
    ], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    result = json.loads((out/'result.json').read_text())
    assert not result['dispatch_misses'] and result['returncode'] == 0


def placed(source, output, case):
    f = Fixture(source)
    px = int.from_bytes(f.data[f.ram+0x14b8:f.ram+0x14ba], 'little')
    py = int.from_bytes(f.data[f.ram+0x14ba:f.ram+0x14bc], 'little')
    if case == 'secondary-primary':
        f.put(f.core+20, 1)
        f.data[f.ram+0xdb3] = 1
    for player in f.actors:
        riding = case != 'empty' and (player == 0 or case != 'one')
        f.actor(player, px-16+24*player if riding else 32+32*player,
                py-40 if riding else 352, 1, player+1)
        for at, value in ((0x1471, 0), (0x1490, 0), (0x1497, 0), (0x76, 1)):
            field(f, player, at, value)
    f.data[f.ram+0xc2] = px & 255  # previous platform X: no invented initial carry
    f.data[f.ram+0x14] = 0
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    f.camera(0)
    f.save(output)


def observe(path):
    f = Fixture(path)
    actors = {}
    for player in f.actors:
        actors[player] = dict(
            x=field(f, player, 0x94)+256*field(f, player, 0x95),
            y=field(f, player, 0x96)+256*field(f, player, 0x97),
            standing=field(f, player, 0x1471), life=f.word(f.actors[player][1]+8),
            reserve=field(f, player, 0xdc2), visible_pieces=f.word(f.actors[player][0]-4))
    return f, dict(actors=actors,
                   platform_y=int.from_bytes(f.data[f.ram+0x14ba:f.ram+0x14bc], 'little'),
                   rotation=[f.data[f.ram+at] for at in (0x1504, 0x1510, 0x151c, 0x1528)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'snapshot', 'out'):
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--reference-exe', type=Path)
    args = parser.parse_args()
    if not args.exe or not args.rom or not args.snapshot:
        print('UNAVAILABLE: supply a built --exe, private USA --rom and unmounted YI2 --snapshot.')
        return 0
    if not args.out:
        parser.error('supply a new --out directory')
    args.out.mkdir(parents=True, exist_ok=False)
    f = Fixture(args.snapshot)
    assert f.level == 0x106 and not f.mounts and set(f.actors) == {0, 1}
    f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
    sprite(f, 0x5f, 240, 408)
    for at, value in ((0x151c, 0x80), (0x1528, 1), (0x1504, 0),
                      (0x1510, 0), (0xc2, 0), (0x161a, 0xff)):
        f.data[f.ram+at] = value
    for player in f.actors:
        f.actor(player, 32+32*player, 352, 1, player+1)
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    f.camera(0)
    initial = args.out/'initial.sav'
    f.save(initial)
    run(args, args.exe, initial,
        'wait 60\nloadstate 10\nwait 1\nsavestate 11\n', args.out/'initialize')
    source = args.out/'initialize/saves/native-coop-v1/save11.sav'
    reports = {}
    for variant, exe in [('candidate', args.exe), ('reference', args.reference_exe)]:
        if exe is None:
            continue
        report = {}
        for case in ('one', 'two', 'secondary-primary', 'empty'):
            staged = args.out/(case+'.sav')
            if not staged.exists():
                placed(source, staged, case)
            out = args.out/(variant+'-'+case)
            # Loading simulates the restored frame immediately. The second
            # wait is one shorter so both observations cover the same span.
            run(args, exe, staged,
                'wait 60\nloadstate 10\nwait 28\nsavestate 11\n'
                'savestate 12\nwait 20\nsavestate 13\n'
                'loadstate 12\nwait 19\nsavestate 14\n', out)
            final, result = observe(out/'saves/native-coop-v1/save11.sav')
            riders = [0] if case == 'one' else [] if case == 'empty' else [0, 1]
            failures = []
            for player in riders:
                actor = result['actors'][player]
                if actor['life'] != 0 or actor['standing'] != 3 or actor['y']+40 != result['platform_y']:
                    failures.append(f'player {player} lost native footing')
                if actor['reserve'] != player+1:
                    failures.append(f'player {player} lost its reserve item')
                if not actor['visible_pieces']:
                    failures.append(f'player {player} disappeared after its contact redraw')
            if len(riders) == 2 and result['actors'][1]['x']-result['actors'][0]['x'] != 24:
                failures.append('rider positions were copied or carried inconsistently')
            before, _ = observe(out/'saves/native-coop-v1/save13.sav')
            after, _ = observe(out/'saves/native-coop-v1/save14.sav')
            if before.data[before.ram:before.ram+0x20000] != after.data[after.ram:after.ram+0x20000]:
                failures.append('current-state continuation changed WRAM')
            if before.data[before.machine:before.end] != after.data[after.machine:after.end]:
                failures.append('current-state continuation changed co-op actors/world')
            result['failures'] = failures
            report[case] = result
        if report['one']['rotation'] != report['two']['rotation']:
            report['two']['failures'].append('platform advanced differently with two riders')
        reports[variant] = report
    (args.out/'results.json').write_text(json.dumps(reports, indent=2)+'\n')
    print(json.dumps(reports, indent=2))
    failed = any(v['failures'] for v in reports['candidate'].values())
    if args.reference_exe:
        failed |= not reports['reference']['two']['failures']
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
