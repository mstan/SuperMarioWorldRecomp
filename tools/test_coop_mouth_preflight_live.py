#!/usr/bin/env python3
"""Validate atomic mouth-state rejection in a running isolated, muted guest.

The source must be a two-mount snapshot with one retained mouth object.
Malformed copies are explicit negative fixtures; no owner files are changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--legacy-bad', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    parser.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source = args.source.resolve()
    valid = Fixture(source)
    mouths = [(e[2], valid.data[valid.ram+0x160e+e[2]])
              for e in valid.entities.values() if e[3] == 0x35]
    assert len(mouths) == 2
    filled = next((slot, obj) for slot, obj in mouths if obj < 12)
    empty = next(slot for slot, obj in mouths if obj >= 128)
    variants = {}
    for kind, value in (('duplicate', filled[1]), ('self', empty), ('range', 12)):
        f = Fixture(source)
        f.data[f.ram+0x160e+empty] = value
        variants[kind] = root/f'{kind}.sav'
        f.save(variants[kind])
    if args.legacy_bad:
        variants['legacy-duplicate'] = args.legacy_bad.resolve()
    script = root/'probe.script'
    script.write_text('wait 60\nloadstate 10\nwait 12\nloadstate 9\nwait 12\nsavestate 8\nwait 5\n')
    results = []
    for bounce in (False, True):
        reference = None
        for kind, bad in [('missing-slot-control', None), *variants.items()]:
            out = root/f'{kind}-{"bounce" if bounce else "native"}'
            command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                       '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                       '--snapshot', str(source), '--script', str(script), '--out', str(out),
                       '--frames', '105', '--native', '--framedump', '95']
            if bad:
                command += ['--extra-state', f'9={bad}']
            if bounce:
                command.append('--bounce')
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            result = json.loads((out/'result.json').read_text())
            assert not result['returncode'] and not result['timed_out'] and not result['dispatch_misses']
            log = (out/'stderr.log').read_text()
            assert log.count('[co-op] restored 2 players with native actor state') == 1, (out, log)
            saved = (out/'saves/native-coop-v1/save8.sav').read_bytes()
            if reference is None:
                reference = saved
            else:
                assert saved == reference, f'{out}: rejected state changed running guest'
                logs = log+(out/'stdout.log').read_text()
                assert 'preflight' in logs.lower(), out
            results.append(dict(case=kind, bounce=bounce, status='passed',
                                snapshot_sha256=hashlib.sha256(saved).hexdigest(),
                                evidence=out.name))
            (root/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print(f'{len(results)} valid-load/atomic-rejection checks passed')


if __name__ == '__main__':
    main()
