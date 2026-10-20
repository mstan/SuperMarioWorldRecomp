#!/usr/bin/env python3
"""Copy a YI2 entrance state for death, overlap and original-HUD checks."""
import argparse
from coop_fixture import Fixture
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type


def make(source, output, scenario, checkpoint=False):
    f = Fixture(source)
    d, r = f.data, f.ram
    assert not f.entities and f.version == 2 and d[r+0x13bf] == 0x2a
    for i in range(12):
        d[r+0x14c8+i] = 0
    for player in f.actors:
        x = 32 if scenario == 'overlap' else 32+player*72
        if scenario == '1':
            x = 104-player*72  # Spiny travels away from the stationary survivor
        f.actor(player, x, 352, int(scenario == 'overlap' and player == 1), 1 if player == 0 else 4)
        if scenario in ('both', 'gameover') or scenario == str(player):
            slot = loose_key(f, x+8, 368)
            sprite_type(f, slot, 0x13)  # original Spiny contact/kill routine
            d[r+0x14c8+slot] = 8
    if scenario == 'timeout':
        d[r+0xf30:r+0xf34] = bytes((0, 0, 0, 1))
    if scenario == 'gameover':
        d[r+0xdbe] = 0
        f.put(f.core+36, 1)
    if checkpoint:
        assert scenario in ('both', 'timeout'), 'checkpoint option checks team retry'
        d[r+0x13ce] = 1  # stock midway flag, persisted at attempt end
        if scenario == 'timeout':
            d[r+0xf30] = 10  # include pre-failure lives in the checkpoint trace
    f.camera(0)
    f.save(output)
    print(f'Created feedback fixture: {scenario} -> {output}')


if __name__ == '__main__':
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scenario', choices=('0', '1', 'both', 'gameover', 'timeout', 'overlap'), required=True)
    parser.add_argument('--checkpoint', action='store_true')
    args = parser.parse_args()
    make(args.source, args.output, args.scenario, args.checkpoint)
