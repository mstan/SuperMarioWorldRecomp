#!/usr/bin/env python3
"""Stage an offline YI2 pipe fixture in a COPY of a native co-op entrance state."""
import argparse
from pathlib import Path
from coop_fixture import Fixture


def make(source, output, entrant):
    fixture = Fixture(source)
    assert set(fixture.actors) == {0, 1} and fixture.data[fixture.ram+0x13bf] == 0x2a
    for player in range(2):
        x = 3880 if entrant == 'both' or player == int(entrant) else 3760
        power, reserve = (1, 1) if player == 0 else (3, 4)
        fixture.actor(player, x, 320 if x == 3880 else 352, power, reserve)
    fixture.camera(3744)
    fixture.save(output)
    print(f'Created staged YI2 pipe fixture: {output} (entrant={entrant})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--entrant', choices=('0', '1', 'both'), default='1')
    args = parser.parse_args()
    make(args.source, args.output, args.entrant)
