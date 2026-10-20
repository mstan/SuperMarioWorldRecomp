#!/usr/bin/env python3
"""Copied stock YI2 encounters for simultaneous native contact acceptance.

Coordinates/equipment are staged inputs. No enemy outcome, ownership, reward,
or successful contact is pre-applied. These are not terrain/campaign audits.
"""
import argparse
from pathlib import Path

from coop_fixture import Fixture, ROOT
from make_coop_contact_fixture import make as make_contact
from make_coop_carry_fixture import loose_key

POWERUPS = {'mushroom': 0x74, 'flower': 0x75, 'invincibility': 0x76,
            'feather': 0x77, 'life': 0x78, 'coin': 0x21}
CASES = ('stomp', 'spin', 'spiny', 'star', 'side', 'protected',
         'pickup-tie', 'pickup-nearest', 'pickup-eligible', 'pickup-fence',
         *(f'{item}-{winner}' for item in POWERUPS for winner in ('tie', 'nearest')),
         'dual-transform-primary-short', 'dual-transform-secondary-short')


def make(source, output, case, camera=192):
    # Reuse the same stock sprite/tweaker setup as the single-actor control.
    base = output.with_suffix('.base.sav')
    falling = case in ('stomp', 'spin', 'spiny')
    make_contact(source, base, 'stomp' if falling else
                 'pickup' if case.startswith('pickup') else 'side', 0, camera)
    f = Fixture(base)
    if case.startswith('pickup'):
        # Let the scripted held input reach both seats before the first
        # contact. Without this native cooldown the load frame kicks it.
        f.data[f.ram+0x154c] = 3
    for actor in f.actors:
        f.actor(actor, 348 if falling else 320, 270 if falling else 304, 0, 0)
        changes = {0x1490: 255 if case == 'star' else 0,
                   0x1497: 100 if case == 'protected' and actor == 0 else 0,
                   0x140d: int(case in ('spin', 'spiny')),
                   0x13f9: int(case == 'pickup-fence' and actor == 0)}
        if falling:
            changes.update({0x72: 0x24, 0x77: 0, 0x7d: 0x20})
        for at, value in changes.items():
            f.data[f.actors[actor][0]+f.offsets[at]] = value
            if actor == f.word(f.core+20):
                f.data[f.ram+at] = value
    if case.endswith('-nearest') or case == 'pickup-fence':
        player = 1 if case == 'pickup-fence' else 0
        guest, record = f.actors[player]
        for at in (0x94, 0xd1):
            f.data[guest+f.offsets[at]] = 316 & 255
            f.data[guest+f.offsets[at+1]] = 316 >> 8
            if player == f.word(f.core+20):
                f.data[f.ram+at:f.ram+at+2] = (316).to_bytes(2, 'little')
        f.put(record+20, 324)
    if case == 'pickup-fence':
        # Native CODE_00EA32 clears the back-of-fence flag if there is no
        # climbable tile. Supply real Map16 net contacts (00:F2EE), rather
        # than claiming a flag that the player pipeline immediately clears.
        # VRAM is deliberately not rebuilt: this is contact, not visual QA.
        rom = (ROOT/'smw.sfc').read_bytes()
        assert not f.data[f.ram+0x5b] & 1
        for x in range(304, 353, 16):
            screen = x >> 8
            base = rom[0x3a60+screen] | (rom[0x3a9c+screen] << 8)
            for y in range(288, 336, 16):
                at = base+(y & 0xf0)+((x & 255) >> 4)+(y & 0xff00)
                assert 0xc800 <= at <= 0xffff
                f.data[f.ram+at] = 7
                f.data[f.ram+at+0x10000] = 0
    item = case.split('-')[0]
    if case == 'spiny' or item in POWERUPS:
        sprite = 0x13 if case == 'spiny' else POWERUPS[item]
        rom = (ROOT/'smw.sfc').read_bytes()
        for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                    (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                    (0x1686, 0x3f590), (0x190f, 0x3f659)):
            f.data[f.ram+table] = rom[source_table+sprite]
        f.data[f.ram+0x15f6] = f.data[f.ram+0x166e] & 15
        f.data[f.ram+0x9e] = sprite
    if case.startswith('dual-transform'):
        loose_key(f, 320, 320, slot=1)
        rom = (ROOT/'smw.sfc').read_bytes()
        types = (0x74, 0x75) if case.endswith('primary-short') else (0x75, 0x74)
        # Native descending slot order: slot 1 goes to P1 on the exact tie.
        # Slot 0 then goes to P2 because P1's transformation is ineligible.
        for slot, sprite in enumerate(types):
            for table, source_table in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                                        (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                                        (0x1686, 0x3f590), (0x190f, 0x3f659)):
                f.data[f.ram+table+slot] = rom[source_table+sprite]
            f.data[f.ram+0x15f6+slot] = f.data[f.ram+0x166e+slot] & 15
            f.data[f.ram+0x9e+slot] = sprite
            f.data[f.ram+0x14c8+slot] = 8
    records = [record for _, record in f.actors.values()]
    for offset, value in ((12, sum(f.word(a+20) for a in records)//len(records)),
                          (16, sum(f.word(a+24) for a in records)//len(records))):
        f.put(f.focus+offset, value)
    f.save(output)
    held = ('p2.y' if case == 'pickup-eligible' else 'y+p2.y')
    if case.startswith('dual-transform'):
        return 'wait 60\nloadstate 10\nwait 60\npress right+p2.right 30\nsavestate 11\nwait 5\n'
    return f'wait 60\nloadstate 10\n' + (
        f'press {held} 50\nwait 40\n' if case.startswith('pickup') else 'wait 90\n'
    ) + 'savestate 11\nwait 5\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--case', choices=CASES, required=True)
    parser.add_argument('--camera', type=int, choices=(0, 192, 384), default=192)
    args = parser.parse_args()
    script = make(args.source, args.output, args.case, args.camera)
    args.output.with_suffix('.script').write_text(script, encoding='ascii', newline='\n')
