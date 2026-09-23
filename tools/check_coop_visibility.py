#!/usr/bin/env python3
"""Check the owner cave/YI2 visibility replays against captured SNES OBJ pixels.

Usage: python tools/check_coop_visibility.py build-adaptive/playtest/visibility-feedback
Requires private raster captures described in docs/NATIVE_COOP.md. Never
controls the guest. Original failing images must fail the same pixel oracle.
"""
import argparse
import struct
from pathlib import Path

from renderer_visibility import Capture, OWNER_OFFSET


def primary(root, frame, require_owner=True):
    c = Capture(root, frame)
    assert c.ram[0x100] == 20 and c.ram[0x13f9] == 0
    assert not c.ram[0x1497] and not c.ram[0x71], 'unexpected flashing/animation'
    # $7E is the original draw's signed screen coordinate, preceding movement.
    # Each body tile's low byte supplies its small signed offset from that origin.
    origin = struct.unpack_from('<h', c.ram, 0x7e)[0]
    pixels, positions = 0, []
    for slot in range(66, 73):
        pos, attr = struct.unpack_from('<HH', c.ram, 0x200 + slot * 4)
        if pos >> 8 == 240:
            continue
        x = origin + (((pos - origin + 128) & 255) - 128)
        owner = struct.unpack_from('<iHH?', c.raw, OWNER_OFFSET + slot * 12)
        if require_owner:
            assert owner == (x, pos, attr, True), (frame, slot, owner, x)
        else:
            # Give the old image the independently recovered X so failure is
            # missing rendered pixels, not merely a missing ownership record.
            raw = bytearray(c.raw)
            struct.pack_into('<iHH?', raw, OWNER_OFFSET + slot * 12, x, pos, attr, True)
            c.raw = bytes(raw)
        pixels += c.piece(slot)
        positions.append(x)
    assert pixels >= 32, 'empty player capture'
    return pixels, positions


def check(root):
    total = 0
    for case, baseline, bad_frame in (
            ('cave', 'cave-long-before', 180), ('sprint', 'sprint-before', 210)):
        a, b = root / f'{case}-after-0', root / f'{case}-after-1'
        trace = (a / 'coop.csv').read_bytes()
        assert len(trace.splitlines()) > 500, 'incomplete gameplay replay'
        assert trace == (b / 'coop.csv').read_bytes(), 'compiled/interpreted divergence'
        assert trace == (root / baseline / 'coop.csv').read_bytes(), 'gameplay changed'
        paths = sorted(a.glob('frame-*.bmp'))
        assert len(paths) >= 10, 'need multiple boundary captures'
        xs = []
        for path in paths:
            frame = int(path.stem.split('-')[1])
            assert path.read_bytes() == (b / path.name).read_bytes(), 'render paths diverged'
            pixels, positions = primary(a, frame)
            total += pixels
            xs.extend(positions)
        assert min(xs) < -16 if case == 'cave' else max(xs) >= 256
        try:
            primary(root / baseline, bad_frame, require_owner=False)
        except AssertionError as error:
            assert 'sprite pixels visible' in str(error), str(error)
        else:
            raise AssertionError(f'{case}: original disappearing player was not detected')
    far_x = []
    for frame in (63, 65, 70, 100):
        pixels, positions = primary(root / 'far-after', frame)
        total += pixels
        far_x.extend(positions)
    assert max(far_x) >= 384, 'did not exercise original helper cull'
    native = sorted((root / 'native-after').glob('frame-*.bmp'))
    assert len(native) == 13
    for path in native:
        assert path.read_bytes() == (root.parent / 'av-feedback/hud-native' / path.name).read_bytes()
    print(f'Visibility passed: {total} original OBJ pixels; both boundaries, '
          'compiled/interpreted parity, unchanged gameplay/native frames, failing baselines.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path)
    check(parser.parse_args().captures)
