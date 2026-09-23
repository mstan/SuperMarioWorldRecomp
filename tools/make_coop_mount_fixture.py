#!/usr/bin/env python3
"""Stage native Yoshi initialization and block contacts in a COPY of YI2."""
import argparse
from pathlib import Path
from coop_fixture import Fixture
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type


def make(source, output, scenario):
    f = Fixture(source)
    d, r = f.data, f.ram
    if scenario in ('catchup', 'fall'):
        assert f.version == 5 and len(f.mounts) == 2
        guest, record = f.actors[1]
        if scenario == 'catchup':
            f.put(record+8, 3)
            f.put(record+44, 30)
            d[guest+f.offsets[0x78]] = 255
        else:
            for address in (0x96, 0xd3):
                d[guest+f.offsets[address]] = 480 & 255
                d[guest+f.offsets[address+1]] = 480 >> 8
            f.put(record+24, 512)
        f.save(output)
        print(f'Created mounted recovery fixture: {scenario} -> {output}')
        return
    assert f.version == 5 and f.level == 0x106 and not f.mounts
    assert all(e[4] == 0xffffffff for e in f.entities.values())
    for i in range(12):
        d[r+0x14c8+i] = 0
    d[r+0xef8] = 1  # this fixture has already seen the original Yoshi message
    d[r+0x18df] = d[r+0x18e2] = d[r+0xdc1] = 0
    positions = ((32, 280), (160, 280)) if scenario == 'riders' else ((824, 280), (864, 352))
    for player, (x, y) in enumerate(positions):
        f.actor(player, x, y, 1, 1 if player == 0 else 4)
        guest, _ = f.actors[player]
        d[guest+f.offsets[0x187a]] = d[guest+f.offsets[0x188b]] = 0
        if (player == 0 and scenario != 'source-empty') or scenario in ('riders', 'cap'):
            slot = loose_key(f, x if player == 0 or scenario == 'riders' else 1008, 352)
            sprite_type(f, slot, 0x35)
            d[r+0x14c8+slot] = 1  # original InitYoshi supplies native state
    f.camera(0 if scenario == 'riders' else 720)
    f.put(f.focus+24, 0)
    f.save(output)
    print(f'Created native mount fixture: {scenario} -> {output}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--scenario', choices=('riders', 'source', 'source-empty', 'cap', 'catchup', 'fall'), required=True)
    args = ap.parse_args()
    make(args.source, args.output, args.scenario)
