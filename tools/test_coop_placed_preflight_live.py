#!/usr/bin/env python3
"""Reject inconsistent placed-egg records before changing a running guest."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='CNR8 state during the first placed egg hatch')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    valid = Fixture(args.source)
    assert len(valid.placed_eggs) == 1 and valid.placed_eggs[0][4] == 1
    slot = valid.entities[valid.placed_eggs[0][5]][2]
    at = valid.placed_at+8
    variants = {}
    fields = [('type', valid.ram+0x9e+slot, 0x2d, False),
              ('status', valid.ram+0x14c8+slot, 0, False),
              ('position', valid.ram+0xe4+slot, 1, False),
              ('contents', valid.ram+0x151c+slot, 0x35, False),
              ('unused-hatch', at+16, 0, True),
              ('excess-grants', at+16, 3, True),
              ('unknown-egg', at+20, valid.next_entity+100, True),
              ('wrong-room', at, valid.level ^ 1, True)]
    for name, address, value, word in fields:
        f = Fixture(args.source)
        if word:
            f.put(address, value)
        else:
            f.data[address] = value
        path = root/f'{name}.sav'
        f.save(path)
        variants[name] = path
    script = root/'probe.script'
    script.write_text('wait 60\nloadstate 10\nwait 12\nloadstate 9\n'
                      'wait 12\nsavestate 8\nwait 5\n')
    results = []
    for bounce in (False, True):
        control = None
        for name, bad in [('missing-slot-control', None), *variants.items()]:
            out = root/f'{name}-{"bounce" if bounce else "native"}'
            cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(args.source.resolve()), '--script', str(script),
                   '--out', str(out), '--frames', '105', '--native', '--framedump', '95']
            if bounce:
                cmd.append('--bounce')
            if bad:
                cmd += ['--extra-state', f'9={bad}']
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            report = json.loads((out/'result.json').read_text())
            assert not report['returncode'] and not report['timed_out'] and not report['dispatch_misses']
            log = (out/'stderr.log').read_text()
            assert log.count('[co-op] restored 2 players with native actor state') == 1, (out, log)
            saved = (out/'saves/native-coop-v1/save8.sav').read_bytes()
            if control is None:
                control = saved
            else:
                assert saved == control, f'{out}: rejected state changed running guest'
                assert 'preflight' in (log+(out/'stdout.log').read_text()).lower(), out
            results.append(dict(case=name, bounce=bounce, status='passed', evidence=out.name,
                                snapshot_sha256=hashlib.sha256(saved).hexdigest()))
            (root/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print(f'{len(results)} placed-egg valid-load/atomic-rejection checks passed')


if __name__ == '__main__':
    main()
