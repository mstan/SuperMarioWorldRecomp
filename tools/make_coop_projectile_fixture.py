#!/usr/bin/env python3
"""Stage two fire-powered actors in a copied stock YI2 entrance state.

No shots or successful contacts are pre-created. The script makes two real
fire-button presses per actor and captures the resulting guest states.
These are focused allowance tests, not campaign/terrain acceptance.
"""
import argparse
from pathlib import Path
from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key


def make(source, output, encounter='allowance'):
    fixture = Fixture(source)
    assert fixture.version == 5 and not fixture.mounts and fixture.level == 0x106
    assert set(fixture.actors) == {0, 1}
    data, ram = fixture.data, fixture.ram
    for slot in range(12):
        data[ram+0x14c8+slot] = 0
    # All twelve original extended-sprite tables, ten bytes per table.
    data[ram+0x170b:ram+0x1783] = bytes(120)
    for player, x in ((0, 40), (1, 88 if encounter == 'allowance' else 40)):
        fixture.actor(player, x, 352, 3, 0)
        guest, _ = fixture.actors[player]
        for address, value in ((0x76, 1), (0x149c, 0), (0x1497, 0), (0x1490, 0)):
            data[guest+fixture.offsets[address]] = value
            if player == fixture.word(fixture.core+20):
                data[ram+address] = value
    if encounter == 'chuck':
        loose_key(fixture, 112, 352, slot=0)
        rom = (ROOT/'smw.sfc').read_bytes()
        for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                    (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                    (0x1686, 0x3f590), (0x190f, 0x3f659)):
            data[ram+table] = rom[source_table+0x91]
        data[ram+0x9e] = 0x91
        # Native initialization and AI run normally; no hit or freeze supplied.
        data[ram+0x14c8] = 1
    fixture.camera(0)
    fixture.put(fixture.core+92, 256)
    fixture.put(fixture.focus+24, 0)
    fixture.put(fixture.focus+28, 0)
    fixture.save(output)
    if encounter == 'chuck':
        return ('wait 60\nloadstate 10\nwait 2\npress y+p2.y 1\n'
                'wait 45\nsavestate 11\nwait 5\n')
    return ('wait 60\nloadstate 10\nwait 2\npress y+p2.y 1\nwait 1\n'
            'savestate 11\nwait 12\npress y+p2.y 1\nwait 1\n'
            'savestate 9\nwait 12\npress y+p2.y 1\nwait 1\n'
            'savestate 8\nwait 10\nsavestate 7\n'
            'loadstate 8\nwait 10\nsavestate 6\nwait 100\n'
            'savestate 5\npress y+p2.y 1\nwait 1\nsavestate 4\nwait 5\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--encounter', choices=('allowance', 'chuck'), default='allowance')
    args = parser.parse_args()
    script = make(args.source, args.output, args.encounter)
    args.output.with_suffix('.script').write_text(script, encoding='ascii', newline='\n')
