#!/usr/bin/env python3
"""Stage a COPY of a YI2 entrance state just before its midway tape."""
import argparse
from pathlib import Path
from coop_fixture import Fixture


def make(source, output, collector, waiting, strong):
    fixture = Fixture(source)
    assert set(fixture.actors) == {0, 1} and fixture.data[fixture.ram+0x13bf] == 0x2a
    assert not waiting or not strong
    for player in range(2):
        collects = collector == 'both' or player == int(collector)
        fixture.actor(player, 2544 if collects else 2480, 352,
                      3 if strong and not collects else 0,
                      1 if player == 0 else 4,
                      2 if waiting and not collects else 0,
                      180 if waiting and not collects else 0)
    fixture.camera(2420)
    fixture.save(output)
    print(f'Created staged midway fixture: {output} (collector={collector}, waiting={waiting}, strong={strong})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--collector', choices=('0', '1', 'both'), default='1')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--waiting', action='store_true')
    group.add_argument('--strong', action='store_true')
    args = parser.parse_args()
    make(args.source, args.output, args.collector, args.waiting, args.strong)
