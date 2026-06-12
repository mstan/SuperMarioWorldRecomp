#!/usr/bin/env python3
"""Parse a level's sprite list straight from the ROM (ground truth for
spawn investigations). Usage: python tools/_parse_level_sprites.py 27
"""
import sys

LEVEL = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x27
rom = open("smw.sfc", "rb").read()
HDR = len(rom) % 0x8000  # 0x200 if copier-headered, 0 if not (project smw.sfc is unheadered)


def lorom(bank, addr):
    return bank * 0x8000 + (addr & 0x7FFF) + HDR


# Sprite pointer table: 2 bytes/level at $05:EC00, data bank $07.
pt = lorom(0x05, 0xEC00) + LEVEL * 2
ptr = rom[pt] | (rom[pt + 1] << 8)
off = lorom(0x07, ptr)
print(f"level {LEVEL:02X}: sprite ptr $07:{ptr:04X} file_off 0x{off:X}")
print(f"header byte: 0x{rom[off]:02X}")
off += 1
i = 0
print("idx  num  screen xtile ytile  worldX worldY  extra")
while rom[off] != 0xFF:
    b1, b2, b3 = rom[off], rom[off + 1], rom[off + 2]
    y = ((b1 & 0x01) << 4) | (b1 >> 4)
    screen = ((b1 & 0x02) << 3) | (b2 & 0x0F)
    x = b2 >> 4
    extra = (b1 >> 2) & 3
    wx = screen * 256 + x * 16
    wy = y * 16
    mark = "  <-- $5F" if b3 == 0x5F else ""
    print(f"{i:3d}  0x{b3:02X}  {screen:4d}  {x:4d}  {y:4d}  {wx:6d} {wy:6d}  {extra}{mark}")
    off += 3
    i += 1
