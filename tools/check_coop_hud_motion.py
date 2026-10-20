#!/usr/bin/env python3
"""Verify fixed reserve borders and TIME pixels during camera movement.

Requires running SMW_RENDER_DIAGNOSTICS captures from the two-mounted fixture.
See docs/NATIVE_COOP.md for input and capture frames. Needs Pillow and private
ROM-derived artifacts; does not control the guest or accept a stationary test.
"""
import argparse
import csv
from pathlib import Path

from PIL import Image


def check(root):
    with (root / 'frames.csv').open() as source:
        rows = {int(r['frame']): {k: int(v) for k, v in r.items()}
                for r in csv.DictReader(source)}
    captures = sorted(root.glob('frame-*.bmp'))
    assert len(captures) >= 4, 'need multiple running captures'
    cameras, offsets, borders, times = [], [], [], []
    width = None
    for path in captures:
        row = rows[int(path.stem.split('-')[1])]
        assert row['mode'] == 0x14, f'{path}: not level gameplay'
        with Image.open(path) as source:
            im = source.convert('RGB')
        if width is None:
            width = im.width
        assert im.size == (width, 224) and row['width'] == width
        assert width in (256, 342), 'fixture is qualified at native and 16:9'
        pixels = im.load()
        timer = max(width - 112, 168)
        border = frozenset((x, y) for y in range(40)
                           for x in range(100, width - 65)
                           if pixels[x, y] == (90, 173, 247))
        time = frozenset((x, y) for y in range(14, 23)
                         for x in range(timer + 8, timer + 32)
                         if pixels[x, y] == (255, 222, 115))
        assert len(border) == 468, f'{path}: missing original reserve borders'
        assert time, f'{path}: missing TIME label'
        cameras.append(row['camera'])
        offsets.append(row['native_offset'])
        borders.append(border)
        times.append(time)
    assert max(cameras) - min(cameras) > 60, 'camera did not traverse test range'
    if width > 256:
        assert max(offsets) - min(offsets) > 30, 'viewport clamp not exercised'
    assert len(set(borders)) == 1, 'reserve borders moved on screen'
    assert len(set(times)) == 1, 'TIME label moved on screen'
    assert len(times[0]) == 70, 'TIME label clipped or contaminated'
    assert min(x for x, y in borders[0]) == max(112, width // 2 - 32) + 3
    print(f'{root}: {len(captures)} frames at {width}px; '
          '468 reserve-border and 70 TIME pixels stay fixed as camera moves.')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path, nargs='+')
    for root in ap.parse_args().captures:
        check(root)
