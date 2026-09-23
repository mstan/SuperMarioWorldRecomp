#!/usr/bin/env python3
"""Create an offline YI2 pipe test fixture from a native co-op entrance state.

This deliberately positions actors and assigns equipment in a COPY of a state.
It tests the real pipe collision/loader, not natural campaign reachability. No
running guest is paused or patched. The original file is never overwritten.
"""
import argparse
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def make(source, output, entrant):
    if source.resolve() == output.resolve():
        raise ValueError('source and output must differ')
    data = bytearray(source.read_bytes())
    def word(at):
        return struct.unpack_from('<I', data, at)[0]
    def put(at, value):
        struct.pack_into('<I', data, at, value)
    def check_crc(start, end):
        assert word(end - 4) == zlib.crc32(data[start:end - 4])
    assert data[:8] == b'RSGUARD\1' and data[16:48].rstrip(b'\0') == b'smw.us.native-coop.v1'
    assert struct.unpack_from('<Q', data, 8)[0] == len(data) - 52
    check_crc(0, len(data))
    assert struct.unpack_from('<II', data, 48) == (0x52544c53, 9)
    assert data[-8:-4] == b'CNRE'
    end = len(data) - 12
    machine = end - word(end)
    assert data[machine:machine+4] == b'CNR1' and word(machine+4) == 2
    check_crc(machine, end)
    core = machine + 64
    core_end = core + word(machine+40)
    assert data[core:core+4] == b'COOP' and word(core+4) == 1 and word(core+8) == 2
    check_crc(core, core_end)
    extra = machine - 145
    assert data[extra:extra+4] == b'SMC1' and data[extra+4:extra+8] == b'SMWS'
    # RTLS v9: ramAdr(4), legacy joypad fields(7), JoypadState(58).
    ram = extra - 69 - 0x20000
    assert data[ram+0x100] == 0x14 and data[ram+0x13bf] == 0x2a
    fields = [(int(a, 16), int(n)) for a, n in re.findall(
        r'COOP_FIELD\(\w+,\s*(0x[0-9a-f]+),\s*(\d+)\)',
        (ROOT/'src/mods/coop/coop_player_fields.def').read_text())]
    layout = 2166136261
    offsets = {}
    for address, size in fields:
        layout = ((layout ^ address) * 16777619) & 0xffffffff
        layout = ((layout ^ size) * 16777619) & 0xffffffff
        for at in range(address, address+size):
            offsets[at] = len(offsets)
    assert word(machine+48) == layout
    at = core_end
    for player in range(2):
        assert word(at) == player
        guest = at+16
        x = 3880 if entrant == 'both' or player == int(entrant) else 3760
        y = 320 if x == 3880 else 352
        power, reserve = (1, 1) if player == 0 else (3, 4)
        changes = {0x19: power, 0xdc2: reserve, 0x71: 0, 0x72: 0, 0x73: 0,
                   0x77: 4, 0x7a: 0, 0x7b: 0, 0x7c: 0, 0x7d: 0}
        for addr, value in ((0x94, x), (0x96, y), (0xd1, x), (0xd3, y)):
            changes[addr] = value & 255
            changes[addr+1] = value >> 8
        for addr, value in changes.items():
            data[guest+offsets[addr]] = value
        if player == 0:
            for addr, offset in offsets.items():
                data[ram+addr] = data[guest+offset]
        record = core+136+player*96
        for offset, value in ((12, power), (16, reserve), (20, x+8),
                              (24, y+19), (32, 26)):
            put(record+offset, value)
        at += 16+len(offsets)+(word(at+8)+word(at+12))*540
    assert at == end-4
    for addr, value in ((0x1a, 3744), (0x1e, 1872), (0x1462, 3744), (0x1466, 1872)):
        struct.pack_into('<H', data, ram+addr, value)
    put(core_end-4, zlib.crc32(data[core:core_end-4]))
    put(end-4, zlib.crc32(data[machine:end-4]))
    put(len(data)-4, zlib.crc32(data[:-4]))
    output.write_bytes(data)
    print(f'Created staged YI2 pipe fixture: {output} (entrant={entrant})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--entrant', choices=('0', '1', 'both'), default='1')
    args = parser.parse_args()
    make(args.source, args.output, args.entrant)
