#!/usr/bin/env python3
"""Test native menu selection against sentinel SRAM and failed writes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = 'C:/msys64/mingw64/bin/gcc.exe' if os.name == 'nt' else shutil.which('cc')
    with tempfile.TemporaryDirectory(prefix='smw-coop-storage-') as tmp:
        out = Path(tmp)
        exe = out / ('storage.exe' if os.name == 'nt' else 'storage')
        subprocess.run([
            compiler, '-std=c11', '-O2', '-flto', '-ffunction-sections', '-fdata-sections',
            '-Isrc', '-Isnesrecomp/runner/src',
            'src/mods/coop/coop_runtime.c', 'src/mods/coop/coop_session.c',
            'src/mods/coop/coop_state.c', 'src/mods/coop/coop_guest.c',
            'src/mods/coop/coop_machine.c', 'test/coop/storage_selection_test.c',
            '-Wl,--gc-sections', '-o', str(exe)], cwd=ROOT, check=True)
        saves = out / 'saves/native-coop-v1'
        saves.mkdir(parents=True)
        (saves / 'last-mode.txt').write_text('2')
        subprocess.run([str(exe)], cwd=out, check=True)


if __name__ == '__main__':
    main()
