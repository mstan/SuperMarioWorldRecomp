#!/usr/bin/env python3
"""Stage a stock Koopa/shell encounter in a copied CNR5 YI2 entrance state.

Camera 192 puts the encounter in the native window; 0/384 put it in the
right/left 21:9 extension. The encounters have identical world coordinates.
These are explicit offline test inputs, not campaign-play evidence.
Camera relocation does not rebuild streamed terrain/VRAM; visual gaps in
these fixtures must not be used to diagnose ordinary save loading or terrain.
"""
import argparse
from pathlib import Path
from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key


def make(source, output, kind, actor, camera):
    f = Fixture(source)
    d, r = f.data, f.ram
    assert f.version == 5 and f.level == 0x106 and not f.mounts
    for slot in range(12):
        d[r+0x14c8+slot] = 0
    for player in f.actors:
        f.actor(player, 16, 352, 0, 0)
    f.actor(actor, 348 if kind == 'stomp' else 300,
            270 if kind == 'stomp' else 304, 0, 0)
    if kind == 'stomp':
        guest = f.actors[actor][0]
        for address, value in ((0x72, 0x24), (0x77, 0), (0x7d, 0x20)):
            d[guest+f.offsets[address]] = value
            if actor == f.word(f.core+20):
                d[r+address] = value
    slot = loose_key(f, 352 if kind == 'stomp' else 320, 320, slot=0)
    sprite = 5 if kind in ('stomp', 'side') else 4
    rom = (ROOT/'smw.sfc').read_bytes()
    for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                (0x1686, 0x3f590), (0x190f, 0x3f659)):
        d[r+table+slot] = rom[source_table+sprite]
    d[r+0x15f6+slot] = d[r+0x166e+slot] & 15
    d[r+0x9e+slot] = sprite
    d[r+0x14c8+slot] = 8 if kind in ('stomp', 'side') else 9
    d[r+0x157c+slot], d[r+0x1588+slot] = 1, 4
    f.camera(camera)
    # Hold the chosen camera offset relative to the party's center. Merely
    # changing WRAM is insufficient: the saved co-op focus would undo it.
    records = [record for guest, record in f.actors.values()]
    for offset, value in ((4, camera+128),
                          (12, sum(f.word(a+20) for a in records)//len(records)),
                          (16, sum(f.word(a+24) for a in records)//len(records)),
                          (20, len(records)), (24, 1), (28, 1)):
        f.put(f.focus+offset, value)
    f.save(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--kind', choices=('stomp', 'kick', 'pickup', 'side'), required=True)
    parser.add_argument('--actor', type=int, choices=(0, 1), default=0)
    parser.add_argument('--camera', type=int, choices=(0, 192, 384), required=True)
    args = parser.parse_args()
    make(args.source, args.output, args.kind, args.actor, args.camera)
