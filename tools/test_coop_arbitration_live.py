#!/usr/bin/env python3
"""Exercise contested native contacts with finite isolated muted launches.

Requires the original ROM and a local copied CNR5 YI2 entrance state. Retains
inputs, fixtures, native contact outcomes, captures and execution comparisons.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from make_coop_arbitration_fixture import CASES, POWERUPS, make
from coop_fixture import Fixture

ROOT = Path(__file__).resolve().parents[1]


def read_rows(path):
    with path.open() as source:
        return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(source)]

def check_dual(out, case, native):
    rows = read_rows(out/'arbitration.csv')
    first = min(r['frame'] for r in rows)
    winners = [r for r in rows if r['frame'] == first and r['phase'] == 1 and r['winner']]
    assert [(r['slot'], r['player'], r['status']) for r in winners] == [(1, 0, 0), (0, 1, 0)], winners
    assert all(r['kind'] == 9 and r['query_audits'] for r in winners), winners
    assert winners[-1]['rewards'] == 2, winners
    actors = read_rows(out/'coop.csv')
    assert actors and all(r['stack'] == 511 for r in actors), 'Guest stack imbalance'
    assert len(actors) % 2 == 0
    frames = list(zip(actors[::2], actors[1::2]))
    unlocked = next(i for i, pair in enumerate(frames) if pair[0]['lock'] == 0)
    assert unlocked == 48, f'World freeze lasted {unlocked}, expected native mushroom 48 frames'
    assert all(r['frame'] == first for pair in frames[:unlocked] for r in pair)
    short = 0 if case.endswith('primary-short') else 1
    assert frames[32][short]['animation'] == 0 and frames[32][1-short]['animation'] == 2
    state = Fixture(out/'saves/native-coop-v1/save11.sav')
    assert state.data[state.ram+0x9d] == 0
    expected = (3, 1) if short == 0 else (1, 3)
    for player, (guest, record) in state.actors.items():
        assert state.data[guest+state.offsets[0x19]] == expected[player]
        assert state.data[guest+state.offsets[0x71]] == 0
        assert state.word(record+20) > 344, 'Post-transformation movement did not resume'
    capture = 'frames/frame_000080.bmp' if native else 'frame-000080.bmp'
    assert (out/capture).is_file(), 'Missing captured evidence'
    return {'status': 'passed', 'frame': first, 'world_freeze_frames': unlocked,
            'responses': winners}


def check(out, case, native=False):
    result = json.loads((out/'result.json').read_text())
    assert result['returncode'] == 0 and not result['timed_out'], result
    assert not result['dispatch_misses'], result
    if case.startswith('dual-transform'):
        return check_dual(out, case, native)
    rows = [r for r in read_rows(out/'arbitration.csv') if r['slot'] == 0]
    assert rows, 'No executed contact evidence'
    first = min(r['frame'] for r in rows)
    hit = [r for r in rows if r['frame'] == first]
    queries = [r for r in hit if r['phase'] == 2]
    before = [r for r in hit if r['phase'] == 0]
    after = [r for r in hit if r['phase'] == 1]
    item = case.split('-')[0]
    powerup = item in POWERUPS
    assert len(before) == len(after) and before, 'Incomplete response evidence'
    assert all(r['query_audits'] > 0 for r in queries), 'Query audit never executed'
    assert {r['player'] for r in queries} == (
        {1} if case == 'pickup-fence' else {0, 1}), queries
    winners = [r for r in after if r['winner']]
    if case in ('stomp', 'spin', 'star') or case.startswith('pickup') or powerup:
        expected = 1 if case.endswith('-nearest') or case in ('pickup-eligible', 'pickup-fence') else 0
        assert len(winners) == 1 and winners[0]['player'] == expected, winners
    else:
        assert not winners, winners
    if case in ('stomp', 'spin', 'spiny'):
        assert {r['player'] for r in after} == {0, 1}, after
        assert all(r['vy'] > 0 for r in before) and all(r['vy'] < 0 for r in after), hit
        assert all(r['animation'] == 0 for r in after), hit
        if case != 'spiny':
            assert all(r['chain'] == b['chain']+1 for b, r in zip(before, after)), hit
            assert winners[0]['status'] == (9 if case == 'stomp' else 4), winners
            assert after[-1]['status'] == winners[0]['status'], hit
            assert after[-1]['rewards'] == before[0]['rewards']+1, hit
        else:
            assert all(r['status'] == 8 for r in after), hit
    elif case == 'star':
        assert winners[0]['status'] == 2, winners
        assert after[-1]['rewards'] == before[0]['rewards']+1, hit
    elif case == 'side':
        assert {r['player'] for r in after if r['animation'] == 9} == {0, 1}, hit
    elif case == 'protected':
        assert {r['player']: r['animation'] for r in after} == {0: 0, 1: 9}, hit
    elif powerup:
        assert winners[0]['kind'] == 9 and winners[0]['status'] == 0, hit
        assert len(after) == 1, hit
        assert winners[0]['rewards'] == before[0]['rewards']+1, hit
        if item == 'invincibility':
            assert winners[0]['star'] == 255, hit
        if item in ('mushroom', 'flower'):
            assert winners[0]['animation'] == (2 if item == 'mushroom' else 4), hit
        if item == 'feather':
            assert winners[0]['power'] == 2 and winners[0]['animation'] == 3, hit
        actors = read_rows(out/'coop.csv')
        if item == 'feather':
            unlocked = next(i//2 for i, row in enumerate(actors) if row['lock'] == 0)
            assert unlocked == 24, f'Cape freeze lasted {unlocked}, expected 24 frames'
        # The noncollector must retain its small form through the native
        # shared transformation freeze and the following gameplay.
        assert all(r['power'] == 0 for r in actors if r['player'] != expected), actors[-2:]
        state = Fixture(out/'saves/native-coop-v1/save11.sav')
        assert state.data[state.ram+0x9d] == 0, 'Transformation never released world freeze'
        assert all(state.data[guest+state.offsets[0x71]] == 0
                   for guest, _ in state.actors.values()), 'Transformation never finished'
        powers = {player: state.data[guest+state.offsets[0x19]]
                  for player, (guest, _) in state.actors.items()}
        expected_power = {'mushroom': 1, 'flower': 3, 'feather': 2}.get(item, 0)
        assert powers == {player: expected_power if player == expected else 0
                          for player in powers}, powers
        if item == 'invincibility':
            stars = {player: state.data[guest+state.offsets[0x1490]]
                     for player, (guest, _) in state.actors.items()}
            assert {player for player, timer in stars.items() if timer} == {expected}, stars
        if item == 'life':
            assert state.data[state.ram+0xdbe] == before[0]['lives']+1
        if item == 'coin':
            assert state.data[state.ram+0xdbf] == before[0]['coins']+1
    else:
        assert winners[0]['kind'] == 7 and winners[0]['status'] == 11, winners
        actors = read_rows(out/'coop.csv')
        owners = [r for r in actors if r['frame'] == first and r['held_slot'] == 0]
        assert len(owners) == 1 and owners[0]['player'] == expected, owners
    actors = read_rows(out/'coop.csv')
    assert actors and all(r['stack'] == 511 for r in actors), 'Guest stack imbalance'
    capture = 'frames/frame_000080.bmp' if native else 'frame-000080.bmp'
    assert (out/capture).is_file(), 'Missing captured evidence'
    return {'status': 'passed', 'frame': first, 'queries': queries, 'responses': after}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'source', 'out'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    parser.add_argument('--cameras', type=int, nargs='+', choices=(0, 192, 384), default=[192])
    parser.add_argument('--execution', choices=('floor', 'bounce', 'both'), default='both')
    parser.add_argument('--native', action='store_true')
    args = parser.parse_args()
    if args.native and args.cameras != [192]:
        parser.error('Native-width cases use the center fixture (--cameras 192)')
    for name in ('exe', 'rom', 'source', 'out'):
        setattr(args, name, getattr(args, name).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    modes = [0, 1] if args.execution == 'both' else [int(args.execution == 'bounce')]
    results = []
    for bounce in modes:
        for camera in args.cameras:
            for case in args.cases:
                name = f'{"bounce" if bounce else "floor"}-{case}-{camera}'
                fixture = args.out/(name+'.sav')
                script = fixture.with_suffix('.script')
                script.write_text(make(args.source, fixture, case, camera),
                                  encoding='ascii', newline='\n')
                out = args.out/name
                command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                           '--exe', str(args.exe), '--rom', str(args.rom),
                           '--snapshot', str(fixture), '--script', str(script),
                           '--out', str(out), '--frames', '155', '--capture', '70,80',
                           '--audit-queries']
                if bounce:
                    command.append('--bounce')
                if args.native:
                    command.extend(['--native', '--framedump', '80'])
                completed = subprocess.run(command, capture_output=True, text=True)
                report = {'case': name, 'status': 'failed'}
                try:
                    assert completed.returncode == 0, completed.stdout+completed.stderr
                    report.update(check(out, case, args.native))
                except (AssertionError, OSError, ValueError) as error:
                    report['error'] = str(error)
                results.append(report)
                (args.out/'results.json').write_text(json.dumps(results, indent=2)+'\n',
                                                     encoding='utf-8', newline='\n')
                print(f'{name}: {report["status"]} {report.get("error", "")}', flush=True)
    parity = []
    if args.execution == 'both':
        for floor in sorted(args.out.glob('floor-*')):
            if not floor.is_dir():
                continue
            bounce = args.out/floor.name.replace('floor-', 'bounce-', 1)
            captures = (('frames/frame_000080.bmp',) if args.native else
                        ('frame-000070.bmp', 'frame-000080.bmp'))
            for name in ('coop.csv', 'contacts.csv', 'arbitration.csv', *captures):
                a, b = floor/name, bounce/name
                same = (a.is_file() and b.is_file() and
                        hashlib.sha256(a.read_bytes()).digest() == hashlib.sha256(b.read_bytes()).digest())
                parity.append({'case': floor.name[6:], 'file': name, 'passed': same})
        (args.out/'parity.json').write_text(json.dumps(parity, indent=2)+'\n',
                                          encoding='utf-8', newline='\n')
        print(f'Execution parity: {sum(p["passed"] for p in parity)}/{len(parity)}', flush=True)
    return int(any(r['status'] != 'passed' for r in results) or any(not p['passed'] for p in parity))


if __name__ == '__main__':
    raise SystemExit(main())
