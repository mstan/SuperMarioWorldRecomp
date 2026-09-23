#!/usr/bin/env python3
"""Check native keyhole arbitration, shared window cadence, and restore."""
import argparse
import csv
from pathlib import Path


def read(path):
    return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(path.open())]


def check(path, winner, small, stars, compare, replay, candidates, bonus_start):
    rows = read(path)
    assert len(rows) > 100 and len(rows) % 2 == 0
    assert all(r['stack'] == 511 and r['pause'] == 0 for r in rows)
    if compare:
        assert rows == read(compare), 'compiled/interpreted keyhole divergence'
    if replay:
        starts = [i for i, r in enumerate(rows) if r == rows[0]]
        assert len(starts) == 2 and starts[1] > 100
        assert rows[:starts[1]] == rows[starts[1]:], 'keyhole restore diverged'
        print(f'Exact keyhole replay: {starts[1]} actor records')
        rows = rows[:starts[1]]
    lead = rows[::2]
    exits = [r for r in lead if r['exit_player'] != 0xffffffff]
    if replay:
        assert not exits
    else:
        assert len(exits) == 1
        assert exits[0]['exit_player'] == winner
        assert exits[0]['exit_flags'] == (stars << 8) | 1
        assert exits[0]['keyhole_timer'] == 48 and exits[0]['keyhole_direction'] == 0
        assert exits[0]['spotlight'] == 0
        assert exits[0]['exit_candidates'] == candidates
    clear = [r for r in lead if r['outcome'] == 3]
    stars_decimal = (stars >> 4)*10+(stars & 15)
    bonus = bonus_start+stars_decimal >= 100
    assert clear and clear[-1]['mode'] == (16 if bonus else 11) and clear[-1]['ow_exit'] == 2
    assert clear[-1]['keyhole_direction'] == 2 and clear[-1]['keyhole_timer'] == 0
    assert clear[-1]['spotlight'] == 0
    assert all(r['end_timer'] == 0 and r['lives'] >= 5 for r in rows)
    assert clear[-1]['lives'] == (8 if stars == 0x50 else 5)
    assert clear[-1]['bonus_stars'] == (bonus_start+stars_decimal) % 100
    assert len({(r['frame'], r['world_frame'], r['time']) for r in clear}) == 1
    if not replay:
        assert len(clear) == 122, 'shared keyhole cadence changed'
    for a, b in zip(clear, clear[1:]):
        assert b['spotlight']-a['spotlight'] in (-4, 0, 4)
        assert b['keyhole_direction']-a['keyhole_direction'] in (0, 1)
    for r in rows:
        assert r['reserve'] == (1 if r['player'] == 0 else 4)
        if r['outcome'] == 3:
            assert r['life'] == 0 and r['recovery'] == 0
            assert r['power'] == (0 if r['player'] == small else (1 if r['player'] == 0 else 3))
            assert r['goal_stars'] == 0  # credited even though there is no tally scene
    if bonus and not replay:
        returned = [r for r in rows if r['outcome'] == 0 and r['level'] == 0x100]
        assert len(returned) > 100 and all(r['ow_exit'] == 2 for r in returned)
    print(f'Keyhole arbitration and native scene passed: {len(rows)} actor records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--winner', type=int, default=1)
    parser.add_argument('--small', type=int, choices=(0, 1))
    parser.add_argument('--stars', type=lambda x: int(x, 0), default=0)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--replay', action='store_true')
    parser.add_argument('--candidates', type=int, default=1)
    parser.add_argument('--bonus-start', type=int, default=0)
    args = parser.parse_args()
    check(args.trace, args.winner, args.small, args.stars, args.compare, args.replay,
          args.candidates, args.bonus_start)
