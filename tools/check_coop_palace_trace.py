#!/usr/bin/env python3
"""Check native palace activation, one team clear, and retained equipment."""
import argparse
import csv
from pathlib import Path


def read(path):
    return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(path.open())]


def check(path, compare=None):
    rows = read(path)
    assert rows and all(r['stack'] == 511 and r['lives'] == 5 for r in rows)
    if compare:
        assert rows == read(compare), 'palace execution modes diverged'
    assert all(r['reserve'] == (1 if r['player'] == 0 else 4) for r in rows)
    assert any(r['player'] == 0 and r['life'] == 2 for r in rows)
    lead = [r for r in rows if r['player'] == 0]
    exits = [r for r in lead if r['exit_player'] != 0xffffffff]
    assert len(exits) == 1 and exits[0]['exit_player'] == 1
    assert exits[0]['end_timer'] == 8 and exits[0]['exit_flags'] == 0
    cleared = [r for r in rows if r['outcome'] == 3]
    assert cleared and cleared[-1]['mode'] == 11
    assert all(r['frame'] == exits[0]['frame'] for r in cleared)
    assert all(r['life'] == 0 and r['power'] == (0 if r['player'] == 0 else 3)
               for r in cleared)
    timers = [r['end_timer'] for r in cleared if r['player'] == 0]
    assert set(timers) == set(range(9))
    assert all(a-b in (0, 1) for a, b in zip(timers, timers[1:]))
    print(f'Palace team clear and frozen scene passed: {len(rows)} actor records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    check(args.trace, args.compare)
