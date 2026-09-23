#!/usr/bin/env python3
"""Check running co-op feedback traces and captured stock terrain/OAM pixels."""
import argparse
import csv
import struct
from pathlib import Path
from renderer_visibility import Capture, LINE_SIZE


def trace(path, individual=None, gameover=False, compare=None):
    rows = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(path.open())]
    assert rows and all(r['stack'] == 511 for r in rows)
    actors = {i: [r for r in rows if r['player'] == i] for i in {r['player'] for r in rows}}
    primary = actors[0]
    if individual is not None:
        actor = actors[individual]
        dead = next(i for i, r in enumerate(actor) if r['life'] == 1)
        bubble = next(i for i in range(dead, len(actor)) if actor[i]['life'] == 2)
        recovered = next(i for i in range(bubble, len(actor)) if actor[i]['life'] == 0)
        assert 189 <= bubble-dead <= 192 and recovered-bubble >= 180
        assert actor[dead]['sfx'] == 0x23 and actor[dead]['music'] != 9
        assert not any(r['music'] == 9 for r in primary)
        assert all(r['lives'] == 5 and r['lock'] == 0 for r in primary)
        assert actor[recovered]['protection'] == 120
        others = [a[recovered] for i, a in actors.items() if i != individual]
        assert all(abs(actor[recovered]['x']-r['x']) >= 20 for r in others)
        # This fixture leaves the survivor stationary throughout the death.
        view = {(r['camera_x'], r['camera_y']) for r in primary[dead+1:recovered]}
        assert len(view) == 1, view
        print(f'P{individual+1}: short cue, death, visible-wait interval, separate recovery and stationary camera pass')
    else:
        committed = next(i for i, r in enumerate(primary) if r['outcome'] in (1, 2))
        transition = next(i for i in range(committed, len(primary)) if primary[i]['mode'] != 20)
        assert 189 <= transition-committed <= 192
        assert sum(r['music'] == 9 for r in primary) == 1
        assert all(r['lives'] == (0 if gameover else 4) for r in primary[committed:])
        assert len({(r['camera_x'], r['camera_y']) for r in primary[committed:transition]}) == 1
        assert all(any(r['life'] == 1 and r['animation'] == 9 for r in a[committed:transition]) for a in actors.values())
        if not gameover:
            resumed = [r for r in rows if r['outcome'] == 0 and r['lives'] == 4]
            assert resumed and all(r['level'] == 0x106 and r['animation'] != 10 for r in resumed)
            assert all(r['power'] == 0 and r['reserve'] == (1 if r['player'] == 0 else 4) for r in resumed)
        print('Team: complete original death animation, one music request/life charge, correct retry/game-over pass')
    if compare:
        assert path.read_bytes() == compare.read_bytes(), 'compiled/interpreted trace divergence'
        print('Compiled/interpreted traces are identical')


def terrain(root, frame, rom):
    """ROM Map16 definitions versus the actual native VRAM tilemap.

    Restricted to static YI2 air/grass/dirt. Skipped camera columns leave old
    platform or block tiles in precisely this surface, in either renderer.
    """
    c = Capture(root, frame)
    r = c.ram
    assert r[0x1925] == r[0x1931] == 0
    def word(bank, at):
        return struct.unpack_from('<H', rom, bank*0x8000+(at & 0x7fff))[0]
    tested = 0
    for sy in range(48, 224, 8):
        off = 0x20000+sy*LINE_SIZE
        sc = c.raw[off+6]
        hs, vs = struct.unpack_from('<H', c.raw, off+14)[0], struct.unpack_from('<H', c.raw, off+22)[0]
        dx = ((hs-c.camera+512) & 1023)-512
        dy = ((vs-c.word(0x1c)+512) & 1023)-512
        for sx in range(0, 256, 8):
            x, y = c.camera+sx+dx, c.word(0x1c)+sy+1+dy
            if y >= 432:
                continue
            lo = word(0, word(0, 0xbda8)+(x >> 8)*3)
            ix = (y >> 4)*16+((x & 255) >> 4)
            block = r[lo+ix]+(r[0x10000+lo+ix] << 8)
            if block not in (0x25, 0x3f, 0x40, 0x41, 0x100, 0x101, 0x103):
                continue
            ptr = c.word(0xfbe+block*2)+(4 if x & 8 else 0)+(2 if y & 8 else 0)
            expected = word(13, ptr)
            tx, ty = ((sx+hs) & 1023) >> 3, ((sy+1+vs) & 1023) >> 3
            at = (sc & 0xfc)*256+(tx & 31)+(ty & 31)*32
            if sc & 1 and tx & 32:
                at += 1024
            if sc & 2 and ty & 32:
                at += 2048 if sc & 1 else 1024
            actual = struct.unpack_from('<H', c.raw, off+1088+(at & 0x7fff)*2)[0]
            assert actual == expected, f'stale terrain at world {x},{y}: {actual:04x} != {expected:04x}'
            tested += 1
    assert tested > 400
    print(f'Terrain: {tested} visible static tiles match stock Map16 at frame {frame}')


def primary(root, frame):
    c = Capture(root, frame)
    pixels = 0
    for slot in range(66, 73):
        if c.ram[0x201+slot*4] != 0xf0:
            pixels += c.piece(slot, native=True)
    assert pixels >= 32
    print(f'Overlap: all {pixels} opaque Mario pixels retain native colour at frame {frame}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    t = sub.add_parser('trace')
    t.add_argument('path', type=Path)
    t.add_argument('--individual', type=int)
    t.add_argument('--gameover', action='store_true')
    t.add_argument('--compare', type=Path)
    for name in ('terrain', 'primary'):
        p = sub.add_parser(name)
        p.add_argument('root', type=Path)
        p.add_argument('frame', type=int)
    args = parser.parse_args()
    if args.command == 'trace':
        trace(args.path, args.individual, args.gameover, args.compare)
    elif args.command == 'terrain':
        terrain(args.root, args.frame, (Path(__file__).resolve().parents[1]/'smw.sfc').read_bytes())
    else:
        primary(args.root, args.frame)
