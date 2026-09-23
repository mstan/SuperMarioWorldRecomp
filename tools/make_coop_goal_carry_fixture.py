#!/usr/bin/env python3
"""Stage two loose shells before a naturally loaded tape in a copied YI2 state."""
import argparse
from pathlib import Path
from coop_fixture import Fixture
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type


def make(source, output, swap):
    f = Fixture(source)
    d, r = f.data, f.ram
    assert f.version == 2 and not f.entities
    goal = next(i for i in range(12) if d[r+0x14c8+i] == 8 and d[r+0x9e+i] == 0x7b)
    # Isolate reward conversion from nearby enemy contacts in this fixture.
    for slot in range(12):
        if slot != goal:
            d[r+0x14c8+slot] = 0
    for player, x, slot in ((0, 4720, 4), (1, 4780, 3)):
        flower_reserve = (player == 0) != swap
        f.actor(player, x, 352, 1, 2 if flower_reserve else 1)
        loose_key(f, x+8, 368, slot)
        sprite_type(f, slot, 4)
        d[r+0x14c8+slot] = 9
        # Let the scripted hold arrive before native neutral-contact kicking.
        d[r+0x154c+slot] = 4
    f.camera(4660)
    f.save(output)
    print(f'Created goal carry fixture: {output} (swap={swap})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--swap', action='store_true')
    args = parser.parse_args()
    make(args.source, args.output, args.swap)
