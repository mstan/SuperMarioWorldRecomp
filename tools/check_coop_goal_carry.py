#!/usr/bin/env python3
"""Verify per-carrier native goal gifts and retirement of carried identities."""
import argparse
import csv
from pathlib import Path
from coop_fixture import Fixture


def read(path):
    return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(path.open())]


def check(trace, state, swap, compare, replay):
    rows = read(trace)
    assert len(rows) > 1000 and all(r['stack'] == 511 and r['life'] == 0 for r in rows)
    if compare:
        assert rows == read(compare), 'compiled/interpreted gift divergence'
    if replay:
        starts = [i for i, row in enumerate(rows) if row == rows[0]]
        assert len(starts) == 2 and starts[1] > 100
        assert rows[:starts[1]] == rows[starts[1]:], 'converted-reward restore diverged'
        assert all(r['held_object'] == 0xffffffff for r in rows)
        print(f'Exact converted-reward replay: {starts[1]} actor records')
        return
    exits = [r for r in rows if r['player'] == 0 and r['exit_player'] != 0xffffffff]
    assert len(exits) == 1 and exits[0]['exit_player'] == 1
    assert rows[-1]['mode'] == 12
    f = Fixture(state)
    d, ram = f.data, f.ram
    assert f.version == 3 and f.word(f.core+64) == 3
    for player, slot in ((0, 4), (1, 3)):
        before = [r for r in rows if r['player'] == player and r['outcome'] == 0 and
                  r['held_slot'] == slot]
        assert len(before) > 20 and before[-1]['frame'] == exits[0]['frame']-1
        old = before[-1]['held_object']
        assert old not in f.entities, 'carried identity survived conversion'
        gift = next(e for e in f.entities.values() if e[1:3] == (0, slot))
        mushroom = (player == 0) != swap
        assert gift[3] == (0x74 if mushroom else 0x78)
        assert gift[4] == 0xffffffff and gift[6] == 0, 'gift claimed as carried'
        assert d[ram+0x14c8+slot] == 12 and d[ram+0x9e+slot] == gift[3]
        guest, record = f.actors[player]
        assert f.word(record+56) == 0xffffffff  # held_object
        assert d[guest+f.offsets[0x1470]] == d[guest+f.offsets[0x148f]] == 0
    print(f'Per-carrier gifts and new world identities passed: {len(rows)} actor records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('state', type=Path)
    parser.add_argument('--swap', action='store_true')
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--replay', action='store_true')
    args = parser.parse_args()
    check(args.trace, args.state, args.swap, args.compare, args.replay)
