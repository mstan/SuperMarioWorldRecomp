#!/usr/bin/env python3
"""Placed-egg cap and baby held-food checks with original native routines.

Actors, native source sprites and equipment are staged offline. Actual pickup,
held movement, release, food consumption and egg hatching use gameplay code.
This does not qualify repeated placed-source grants or competing fresh mouths.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key
from make_coop_keyhole_fixture import sprite_type
from test_coop_mouth_live import same, set_actor

NONE = 0xffffffff


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav')


def egg(f, slot, x, pending=False):
    loose_key(f, x, 352, slot)
    sprite_type(f, slot, 0x2c)
    f.data[f.ram+0x14c8+slot] = 8 if pending else 1
    if pending:
        f.data[f.ram+0x151c+slot] = 0x35
        f.data[f.ram+0x1540+slot] = 0x40


def placed(args, kind, output):
    two = kind in ('cap-adults', 'cap-bubble')
    base = args.two if two else args.source if 'babies' in kind else args.single
    f = Fixture(base)
    if kind == 'rider1':
        identity = f.word(f.actors[0][1]+52)
        f.put(f.actors[0][1]+52, NONE)
        f.put(f.actors[1][1]+52, identity)
        at = f.data.index(b'ENT1', f.core_end)+16
        for index, key in enumerate(f.entities):
            if key == identity:
                f.put(at+index*28+16, 1)
        for p in (0, 1):
            set_actor(f, p, 160 if p == 0 else 40, 352 if p == 0 else 336, p == 1)
            guest, _ = f.actors[p]
            for address, value in ((0x187a, int(p == 1)), (0x188b, 0)):
                f.data[guest+f.offsets[address]] = value
                if p == 0:
                    f.data[f.ram+address] = value
    elif 'babies' in kind:
        f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
        for p, x in ((0, 40), (1, 160)):
            set_actor(f, p, x, 352, False)
        for slot in range(2 if kind == 'cap-babies' else 1):
            loose_key(f, 200+slot*40, 368, slot)
            sprite_type(f, slot, 0x2d)
            f.data[f.ram+0x14c8+slot] = 1
    elif not two:
        set_actor(f, 1, 160, 352, False)
    if kind == 'cap-pending':
        egg(f, 5, 208, True)
    if kind == 'cap-bubble':
        guest, record = f.actors[1]
        f.put(record+8, 3)
        f.put(record+44, 180)
        f.data[guest+f.offsets[0x78]] = 255
    egg(f, 4, 64)
    f.save(output)


def held(source, output, actor):
    f = Fixture(source)
    assert all(f.word(record+52) == NONE and f.word(record+56) == NONE
               for _, record in f.actors.values())
    assert all(e[4] == NONE and e[6] == 0 for e in f.entities.values())
    f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
    for p in f.actors:
        set_actor(f, p, 88 if p == actor else 40, 352, False)
        g, _ = f.actors[p]
        f.data[g+f.offsets[0x76]] = 1
        if p == 0:
            f.data[f.ram+0x76] = 1
    loose_key(f, 104, 368, 1)
    sprite_type(f, 1, 4)
    f.data[f.ram+0x14c9] = 9  # native stunned green shell
    loose_key(f, 180, 368, 4)
    sprite_type(f, 4, 0x2d)
    f.data[f.ram+0x14cc] = 1  # original InitKey_BabyYoshi
    f.save(output)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='unowned CNR5 YI2 entrance')
    ap.add_argument('--single', type=Path, required=True, help='one native rider')
    ap.add_argument('--two', type=Path, required=True, help='two native riders')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--resume', action='store_true',
                    help='Recheck successful evidence with identical inputs and retry incomplete cases separately')
    ap.add_argument('--timeout-seconds', type=float, default=300)
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=args.resume)
    results, parity = [], {}

    def run(name, source, script, frames, narrow, bounce):
        path = root/f'{name}.script'
        path.write_text(script, encoding='ascii', newline='\n')
        out = root/name
        if out.exists() and args.resume:
            report_path = out/'result.json'
            report = json.loads(report_path.read_text()) if report_path.exists() else {}
            if report.get('returncode') == 0 and not report.get('timed_out') and not report.get('dispatch_misses'):
                old = json.loads((out/'inputs.json').read_text())['inputs']
                for field, source_path in (('exe', args.exe), ('rom', args.rom),
                                           ('snapshot', source), ('script', path)):
                    assert old[field]['sha256'] == hashlib.sha256(source_path.read_bytes()).hexdigest(), (
                        out, 'resume input changed', field)
                return out
            attempt = 1
            while out.exists():
                out = root/f'{name}-retry{attempt}'
                attempt += 1
        cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
               '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
               '--snapshot', str(source.resolve()), '--script', str(path), '--out', str(out),
               '--frames', str(frames), '--framedump', str(frames-10),
               '--timeout-seconds', str(args.timeout_seconds),
               '--wram-watch', '151c:1649']
        if narrow:
            cmd.append('--native')
        if bounce:
            cmd.append('--bounce')
        subprocess.run(cmd, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out'], name
        return out

    def record(key, name, out, slots, bounce):
        snapshots = [state(out, slot) for slot in slots]
        if bounce:
            for a, b in zip(snapshots, parity[key]):
                same(a, b)
        else:
            parity[key] = snapshots
        results.append(dict(case=key, bounce=bounce, status='passed', evidence=out.name,
                            parity_snapshots=slots))
        (root/'summary.json').write_text(json.dumps(dict(
            checks=results, campaign='unverified',
            repeated_placed_grants='covered by test_coop_placed_sources_live.py',
            fresh_mouth_arbitration='unverified'), indent=2)+'\n')

    fixtures = {}
    kinds = ('rider0', 'rider1', 'one-babies', 'cap-adults', 'cap-babies', 'cap-pending', 'cap-bubble')
    for kind in kinds:
        fixtures[kind] = root/f'{kind}.sav'
        placed(args, kind, fixtures[kind])
    for actor in (0, 1):
        fixtures[f'held{actor}'] = root/f'held{actor}.sav'
        held(args.source, fixtures[f'held{actor}'], actor)

    for bounce in (False, True):
        for narrow in (True, False):
            width = 'standard' if narrow else 'wide'
            backend = 'bounce' if bounce else 'native'
            for kind in kinds:
                key = f'{kind}-{width}'
                name = f'{key}-{backend}'
                out = run(name, fixtures[kind],
                          # Observe the first child before the repeated grant
                          # can hatch an alternate 1-up that the baby can eat.
                          'wait 60\nloadstate 10\nwait 42\nsavestate 9\n'
                          'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n',
                          147, narrow, bounce)
                choices = re.findall(r'^f\d+\s+01:1520=(2D|78) w1',
                                     (out/'wram-writes.log').read_text(), re.M)
                assert choices == ['78' if kind.startswith('cap-') else '2D'], (name, choices)
                f = state(out, 9)
                assert f.data[f.ram+0xa2] == (0x78 if kind.startswith('cap-') else 0x2d)
                if not kind.startswith('cap-'):
                    assert f.data[f.ram+0x14cc] == 9
                same(state(out, 8), state(out, 7))
                record(key, name, out, (9, 8), bounce)
            for actor in (0, 1):
                key = f'held{actor}-{width}'
                name = f'{key}-{backend}'
                prefix = '' if actor == 0 else 'p2.'
                out = run(name, fixtures[f'held{actor}'],
                          f'wait 60\nloadstate 10\npress {prefix}y+{prefix}right 60\nsavestate 9\nwait 5\n',
                          130, narrow, bounce)
                f = state(out, 9)
                identity = f.word(f.actors[actor][1]+56)
                assert identity != NONE and f.entities[identity][2] == 1
                assert f.data[f.ram+0x14c9] == 11
                assert f.data[f.ram+0x1612] == 255 and f.data[f.ram+0x1642] == 0
                rows = list(csv.DictReader((out/'coop.csv').open()))
                assert sum(r['player'] == str(actor) and r['held_slot'] == '1' for r in rows) >= 40
                record(key, name, out, (9,), bounce)
                source = out/'saves/native-coop-v1/save9.sav'
                key = f'release{actor}-{width}'
                name = f'{key}-{backend}'
                out = run(name, source,
                          'wait 60\nloadstate 10\nwait 10\nsavestate 9\nwait 50\nsavestate 8\n'
                          'loadstate 9\nwait 50\nsavestate 7\nwait 5\n', 185, narrow, bounce)
                pending, final = state(out, 9), state(out, 8)
                assert pending.data[pending.ram+0x1612] == 1 and pending.data[pending.ram+0x1642] > 32
                assert pending.data[pending.ram+0x14c9] != 11
                assert final.data[final.ram+0x14c9] == 0 and final.data[final.ram+0x1574] == 1
                assert final.data[final.ram+0xdbf] == f.data[f.ram+0xdbf]+1
                assert final.word(final.actors[actor][1]+56) == NONE
                same(final, state(out, 7))
                record(key, name, out, (9, 8), bounce)
    print(f'{len(results)} placed-egg and baby food checks passed with native/bounce parity')


if __name__ == '__main__':
    main()
