#!/usr/bin/env python3
"""Spawn-path probe for the right $5F chain platform (never-spawns bug).
Reads live slot tables + the level sprite load-status ring ($1938) for the
$5F trio. Never pauses."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from dbg import Dbg

d = Dbg()
cam = d.read(0x1A, 2)
camx = cam[0] | (cam[1] << 8)
num = d.read(0x9E, 12)
st = d.read(0x14C8, 12)
xl = d.read(0xE4, 12)
xh = d.read(0x14E0, 12)
li = d.read(0x161A, 12)
print(f"frame={d.frame()} camX={camx} mode=0x{d.u8(0x100):02x}")
for k in range(12):
    if st[k] == 0:
        continue
    wx = xl[k] | (xh[k] << 8)
    print(f"  slot{k:2d} num=0x{num[k]:02x} st=0x{st[k]:02x} worldX={wx}"
          f" loadidx={li[k]}")
ls = d.read(0x1938, 64)
print("load-status $1938[0..63]:")
for base in range(0, 64, 16):
    print("  ", " ".join(f"{ls[base+j]:02x}" for j in range(16)))
