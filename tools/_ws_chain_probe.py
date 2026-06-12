#!/usr/bin/env python3
"""Scratch probe for the $5F brown-chain-platform widescreen bug.

Reads live sprite tables (never pauses): per slot the sprite number,
status, world pos, swing speed ($1504), swing angle ($151C/$1528) and
the private way-offscreen flag ($15C4). Run repeatedly / timeseries
mode to watch slot cycling.

  python tools/_ws_chain_probe.py            # one snapshot
  python tools/_ws_chain_probe.py watch N    # N samples, ~1/frame batch
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
from dbg import Dbg

d = Dbg()


def snap():
    cam = d.read(0x1A, 4)
    camx = cam[0] | (cam[1] << 8)
    camy = cam[2] | (cam[3] << 8)
    num = d.read(0x9E, 12)
    st = d.read(0x14C8, 12)
    xl = d.read(0xE4, 12)
    xh = d.read(0x14E0, 12)
    yl = d.read(0xD8, 12)
    yh = d.read(0x14D4, 12)
    spd = d.read(0x1504, 12)
    a0 = d.read(0x151C, 12)
    a1 = d.read(0x1528, 12)
    way = d.read(0x15C4, 12)
    return (d.frame(), camx, camy, num, st, xl, xh, yl, yh, spd, a0, a1, way)


def show(s):
    f, camx, camy, num, st, xl, xh, yl, yh, spd, a0, a1, way = s
    print(f"frame={f} camX={camx} camY={camy}")
    print(" slot num stat worldX worldY scrX  spd1504 ang  way15C4")
    for k in range(12):
        if st[k] == 0:
            continue
        wx = xl[k] | (xh[k] << 8)
        wy = yl[k] | (yh[k] << 8)
        sx = wx - camx
        ang = a0[k] | (a1[k] << 8)
        print(f"  {k:2d}  0x{num[k]:02x} 0x{st[k]:02x} {wx:6d} {wy:6d} {sx:5d}"
              f"  0x{spd[k]:02x}   0x{ang:04x}  0x{way[k]:02x}")


if len(sys.argv) > 1 and sys.argv[1] == "watch":
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    print("frame camX | per-$5F-slot: stat scrX spd ang way")
    for _ in range(n):
        f, camx, camy, num, st, xl, xh, yl, yh, spd, a0, a1, way = snap()
        cells = []
        for k in range(12):
            if num[k] != 0x5F:
                continue
            wx = xl[k] | (xh[k] << 8)
            cells.append(f"s{k}[st={st[k]:02x} scrX={wx-camx:4d} "
                         f"spd={spd[k]:02x} ang={a0[k]|(a1[k]<<8):04x} "
                         f"way={way[k]:02x}]")
        print(f"{f:7d} {camx:5d} {camy:4d} | " + "  ".join(cells))
        time.sleep(0.20)
else:
    show(snap())
