#!/usr/bin/env python3
"""Check native contact outcomes from SMW_COOP_CONTACT_TRACE, not pixels.

Use with make_coop_contact_fixture.py. --expect-miss is the pre-fix negative
control; the center-camera encounter must also pass as a positive control.
"""
import argparse
import csv
import json
from pathlib import Path


def check(path, kind, actor, region, expect_miss=False):
    rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(path.open())]
    rows = [r for r in rows if r['slot'] == 0 and r['player'] == actor]
    assert rows, 'No contact trace for fixture slot/actor'
    assert len(rows) % 2 == 0, 'Incomplete before/after pair'
    first = rows[0]['frame']
    if expect_miss:
        assert rows[-1]['frame'] >= first+30, 'Incomplete negative-control replay'
    pairs = list(zip(rows[::2], rows[1::2]))
    hits = []
    for before, after in pairs:
        assert before['phase'] == 0 and after['phase'] == 1
        assert before['frame'] == after['frame']
        if before['frame'] > first+30:
            break  # Later encounters with level-loaded enemies are unrelated.
        hit = ((kind == 'stomp' and before['status'] == 8 and after['status'] == 9
                and before['player_vy'] > 0 and after['player_vy'] < 0)
               or (kind == 'kick' and before['status'] == 9 and after['status'] == 10)
               or (kind == 'pickup' and before['status'] == 9 and after['status'] == 11)
               or (kind == 'side' and before['animation'] == 0 and after['animation'] == 9))
        if hit:
            x = before['sprite_x']-before['camera_x']
            assert {'center': 0 <= x < 256, 'right': x >= 256, 'left': x < 0}[region], (region, x)
            hits.append((before, after))
    assert bool(hits) != expect_miss, f'{kind}: expected {"miss" if expect_miss else "contact"}'
    if not hits:
        return {'kind': kind, 'actor': actor, 'region': region, 'result': 'expected miss'}
    before, after = hits[0]
    return {'kind': kind, 'actor': actor, 'region': region, 'frame': before['frame'],
            'native_offscreen': before['offscreen_x'],
            'status': [before['status'], after['status']],
            'vy': [before['player_vy'], after['player_vy']],
            'animation': [before['animation'], after['animation']]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--kind', choices=('stomp', 'kick', 'pickup', 'side'), required=True)
    parser.add_argument('--actor', type=int, choices=(0, 1), default=0)
    parser.add_argument('--region', choices=('center', 'right', 'left'), required=True)
    parser.add_argument('--expect-miss', action='store_true')
    args = parser.parse_args()
    print(json.dumps(check(args.trace, args.kind, args.actor, args.region, args.expect_miss)))
