#!/usr/bin/env python3
"""Copy a native state into the original Yellow Palace room or near its switch."""
import argparse
from pathlib import Path
from coop_fixture import Fixture, ROOT


def make(source, output, entrance, collector, waiting):
    f = Fixture(source)
    d, r = f.data, f.ram
    if entrance:
        # Stock05:D796 derives the high level byte from OWPlayerSubmap.
        # Screen0 exitCA on the main map is room0CA (on a submap it is1CA).
        for at, value in {0x100: 0x0f, 0x141a: 1, 0x1b93: 0, 0x19b8: 0xca,
                          0x13bf: 0x14, 0x13ce: 0, 0x1493: 0, 0x1f11: 0}.items():
            d[r+at] = value
        assert d[r+0x95] == 0 and not d[r+0x5b] & 1
        f.put(f.core+56, 0)
    else:
        assert d[r+0x13bf] == 0x14 and d[r+0x1f11] == 0
        rom = (ROOT/'smw.sfc').read_bytes()
        x, y = 624, 352
        index = (y & 0xf0) | ((x & 255) >> 4)
        index += y & 0xff00
        at = rom[0x3a60+(x >> 8)]+256*rom[0x3a9c+(x >> 8)]+index
        tile = d[r+at]+256*d[r+at+0x10000]
        assert 0xec <= tile < 0xfc, f'not the original palace switch: {tile:03x}'
        assert not any(d[r+0x1f27:r+0x1f2b]), 'fixture requires unpressed switches'
        for player in range(2):
            collects = collector == 'both' or player == int(collector)
            f.actor(player, 628 if collects else 544, 288 if collects else 352,
                    1 if player == 0 else 3, 1 if player == 0 else 4,
                    2 if waiting and not collects else 0,
                    180 if waiting and not collects else 0)
        f.camera(490)
    f.save(output)
    print(f'Created palace fixture: {output} (entrance={entrance}, collector={collector}, waiting={waiting})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--entrance', action='store_true')
    parser.add_argument('--collector', choices=('0', '1', 'both'), default='1')
    parser.add_argument('--waiting', action='store_true')
    args = parser.parse_args()
    make(args.source, args.output, args.entrance, args.collector, args.waiting)
