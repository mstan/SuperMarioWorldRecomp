#!/usr/bin/env python3
"""Validate running goal arbitration, shared victory cadence, and replay."""
import argparse
import csv
from pathlib import Path


def check(path, winner, stars, secret, small, replayed, compare, time_match):
    rows = [{k: int(v) for k, v in row.items()} for row in
            csv.DictReader(path.open(encoding='utf-8'))]
    assert rows and {r['player'] for r in rows} == {0, 1}
    assert all(r['stack'] == 511 and r['life'] == 0 for r in rows)
    assert all(r['reserve'] == (1 if r['player'] == 0 else 4) for r in rows)
    if compare:
        other = [{k: int(v) for k, v in row.items()} for row in
                 csv.DictReader(compare.open(encoding='utf-8'))]
        assert rows == other, 'execution modes diverged'
    if replayed:
        starts = [i for i, r in enumerate(rows) if r == rows[0]]
        assert len(starts) == 2 and starts[1] > 100
        assert rows[:starts[1]] == rows[starts[1]:], 'victory restore diverged'
        print(f'Exact replay: {starts[1]} actor records')
        rows = rows[:starts[1]]
    lead = [r for r in rows if r['player'] == 0]
    exits = [r for r in lead if r['exit_player'] != 0xffffffff]
    if replayed:
        assert not exits
    else:
        assert len(exits) == 1, f'exit commits: {len(exits)}'
        event = exits[0]
        assert event['exit_player'] == winner
        assert event['exit_flags'] == ((stars << 8) | secret)
        assert event['goal_stars'] == stars
        assert event['end_timer'] == 255 and event['lives'] == 5
    clear = [r for r in lead if r['outcome'] == 3]
    assert clear and clear[-1]['mode'] == 12, 'native victory did not finish'
    assert clear[-1]['spotlight'] == 0 and any(r['peace'] for r in clear)
    # Native AF17 decrements once on alternate true frames, stopping at1.
    # A duplicate shared scene call produces a decrement of2 and fails here.
    for a, b in zip(clear, clear[1:]):
        assert a['end_timer']-b['end_timer'] in (0, 1)
    for a, b in zip(clear, clear[2:]):
        if b['end_timer'] > 1:
            assert a['end_timer']-b['end_timer'] == 1
    assert min(r['lives'] for r in rows) == 5, 'clear charged a team life'
    # Native 05:CC77 also grants a life when the existing bonus-star tens
    # digit matches both final timer digits (including 0 stars / TIME000).
    assert clear[-1]['lives'] == (8 if stars == 0x50 else 5)+time_match
    for r in rows:
        if r['outcome'] != 3:
            continue
        assert r['power'] == (0 if r['player'] == small else (1 if r['player'] == 0 else 3))
    print(f'Goal arbitration and single victory cadence passed: {len(rows)} actor records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--winner', type=int, default=1)
    parser.add_argument('--stars', type=lambda s: int(s, 0), default=0)
    parser.add_argument('--secret', action='store_true')
    parser.add_argument('--small', type=int, choices=(0, 1))
    parser.add_argument('--replayed', action='store_true')
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--time-match', action='store_true')
    args = parser.parse_args()
    check(args.trace, args.winner, args.stars, args.secret, args.small, args.replayed, args.compare, args.time_match)
