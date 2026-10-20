#!/usr/bin/env python3
"""Stage a stock loose key in a copied YI2 entrance state; pickup stays native."""
import argparse
from pathlib import Path
from coop_fixture import Fixture, ROOT


def loose_key(f, x, y, slot=None):
    d, r = f.data, f.ram
    # Zero a free normal-sprite slot, then load the stock key tweakers.
    if slot is None:
        slot = next(i for i in range(12) if not d[r+0x14c8+i])
    assert not d[r+0x14c8+slot]
    tables = (0x9e, 0xaa, 0xb6, 0xc2, 0xd8, 0xe4, 0x14c8, 0x14d4,
              0x14e0, 0x14ec, 0x14f8, 0x1504, 0x1510, 0x151c, 0x1528,
              0x1534, 0x1540, 0x154c, 0x1558, 0x1564, 0x1570, 0x157c,
              0x1588, 0x1594, 0x15a0, 0x15ac, 0x15b8, 0x15c4, 0x15d0,
              0x15dc, 0x15ea, 0x15f6, 0x1602, 0x160e, 0x161a, 0x1626,
              0x1632, 0x163e, 0x164a, 0x1656, 0x1662, 0x166e, 0x167a,
              0x1686, 0x186c, 0x187b, 0x190f)
    for at in tables:
        d[r+at+slot] = 0
    rom = (ROOT/'smw.sfc').read_bytes()
    for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                (0x1686, 0x3f590), (0x190f, 0x3f659)):
        d[r+table+slot] = rom[source_table+0x80]
    d[r+0x15f6+slot] = d[r+0x166e+slot] & 0x0f
    d[r+0x9e+slot], d[r+0x14c8+slot] = 0x80, 9
    d[r+0x161a+slot] = 0xff  # fixture has no level sprite-load index
    for lo, hi, value in ((0xe4, 0x14e0, x), (0xd8, 0x14d4, y)):
        d[r+lo+slot], d[r+hi+slot] = value & 255, value >> 8
    return slot


def make(source, output, collector, pipe, timeout, catchup):
    f = Fixture(source)
    d, r = f.data, f.ram
    assert not f.entities and f.version == 2
    assert d[r+0x13bf] == 0x2a
    for player in f.actors:
        if pipe:
            x, y = (3880, 280) if player == collector else (3760, 352)
            f.actor(player, x, y, 1 if player == 0 else 3, 1 if player == 0 else 4)
            loose_key(f, x+8, 336 if player == collector else 368, 7 if player == 0 else 4)
        else:
            f.actor(player, 88 if player == collector else 32, 352, 1, player+1)
            if catchup and player != collector:
                f.actor(player, 4740, 352, 1, player+1)
    if not pipe:
        loose_key(f, 96, 368)
    if timeout:
        d[r+0xf30:r+0xf34] = bytes((60, 0, 0, 1))
    f.camera(3744 if pipe else 0)
    f.save(output)
    print(f'Created loose-key fixture: {output} (collector={collector}, pipe={pipe}, timeout={timeout}, catchup={catchup})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--collector', type=int, choices=(0, 1), default=1)
    parser.add_argument('--pipe', action='store_true')
    parser.add_argument('--timeout', action='store_true')
    parser.add_argument('--catchup', action='store_true')
    args = parser.parse_args()
    if args.catchup and (args.pipe or args.timeout):
        parser.error('--catchup is a separate case')
    make(args.source, args.output, args.collector, args.pipe, args.timeout, args.catchup)
