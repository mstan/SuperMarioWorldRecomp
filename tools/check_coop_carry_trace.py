#!/usr/bin/env python3
"""Validate ownership, native release, room transport, and snapshot replay."""
import argparse
import csv
from pathlib import Path
from coop_fixture import Fixture

NONE = 0xffffffff


def read(path):
    return [{k: int(v) for k, v in row.items()}
            for row in csv.DictReader(path.open(newline=''))]


def check(path, case, snapshot, compare):
    rows = read(path)
    assert len(rows) > 100 and len(rows) % 2 == 0
    assert all(r['stack'] == 511 for r in rows)
    for a, b in zip(rows[::2], rows[1::2]):
        assert (a['player'], b['player']) == (0, 1)
        if a['held_object'] != NONE:
            assert a['held_object'] != b['held_object'], 'two carriers for one object'
        for r in (a, b):
            assert (r['held_object'] == NONE) == (r['held_slot'] == 12)
            if r['held_slot'] < 12:
                assert r['held_status'] == 11
    if compare:
        assert rows == read(compare), 'compiled/interpreted ownership differs'

    if case == 'near':
        held = [r for r in rows if r['player'] == 1 and r['held_object'] != NONE]
        identities = {r['held_object'] for r in held}
        assert len(held) > 60 and len(identities) == 1
        nearer = 0
        for a, b in zip(rows[::2], rows[1::2]):
            if b['held_object'] == NONE:
                continue
            kx, ky = b['held_x']+8, b['held_y']+8
            distance = lambda p: (p['x']-kx)**2+(p['y']-ky)**2
            if distance(a) < distance(b) and a['input'] & 2:
                nearer += 1
                assert a['held_object'] == NONE
        assert nearer >= 5, 'missing nearer player trying to grab the held object'
        later = [r for r in rows if r['player'] == 0 and r['held_object'] in identities]
        assert later and later[0]['frame'] > held[-1]['frame']
        print(f'Ownership: {nearer} contested frames; released object picked up by Mario')
    elif case == 'pipe':
        commits = [r for r in rows if r['mode'] == 15]
        assert len(commits) == 2 and all(r['room_request'] == 1 for r in commits)
        for player, old_slot, new_slot in ((0, 7, 1), (1, 4, 0)):
            before = [r for r in rows if r['player'] == player and r['held_slot'] == old_slot]
            after = [r for r in rows if r['player'] == player and r['level'] == 0x1ca
                     and r['held_object'] != NONE]
            assert before and len(after) > 100
            assert {r['held_object'] for r in before+after} == {before[0]['held_object']}
            assert all(r['held_slot'] == new_slot for r in after)
            assert all(r['power'] == (1 if player == 0 else 3) and
                       r['reserve'] == (1 if player == 0 else 4) for r in before+after)
        assert all(r['lives'] == 5 and r['life'] == 0 for r in rows)
        print('Pipe: both object identities and owners survive native slot relocation')
    elif case == 'catchup':
        drops = [r for r in rows if r['drop_entity'] != NONE]
        assert len(drops) == 1 and drops[0]['player'] == 1
        drop = drops[0]
        before = [r for r in rows if r['player'] == 1 and r['frame'] < drop['frame']]
        assert before[-1]['separation'] == 59
        assert drop['drop_entity'] == before[-1]['held_object']
        assert abs(drop['drop_x']-before[-1]['x']) < 24
        returned = next(r for r in rows if r['player'] == 1 and
                        r['frame'] >= drop['frame'] and r['life'] == 0)
        assert returned['x'] > 4700 and returned['held_object'] == NONE
        assert returned['power'] == 1 and returned['reserve'] == 2 and returned['protection'] == 0
        print(f'Catch-up: drop at x={drop["drop_x"]}; recovery at x={returned["x"]}')
    elif case == 'timeout':
        assert any(r['player'] == 1 and r['held_object'] != NONE for r in rows)
        retries = [r for r in rows if r['outcome'] == 1]
        assert len(retries) == 2 and all(r['lives'] == 4 for r in retries)
        assert all(r['held_object'] == NONE for r in retries)
        resumed = [r for r in rows if r['frame'] > retries[0]['frame']]
        assert len(resumed) > 60 and rows[-1]['level'] == 0x106
        assert all(r['lives'] == 4 and r['power'] == 0 and r['held_object'] == NONE
                   and r['reserve'] == r['player']+1 for r in resumed)
        print('Timeout: one shared life; surviving carrier restarts small with reserve and no object')
    elif case == 'replay':
        split = next(i for i in range(1, len(rows)) if rows[i]['frame'] < rows[i-1]['frame'])
        assert split > 100 and len(rows) >= split*2
        assert rows[:split] == rows[split:split*2], 'loaded timeline diverged'
        print(f'Snapshot: {split} identical replayed actor records')

    if snapshot:
        f = Fixture(snapshot)
        assert f.version == 3
        owners = {e[4]: e for e in f.entities.values() if e[6] == 1}
        assert set(owners) == ({1} if case == 'near' else {0, 1})
        for player, entity in owners.items():
            guest, core = f.actors[player]
            assert f.word(core+56) == entity[0]
            assert f.data[f.ram+0x14c8+entity[2]] == 11
            assert f.data[guest+f.offsets[0x1470]] and f.data[guest+f.offsets[0x148f]]
        print('Snapshot: complete native held-object ownership and guest flags preserved')
    print(f'Carry trace passed: {len(rows)} actor records')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--case', choices=('near', 'pipe', 'catchup', 'timeout', 'replay'), required=True)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    check(args.trace, args.case, args.snapshot, args.compare)
