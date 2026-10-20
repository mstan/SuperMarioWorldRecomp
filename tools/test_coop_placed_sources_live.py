#!/usr/bin/env python3
"""Placed egg grants, native colors, cap, exhaustion and slot-pressure replay.

Sources/positions and occupied/free slots are staged offline. Original proximity,
hatching, children, alternate rewards and source consumption execute in game.
This is focused adapter acceptance, not a campaign or terrain-streaming audit.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key
from test_coop_baby_live import egg, placed
from test_coop_mouth_live import same, set_actor


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav')


def contents(out):
    return [int(m[1], 16) for m in re.finditer(
        r'\b01:15(?:1[C-F]|2[0-7])=([0-9A-F]{2}) w1[^\n]*IPC=01F78A',
        (out/'wram-writes.log').read_text())]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='unowned YI2 fixture')
    ap.add_argument('--single', type=Path, required=True)
    ap.add_argument('--two', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures = {}
    for player in (0, 1):
        for x in (64, 80, 96, 112):
            key = f'empty-{player}-{x}'
            f = Fixture(args.source)
            f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
            for p in (0, 1):
                set_actor(f, p, x-16 if p == player else 200, 352, False)
            egg(f, 4, x)
            path = root/f'{key}.sav'
            f.save(path)
            fixtures[key] = path
    for key in ('rider0', 'rider1', 'cap-adults'):
        path = root/f'{key}.sav'
        placed(args, key, path)
        fixtures[key] = path
    f = Fixture(fixtures['empty-0-64'])
    for slot in range(12):
        if slot != 4:
            loose_key(f, 176, 368, slot)
    path = root/'pressure.sav'
    f.save(path)
    fixtures['pressure'] = path
    script = root/'grants.script'
    script.write_text('wait 60\nloadstate 10\nwait 12\nsavestate 9\nwait 35\n'
                      'savestate 8\nwait 70\nsavestate 7\nloadstate 9\n'
                      'wait 35\nsavestate 5\nwait 70\nsavestate 6\nwait 5\n')
    follow = root/'follow.script'
    follow.write_text('wait 60\nloadstate 10\nwait 100\nsavestate 9\n'
                      'loadstate 10\nwait 100\nsavestate 8\nwait 5\n')
    results, parity = [], {}

    def run(source, name, narrow, bounce, inputs=script):
        out = root/name
        cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
               '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
               '--snapshot', str(source.resolve()), '--script', str(inputs),
               '--out', str(out), '--frames', '300', '--framedump', '170',
               '--wram-watch', '151c:1649', '--mouth-trace']
        if narrow:
            cmd.append('--native')
        if bounce:
            cmd.append('--bounce')
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert not report['returncode'] and not report['timed_out'] and not report['dispatch_misses'], out
        return out

    def record(case, out, bounce, snapshots):
        if bounce:
            for a, b in zip(snapshots, parity[case]):
                same(a, b)
        else:
            parity[case] = snapshots
        results.append(dict(case=case, bounce=bounce, status='passed',
                            evidence=out.name, parity_snapshots=len(snapshots),
                            contents=contents(out)))
        (root/'summary.json').write_text(json.dumps(dict(checks=results,
            campaign='unverified', slot_release='staged offline'), indent=2)+'\n')

    for bounce in (False, True):
        for narrow in (True, False):
            view = 'standard' if narrow else 'wide'
            suffix = f'{view}-{"bounce" if bounce else "native"}'
            for key, source in fixtures.items():
                out = run(source, f'{key}-{suffix}', narrow, bounce)
                first, second, final, replay = (state(out, n) for n in (9, 8, 7, 6))
                assert len(first.placed_eggs) == len(final.placed_eggs) == 1, out
                assert first.placed_eggs[0][4] == 1 and first.placed_eggs[0][5], out
                assert final.placed_eggs[0][4:] == (1 if key == 'pressure' else 2, 0), out
                wanted = [0x78]*3 if key == 'cap-adults' else (
                    [0x2d, 0x78, 0x78] if key.startswith('rider') else
                    [0x2d] if key == 'pressure' else [0x2d]*3)
                assert contents(out) == wanted, (out, contents(out), wanted)
                if key.startswith('empty'):
                    babies = [i for i in range(12) if final.data[final.ram+0x14c8+i]
                              and final.data[final.ram+0x9e+i] == 0x2d]
                    assert len(babies) == 2, (out, babies)
                    palette = (8, 6, 4, 6)[(int(key.split('-')[-1]) >> 4) & 3]
                    assert all(final.data[final.ram+0x15f6+i] & 14 == palette for i in babies), out
                    assert all(not final.data[final.ram+0x163e+i] for i in babies), out
                if key == 'pressure':
                    assert all(final.data[final.ram+0x14c8+i] for i in range(12)), out
                same(final, replay)
                record(f'{key}-{view}', out, bounce, (first, second, final))

                # Reintroduce a loader egg at the same exhausted origin, or
                # release one physical slot after a genuine full-pool wait.
                if key not in ('empty-0-64', 'pressure'):
                    continue
                f = Fixture(out/'saves/native-coop-v1/save7.sav')
                if key == 'pressure':
                    assert f.data[f.ram+0x9e+11] == 0x80
                    f.data[f.ram+0x14c8+11] = 0
                else:
                    slot = next(i for i in range(12) if not f.data[f.ram+0x14c8+i])
                    egg(f, slot, 64)
                path = root/f'{key}-follow-{suffix}.sav'
                f.save(path)
                out = run(path, f'{key}-follow-{suffix}', narrow, bounce, follow)
                final, replay = (state(out, n) for n in (9, 8))
                assert final.placed_eggs[0][4:] == (2, 0), out
                assert contents(out) == ([0x2d]*2 if key == 'pressure' else []), out
                babies = [i for i in range(12) if final.data[final.ram+0x14c8+i]
                          and final.data[final.ram+0x9e+i] == 0x2d]
                assert len(babies) == 2, (out, babies)
                same(final, replay)
                record(f'{key}-follow-{view}', out, bounce, (final,))
    print(f'{len(results)} placed-source cases passed with parity and replay')


if __name__ == '__main__':
    main()
