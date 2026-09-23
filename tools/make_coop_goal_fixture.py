#!/usr/bin/env python3
"""Stage a COPY of a YI2 entrance state before its original goal tape."""
import argparse
from pathlib import Path
from coop_fixture import Fixture


def contacts(source, output, secret, timeout, waiting):
    """Synthetic competing contacts using a naturally loaded stock goal sprite.

    Duplicate that sprite's owned tables into an empty slot. This exercises
    conflicting exits; the duplicate is an explicit fixture, not level content.
    """
    f = Fixture(source)
    d, r = f.data, f.ram
    source_slot = next(i for i in range(12)
                       if d[r+0x14c8+i] == 8 and d[r+0x9e+i] == 0x7b)
    second = next(i for i in range(12) if not d[r+0x14c8+i])
    # Stock normal-sprite arrays from rammap.asm; no shared world fields.
    tables = (0x9e, 0xaa, 0xb6, 0xc2, 0xd8, 0xe4, 0x14c8, 0x14d4,
              0x14e0, 0x14ec, 0x14f8, 0x1504, 0x1510, 0x151c, 0x1528,
              0x1534, 0x1540, 0x154c, 0x1558, 0x1564, 0x1570, 0x157c,
              0x1588, 0x1594, 0x15a0, 0x15ac, 0x15b8, 0x15c4, 0x15d0,
              0x15dc, 0x15ea, 0x15f6, 0x1602, 0x160e, 0x161a, 0x1626,
              0x1632, 0x163e, 0x164a, 0x1656, 0x1662, 0x166e, 0x167a,
              0x1686, 0x186c, 0x187b, 0x190f)
    for at in tables:
        d[r+at+second] = d[r+at+source_slot]
    for player, slot, x, y in ((0, source_slot, 4832, 348), (1, second, 4752, 244)):
        for low, high, value in ((0xe4, 0x14e0, x), (0xd8, 0x14d4, y),
                                 (0xc2, 0x151c, x-8)):
            d[r+low+slot], d[r+high+slot] = value & 255, value >> 8
        d[r+0x187b+slot] = 4 if secret and player else 0
        # Next native motion raises the tape 1px. Heights 21 and125 still
        # select the stock BCD rewards 06 and50 from 07:F1AA.
        d[r+0x1540+slot] = 100
        d[r+0x1588+slot] = 1
        d[r+0x14ec+slot] = 0
        f.actor(player, x-8, 320 if player == 0 else 224,
                1 if player == 0 else 3, 1 if player == 0 else 4,
                2 if waiting and player == 0 else 0, 180 if waiting and player == 0 else 0)
    if timeout:
        # UpdateStatusBar decrements the frame divider before its BPL test.
        d[r+0xf30:r+0xf34] = bytes((0, 0, 0, 1))
    f.save(output)
    print(f'Created competing-goal fixture: {output} (secret={secret}, timeout={timeout}, waiting={waiting})')


def make(source, output, collector, waiting):
    fixture = Fixture(source)
    assert set(fixture.actors) == {0, 1} and fixture.data[fixture.ram+0x13bf] == 0x2a
    for player in range(2):
        collects = collector == 'both' or player == int(collector)
        fixture.actor(player, 4800 if collects else 4740, 352,
                      1 if player == 0 else 3, 1 if player == 0 else 4,
                      2 if waiting and not collects else 0,
                      180 if waiting and not collects else 0)
    fixture.camera(4660)
    fixture.save(output)
    print(f'Created staged goal fixture: {output} (collector={collector}, waiting={waiting})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--collector', choices=('0', '1', 'both'), default='1')
    parser.add_argument('--waiting', action='store_true')
    parser.add_argument('--contacts', choices=('normal', 'secret'))
    parser.add_argument('--timeout', action='store_true')
    args = parser.parse_args()
    if args.contacts:
        contacts(args.source, args.output, args.contacts == 'secret', args.timeout, args.waiting)
    else:
        if args.timeout:
            parser.error('--timeout requires --contacts')
        make(args.source, args.output, args.collector, args.waiting)
