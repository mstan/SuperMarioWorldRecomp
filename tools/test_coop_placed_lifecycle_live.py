#!/usr/bin/env python3
"""Carry placed-source history through actual pipes and an actual timeout/retry.

The source and approach positions are copied test fixtures. Pipes and the retry
loader execute native gameplay; no transition or success flag is supplied.
This does not establish the terrain visuals of offline camera relocation.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from test_coop_mount_transport_live import pipe
from test_coop_mouth_live import same, set_actor


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='CNR8 first hatch in staged YI2 source (64,352)')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    base = Fixture(args.source)
    assert base.placed_eggs[0][:5] == (0x106, 64, 352, 255, 1)
    results, parity = [], {}

    def run(name, source, buttons, duration, narrow, bounce):
        out = root/name
        script = root/f'{name}.script'
        script.write_text('wait 60\nloadstate 10\n'+buttons+f'wait {duration}\nsavestate 9\n'
                          'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n')
        cmd = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
               '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
               '--snapshot', str(source.resolve()), '--script', str(script),
               '--out', str(out), '--frames', str(duration+240),
               '--framedump', str(duration+160)]
        if narrow:
            cmd.append('--native')
        if bounce:
            cmd.append('--bounce')
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert not report['returncode'] and not report['timed_out'] and not report['dispatch_misses']
        f, a, b = (Fixture(out/f'saves/native-coop-v1/save{n}.sav') for n in (9, 8, 7))
        same(a, b)
        key = name.rsplit('-', 1)[0]
        if bounce:
            same(f, Fixture(parity[key]))
        else:
            parity[key] = out/'saves/native-coop-v1/save9.sav'
        return out, f

    def record(name, out):
        results.append(dict(case=name, status='passed', evidence=out.name))
        (root/'summary.json').write_text(json.dumps(dict(
            checks=results, campaign='unverified', setup='offline positions and source; retry timer staged'),
            indent=2)+'\n')

    for bounce in (False, True):
        for narrow in (True, False):
            suffix = f'{"standard" if narrow else "wide"}-{"bounce" if bounce else "native"}'
            path = root/f'enter-{suffix}.sav'
            pipe(args.source, path, 0)
            name = f'enter-{suffix}'
            out, f = run(name, path, 'press down 80\n', 160, narrow, bounce)
            assert f.level == 0x1ca and f.placed_eggs == [(0x106, 64, 352, 255, 1, 0)], out
            record(name, out)
            for p, x in ((0, 424), (1, 360)):
                set_actor(f, p, x, 352, False)
            f.camera(192)
            f.put(f.focus+24, 0)
            f.put(f.focus+28, 0)
            path = root/f'return-{suffix}.sav'
            f.save(path)
            name = f'return-{suffix}'
            out, f = run(name, path, 'press right 80\n', 160, narrow, bounce)
            assert f.level == 0x106 and f.placed_eggs[0][:5] == (0x106, 64, 352, 255, 1), out
            record(name, out)
            for p, x in ((0, 48), (1, 160)):
                set_actor(f, p, x, 352, False)
            f.camera(0)
            f.put(f.focus+24, 0)
            f.put(f.focus+28, 0)
            path = root/f'last-grant-{suffix}.sav'
            f.save(path)
            name = f'last-grant-{suffix}'
            out, f = run(name, path, '', 150, narrow, bounce)
            assert f.placed_eggs == [(0x106, 64, 352, 255, 2, 0)], out
            assert any(f.data[f.ram+0x9e+i] == 0x2d and f.data[f.ram+0x14c8+i] == 9
                       for i in range(12)), out
            record(name, out)
            f = Fixture(args.source)
            lives = f.word(f.core+36)
            f.data[f.ram+0xf30:f.ram+0xf34] = bytes((0, 0, 0, 1))
            path = root/f'retry-{suffix}.sav'
            f.save(path)
            name = f'retry-{suffix}'
            out, f = run(name, path, '', 850, narrow, bounce)
            assert f.level == 0x106 and not f.placed_eggs and f.word(f.core+36) == lives-1, out
            record(name, out)
    print(f'{len(results)} placed-source room/retry cases passed with parity and replay')


if __name__ == '__main__':
    main()
