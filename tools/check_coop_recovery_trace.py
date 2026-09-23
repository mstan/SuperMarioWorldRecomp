#!/usr/bin/env python3
"""Validate an uninterrupted live death/recovery trace and optional parity run."""
import argparse
import csv
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('trace',type=Path)
    parser.add_argument('--dead-player',type=int,required=True)
    parser.add_argument('--compare',type=Path)
    args=parser.parse_args()
    with args.trace.open(newline='') as stream:
        rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(stream)]
    ids={r['player'] for r in rows}
    assert len(ids)>=2 and args.dead_player in ids
    actor=[r for r in rows if r['player']==args.dead_player]
    dying=next(i for i,r in enumerate(actor) if r['life']==1)
    bubble=next(i for i in range(dying,len(actor)) if actor[i]['life']==2)
    recovered=next(i for i in range(bubble,len(actor)) if actor[i]['life']==0)
    assert 189<=actor[bubble]['frame']-actor[dying]['frame']<=192
    assert actor[bubble]['recovery']==180
    assert actor[recovered]['frame']-actor[bubble]['frame']==180
    assert actor[recovered]['power']==0 and actor[recovered]['protection']==120
    for identity in ids:
        sequence=[r for r in rows if r['player']==identity]
        assert all(r['lives']==sequence[0]['lives'] and r['stack']==511 and r['lock']==0 for r in sequence)
        for previous,current in zip(sequence,sequence[1:]):
            assert current['frame']==previous['frame']+1
            assert current['world_frame']==(previous['world_frame']+1)%256
        if identity!=args.dead_player:
            assert all(r['life']==0 for r in sequence)
    if args.compare:
        assert args.trace.read_bytes()==args.compare.read_bytes(), 'execution-path divergence'
    print(f'Live co-op recovery: player {args.dead_player}, {len(ids)} actors; '
          'world cadence, death, 180-frame delay, protection, lives and stack passed')


if __name__=='__main__':main()
