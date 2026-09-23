#!/usr/bin/env python3
"""Check running mount-feedback captures; requires the private playtest artifacts.

Usage: python tools/check_coop_mount_trace.py build-adaptive/playtest/mount-feedback
See docs/NATIVE_COOP.md for fixture/script setup. No live guest control is used.
"""
import argparse
import csv
from pathlib import Path
from coop_fixture import Fixture, ROOT


def rows(root, name):
    result = [{k: int(v) for k, v in r.items()}
              for r in csv.DictReader((root/name).open())]
    assert result and {r['player'] for r in result} == {0, 1}, name
    return result


def check(root):
    a, b = rows(root, 'control-0.csv'), rows(root, 'control-1.csv')
    assert a == b, 'compiled/interpreted mount control diverged'
    p1, p2 = ([r for r in a if r['player'] == p] for p in (0, 1))
    assert all(r['mount'] == p1[0]['mount'] and r['riding'] for r in p1)
    assert p1[0]['mount'] != p2[0]['mount'] and p2[0]['riding']
    assert any(not r['riding'] and r['mount'] == 0xffffffff for r in p2)
    assert any(x['tongue_timer'] and not y['tongue_timer'] for x, y in zip(p1, p2))
    assert any(y['tongue_timer'] and not x['tongue_timer'] for x, y in zip(p1, p2))
    assert max(r['x'] for r in p2) > p2[0]['x'] and len({r['x'] for r in p1}) == 1
    for name, waiting in (('catchup.csv', 3), ('fall.csv', 2)):
        a = [r for r in rows(root, name) if r['player'] == 1]
        assert any(r['life'] == waiting for r in a) and a[-1]['life'] == 0, name
        if waiting == 3:
            assert all(r['mount'] == a[0]['mount'] and r['riding'] for r in a)
        else:
            assert a[-1]['mount'] == 0xffffffff and a[-1]['draw_offset'] == 0
    owner = [r for r in rows(root, 'owner-final.csv') if r['player'] == 1]
    assert all(not r['riding'] and not r['draw_offset'] for r in owner)
    rom = (ROOT/'smw.sfc').read_bytes()
    for name, uses, mounts, tile in (
            ('first-grant.sav', 1, 1, 0x126),
            ('second-grant.sav', 2, 2, 0x132),
            ('second-yoshi.sav', 1, 2, 0x126),
            ('at-cap.sav', 2, 2, 0x132)):
        f = Fixture(root/name)
        assert f.sources == [(0x106, 0, 864, 336, 0x126, uses)], name
        d, r = f.data, f.ram
        assert sum(d[r+0x14c8+i] == 8 and d[r+0x9e+i] == 0x35
                   for i in range(12)) == mounts, name
        at = rom[0x3a60+3]+(rom[0x3a9c+3] << 8)+0x156
        assert d[r+at]+(d[r+at+0x10000] << 8) == tile, name
        if name == 'at-cap.sav':
            assert any(d[r+0x14c8+i] == 8 and d[r+0x9e+i] == 0x78 for i in range(12))
    print(f'Mount feedback passed: {len(b)} matching control records, independent riders/tongues, '
          'dismount, catch-up/death recovery, owner offset, two grants and cap reward.')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path)
    check(ap.parse_args().captures)
