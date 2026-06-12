#!/usr/bin/env python3
"""Scratch analyzer for the widescreen chain-platform bug (ISSUES 2026-06-12).

Post-hoc ring scan (always-on rings; never pauses). Two modes:

  python tools/_ws_oam_trace.py live          # find $A3 slot now + dump it
  python tools/_ws_oam_trace.py scan [extra]  # scan whole OAM-render ring for
                                              # platform/ball tiles + xhigh/Y

Platform wooden tiles ~ 0x60/0x61/0x62 ; gold-ball chain link ~ 0xa2.
"""
import json, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from dbg import Dbg

PLAT_TILES = {0x60, 0x61, 0x62, 0xa2}
WS_EXTRA = 71

d = Dbg()

def s16(v):
    return v - 0x10000 if v & 0x8000 else v

mode = sys.argv[1] if len(sys.argv) > 1 else "scan"
if len(sys.argv) > 2:
    WS_EXTRA = int(sys.argv[2])
THR = 256 + WS_EXTRA

def find_a3():
    num = d.read(0x9e, 12)
    sta = d.read(0x14c8, 12)
    return [(s, sta[s]) for s in range(12) if num[s] == 0xA3]

if mode == "live":
    cam = d.read(0x1a, 2); camx = cam[0] | (cam[1] << 8)
    a3 = find_a3()
    print(f"frame={d.frame()} camX={camx} thr={THR}  $A3 slots: {a3}")
    if not a3:
        print("No $A3 live right now. Ride the platform so it's on screen, then rerun.")
        sys.exit(0)
    fr = d.frame()
    for slot, st in a3:
        print(f"\n-- slot {slot} status 0x{st:02x} sprite_timeseries --")
        r = d.cmd(f"sprite_timeseries {slot} {fr-240} {fr} 1 60")
        print(json.dumps(r)[:1500])
    sys.exit(0)

# scan mode: pull the whole OAM-render ring, report platform-tile rows.
r = d.cmd("oam_render_get 256 128")
snaps = r.get("snaps", [])
print(f"oam_render seq={r.get('seq')} count={r.get('count')} snaps={len(snaps)} thr={THR}")
print("frame oslot tile  y  xlow xhigh  9bitX presented  note")
hits = 0
for e in snaps:
    f = e["f"]
    for i, sl in enumerate(e["slot"]):
        y, xl, xh, tile, attr = sl
        if tile not in PLAT_TILES:
            continue
        ninebit = (xh << 8) | xl
        pres = ninebit - 512 if ninebit >= THR else ninebit
        note = []
        if y >= 0xf0:
            note.append("HIDDEN(y=240)")
        if xh and xl < WS_EXTRA:
            note.append("RIGHT-MARGIN")
        if THR <= ninebit < 512:
            note.append("WRAP-LEFT")
        # only print interesting rows (margin / hidden / wrapped)
        if note:
            hits += 1
            print(f"{f:6d} {i:4d}  0x{tile:02x} {y:3d}  {xl:3d}   {xh}    {ninebit:4d}  {pres:5d}    {' '.join(note)}")
print(f"\n{hits} flagged platform-tile rows across {len(snaps)} snapshots")
