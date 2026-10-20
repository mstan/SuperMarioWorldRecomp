#!/usr/bin/env python3
"""Both actors acquire the two actual children of one placed source.

Input is a completed placed-source test save, not fabricated child ownership.
Only approach positions are staged; native contacts and held input pick up the
two babies. This is not a campaign route or terrain-streaming check.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from test_coop_mouth_live import same, set_actor


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    f = Fixture(args.source)
    assert f.placed_eggs == [(0x106, 64, 352, 255, 2, 0)]
    babies = {e[0] for e in f.entities.values() if e[1] == 0 and e[3] == 0x2d
              and e[4] == 0xffffffff and f.data[f.ram+0x14c8+e[2]] == 9}
    assert len(babies) == 2
    for p, x in ((0, 24), (1, 104)):
        set_actor(f, p, x, 352, False)
    source = root/'approach.sav'
    f.save(source)
    script = root/'pickup.script'
    script.write_text('wait 60\nloadstate 10\npress y+right+p2.y+p2.left 50\n'
                      'savestate 9\nloadstate 10\npress y+right+p2.y+p2.left 50\n'
                      'savestate 8\nwait 5\n')
    results, parity = [], {}
    for bounce in (False, True):
        for narrow in (True, False):
            name = f'{"standard" if narrow else "wide"}-{"bounce" if bounce else "native"}'
            out = root/name
            cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(source), '--script', str(script), '--out', str(out),
                   '--frames', '185', '--framedump', '110']
            if narrow:
                cmd.append('--native')
            if bounce:
                cmd.append('--bounce')
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            report = json.loads((out/'result.json').read_text())
            assert not report['returncode'] and not report['timed_out'] and not report['dispatch_misses']
            a, b = (Fixture(out/f'saves/native-coop-v1/save{n}.sav') for n in (9, 8))
            held = {a.word(r+56) for g, r in a.actors.values()}
            assert held == babies, (out, held, babies)
            for p, (g, r) in a.actors.items():
                e = a.entities[a.word(r+56)]
                assert e[4] == p and e[6] == 1 and a.data[a.ram+0x14c8+e[2]] == 11
            same(a, b)
            if bounce:
                same(a, parity[narrow])
            else:
                parity[narrow] = a
            results.append(dict(case=name, status='passed', held=sorted(held),
                                exact_replay=True, evidence=out.name))
            (root/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print('4 independent placed-baby pickup cases passed with parity and replay')


if __name__ == '__main__':
    main()
