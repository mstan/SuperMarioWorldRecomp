#!/usr/bin/env python3
"""Stage loose keys and stock keyholes in an offline YI2 entrance copy."""
import argparse
from pathlib import Path
from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key


def sprite_type(f, slot, kind):
    d, r = f.data, f.ram
    rom = (ROOT/'smw.sfc').read_bytes()
    for table, source in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                          (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                          (0x1686, 0x3f590), (0x190f, 0x3f659)):
        d[r+table+slot] = rom[source+kind]
    d[r+0x9e+slot], d[r+0x14c8+slot] = kind, 8
    d[r+0x15f6+slot] = d[r+0x166e+slot] & 15


def make(source, output, both, waiting):
    f = Fixture(source)
    d, r = f.data, f.ram
    assert f.version == 2 and not f.entities and d[r+0x13bf] == 0x2a
    for player, x, slot in ((0, 32, 7), (1, 128, 4)):
        f.actor(player, x, 352, 1 if player == 0 else 3, 1 if player == 0 else 4,
                2 if waiting and player == 0 else 0, 180 if waiting and player == 0 else 0)
        if not waiting or player:
            loose_key(f, x+8, 368, slot)
        if both or player:
            # Already initialized hole coordinate (InitKeyHole adds eight).
            hole = loose_key(f, x+72, 368)
            sprite_type(f, hole, 0x0e)
    f.camera(0)
    f.save(output)
    print(f'Created keyhole fixture: {output} (both={both}, waiting={waiting})')


def contacts(source, output, timeout, tape_secret, bonus_start):
    """Use two keys actually picked up by the ROM; stage conflicting exits."""
    f = Fixture(source)
    d, r = f.data, f.ram
    assert f.version == 3 and f.word(f.core+64) == 0  # continuing attempt
    held = {e[4]: e for e in f.entities.values() if e[6] == 1}
    assert set(held) == {0, 1} and all(e[3] == 0x80 for e in held.values())
    # The original descending update reaches the hole before either key's
    # neutral load-frame input can release it. Native held status is intact.
    key = held[1][2]
    x = d[r+0xe4+key]+256*d[r+0x14e0+key]
    y = d[r+0xd8+key]+256*d[r+0x14d4+key]
    hole = loose_key(f, x+4, y-4, 11)
    sprite_type(f, hole, 0x0e)
    f.actor(0, 32, 224, 1, 1)
    tape = loose_key(f, 40, 244, 10)
    sprite_type(f, tape, 0x7b)
    d[r+0xc2+tape], d[r+0x151c+tape] = 32, 0
    d[r+0x1528+tape], d[r+0x1534+tape] = 368 & 255, 1
    d[r+0x1540+tape], d[r+0x1588+tape] = 100, 1
    d[r+0x187b+tape] = 4 if tape_secret else 0
    if timeout:
        d[r+0xf30:r+0xf34] = bytes((0, 0, 0, 1))
    d[r+0xf48] = bonus_start
    f.save(output)
    print(f'Created keyhole/tape conflict: {output} (timeout={timeout}, tape_secret={tape_secret})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--both', action='store_true')
    parser.add_argument('--waiting', action='store_true')
    parser.add_argument('--contacts', action='store_true')
    parser.add_argument('--timeout', action='store_true')
    parser.add_argument('--tape-secret', action='store_true')
    parser.add_argument('--bonus-start', type=int, choices=range(100), default=0)
    args = parser.parse_args()
    if args.both and args.waiting:
        parser.error('--both and --waiting are separate cases')
    if args.contacts:
        if args.both or args.waiting:
            parser.error('--contacts requires an actual held-object state')
        contacts(args.source, args.output, args.timeout, args.tape_secret, args.bonus_start)
    else:
        if args.timeout or args.tape_secret:
            parser.error('--timeout/--tape-secret require --contacts')
        make(args.source, args.output, args.both, args.waiting)
