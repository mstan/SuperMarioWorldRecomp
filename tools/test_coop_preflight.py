#!/usr/bin/env python3
"""Check real snapshot preflight against a copied local v9/CNR5 YI2 state."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    compiler = 'C:/msys64/mingw64/bin/gcc.exe' if os.name == 'nt' else shutil.which('cc')
    with tempfile.TemporaryDirectory(prefix='smw-coop-preflight-') as tmp:
        out = Path(tmp)
        exe = out / ('preflight.exe' if os.name == 'nt' else 'preflight')
        subprocess.run([
            compiler, '-std=c11', '-O2', '-flto', '-ffunction-sections', '-fdata-sections',
            '-Isrc', '-Isnesrecomp/runner/src',
            'src/mods/coop/coop_runtime.c', 'src/mods/coop/coop_session.c',
            'src/mods/coop/coop_state.c', 'src/mods/coop/coop_guest.c',
            'src/mods/coop/coop_machine.c', 'test/coop/snapshot_preflight_test.c',
            '-Wl,--gc-sections', '-o', str(exe)], cwd=ROOT, check=True)
        saves = out / 'saves/native-coop-v1'
        saves.mkdir(parents=True)
        (saves / 'last-mode.txt').write_text('2')
        subprocess.run([str(exe), str(source)], cwd=out, check=True)


if __name__ == '__main__':
    main()
