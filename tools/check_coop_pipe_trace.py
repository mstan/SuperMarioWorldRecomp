#!/usr/bin/env python3
"""Check a running trace produced from make_coop_pipe_fixture.py."""
import argparse
import csv
from pathlib import Path


def check(path, entrant, replayed):
    rows = list(csv.DictReader(path.open(newline='')))
    assert len(rows) > 100 and {r['player'] for r in rows} == {'0', '1'}
    for row in rows:
        assert row['stack'] == '511' and row['lives'] == '5' and row['life'] == '0'
        assert (row['power'], row['reserve']) == (('1', '1') if row['player'] == '0' else ('3', '4'))
        assert row['sublevel'] in ('0', '1'), 'room transition committed more than once'
    commits = [r for r in rows if r['mode'] == '15']
    assert len(commits) == 2 and {r['player'] for r in commits} == {'0', '1'}
    assert {r['room_request'] for r in commits} == {str(entrant)}
    assert {r['sublevel'] for r in commits} == {'1'}
    assert {r['level_data'] for r in commits} == {str(0x07c532)}, 'wrong source room'
    arrived = [r for r in rows if r['level_data'] == str(0x07c57f)]
    assert len(arrived) > 100 and {r['player'] for r in arrived} == {'0', '1'}
    assert all(r['sublevel'] == '1' and r['mode'] == '20' for r in arrived)
    assert all(r['animation'] == '0' for r in rows[-2:]), 'entrance did not finish'
    phases = [[]]
    last_frame = None
    for row in arrived:
        frame = int(row['frame'])
        if last_frame is not None and frame < last_frame:
            phases.append([])
        phases[-1].append(row)
        last_frame = frame
    repeats = 0
    if replayed:
        assert len(phases) == 2 and len(phases[0]) > 100, 'missing destination replay'
        assert len(phases[1]) >= len(phases[0]), 'replay ended early'
        # Entrance animations deliberately freeze the gameplay/world counters.
        # Compare ordered records, not a key that aliases those frozen frames.
        for index, row in enumerate(phases[0]):
            assert row == phases[1][index], f'transition replay diverged at destination row {index}'
        repeats = len(phases[0])
    print(f'Pipe trace passed: entrant={entrant}, arrival rows={len(arrived)}, identical replays={repeats}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--entrant', type=int, choices=(0, 1), required=True)
    parser.add_argument('--replayed', action='store_true')
    args = parser.parse_args()
    check(args.trace, args.entrant, args.replayed)
