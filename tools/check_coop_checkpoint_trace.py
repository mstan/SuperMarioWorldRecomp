#!/usr/bin/env python3
"""Validate focused live YI2 checkpoint, recovery, and retry traces."""
import argparse
import csv
from pathlib import Path


def check(path, waiting=None, strong=None, replayed=False, retry=False):
    rows = list(csv.DictReader(path.open(newline='')))
    assert len(rows) > 100 and {r['player'] for r in rows} == {'0', '1'}
    for row in rows:
        assert row['stack'] == '511'
        assert row['reserve'] == ('1' if row['player'] == '0' else '4')
    if retry:
        assert {r['lives'] for r in rows} == {'5', '4'}
        changes = sum(a['lives'] != b['lives'] for a, b in zip(rows, rows[1:]))
        assert changes == 1, 'retry must charge one shared life'
        for row in rows[-2:]:
            assert row['power'] == '0' and row['life'] == '0' and row['mode'] == '20'
            assert row['checkpoint'] == '43' and row['level_data'] == str(0x07c532)
            assert row['x'] == '2328', 'wrong YI2 midway entrance'
        print('Checkpoint retry passed: one life, original midway entrance, small actors, reserves retained')
        return
    active = [r for r in rows if r['checkpoint'] == '43']
    assert len(active) > 100
    assert all(r['lives'] == '5' for r in rows)
    for row in active:
        assert row['power'] == ('3' if row['player'] == str(strong) else '1')
    if waiting is not None:
        bubble = [r for r in active if r['player'] == str(waiting) and r['life'] == '2']
        assert bubble and all(r['checkpoint_upgrade'] == '1' for r in bubble)
        returned = next(r for r in active if r['player'] == str(waiting) and r['life'] == '0')
        assert returned['protection'] == '120' and returned['checkpoint_upgrade'] == '0'
    seen, repeats = {}, 0
    for row in rows:
        key = row['frame'], row['player']
        if key in seen:
            assert row == seen[key], f'checkpoint restore diverged at {key}'
            repeats += 1
        seen[key] = row
    if replayed:
        assert repeats > 100
    print(f'Checkpoint passed: {len(active)} upgraded actor records, {repeats} identical replays')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--waiting', type=int, choices=(0, 1))
    parser.add_argument('--strong', type=int, choices=(0, 1))
    parser.add_argument('--replayed', action='store_true')
    parser.add_argument('--retry', action='store_true')
    args = parser.parse_args()
    check(args.trace, args.waiting, args.strong, args.replayed, args.retry)
