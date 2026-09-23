"""Offline copied native co-op states for focused gameplay tests.

Staged positions/equipment are explicit test inputs, not campaign evidence.
This utility never communicates with or pauses a running guest.
"""
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


class Fixture:
    def __init__(self, source):
        self.source = source.resolve()
        self.data = bytearray(source.read_bytes())
        d = self.data
        assert d[:8] == b'RSGUARD\1' and d[16:48].rstrip(b'\0') == b'smw.us.native-coop.v1'
        assert struct.unpack_from('<Q', d, 8)[0] == len(d)-52
        self.check_crc(0, len(d))
        assert struct.unpack_from('<II', d, 48) == (0x52544c53, 9)
        assert d[-8:-4] == b'CNRE'
        self.end = len(d)-12
        self.machine = self.end-self.word(self.end)
        m = self.machine
        self.version = self.word(m+4)
        assert d[m:m+4] == b'CNR1' and self.version in (2, 3, 4, 5)
        self.check_crc(m, self.end)
        self.core = m+64
        self.core_end = self.core+self.word(m+40)
        c = self.core
        assert d[c:c+4] == b'COOP' and self.word(c+4) == 1
        self.check_crc(c, self.core_end)
        extra = m-145
        assert d[extra:extra+8] == b'SMC1SMWS'
        # RTLS v9: ramAdr(4), legacy joypad(7), JoypadState(58).
        self.ram = extra-69-0x20000
        assert d[self.ram+0x100] == 0x14
        fields = [(int(a, 16), int(n)) for a, n in re.findall(
            r'COOP_FIELD\(\w+,\s*(0x[0-9a-f]+),\s*(\d+)\)',
            (ROOT/'src/mods/coop/coop_player_fields.def').read_text())]
        layout = 2166136261
        self.offsets = {}
        for address, size in fields:
            layout = ((layout ^ address)*16777619) & 0xffffffff
            layout = ((layout ^ size)*16777619) & 0xffffffff
            for at in range(address, address+size):
                self.offsets[at] = len(self.offsets)
        assert self.word(m+48) == layout
        count = self.word(c+8)
        assert count == self.word(m+44)
        records = {self.word(c+136+i*96): c+136+i*96 for i in range(count)}
        self.actors = {}
        at = self.core_end
        for _ in range(count):
            player = self.word(at)
            self.actors[player] = (at+16, records[player])
            at += 16+len(self.offsets)+(self.word(at+8)+self.word(at+12))*540
        self.entities = {}
        if self.version >= 3:
            assert d[at:at+4] == b'ENT1'
            count = self.word(at+4)
            self.next_entity, self.level = self.word(at+8), self.word(at+12)
            at += 16
            for _ in range(count):
                entity = struct.unpack_from('<7I', d, at)
                self.entities[entity[0]] = entity
                at += 28
        if self.version >= 4:
            assert d[at:at+4] == b'CAM1'
            self.focus = at
            at += 32
        self.mounts, self.sources = {}, []
        if self.version >= 5:
            assert d[at:at+4] == b'YSH1'
            mounts, sources = self.word(at+4), self.word(at+8)
            at += 16
            for _ in range(mounts):
                identity, pending, visible = struct.unpack_from('<3I', d, at)
                self.mounts[identity] = bytes(d[at+12:at+33])
                at += 33+(pending+visible)*540
            for _ in range(sources):
                self.sources.append(struct.unpack_from('<6I', d, at))
                at += 24
        assert at == self.end-4

    def word(self, at):
        return struct.unpack_from('<I', self.data, at)[0]

    def put(self, at, value):
        struct.pack_into('<I', self.data, at, value)

    def check_crc(self, start, end):
        assert self.word(end-4) == zlib.crc32(self.data[start:end-4])

    def actor(self, player, x, y, power, reserve, life=0, recovery=0):
        guest, record = self.actors[player]
        changes = {0x19: power, 0xdc2: reserve, 0x71: 0, 0x72: 0, 0x73: 0,
                   0x77: 4, 0x7a: 0, 0x7b: 0, 0x7c: 0, 0x7d: 0,
                   0x78: 0xff if life in (2, 3) else 0}
        for addr, value in ((0x94, x), (0x96, y), (0xd1, x), (0xd3, y)):
            changes[addr] = value & 255
            changes[addr+1] = value >> 8
        for addr, value in changes.items():
            self.data[guest+self.offsets[addr]] = value
        if player == self.word(self.core+20):
            for addr, offset in self.offsets.items():
                self.data[self.ram+addr] = self.data[guest+offset]
        for offset, value in ((8, life), (12, power), (16, reserve), (20, x+8),
                              (24, y+(19 if power else 26)), (32, 26 if power else 12),
                              (44, recovery)):
            self.put(record+offset, value)

    def camera(self, x):
        for addr, value in ((0x1a, x), (0x1e, x//2), (0x1462, x), (0x1466, x//2)):
            struct.pack_into('<H', self.data, self.ram+addr, value)

    def save(self, output):
        if output.resolve() == self.source:
            raise ValueError('source and output must differ')
        for start, end in ((self.core, self.core_end), (self.machine, self.end), (0, len(self.data))):
            self.put(end-4, zlib.crc32(self.data[start:end-4]))
        output.write_bytes(self.data)
