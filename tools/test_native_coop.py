#!/usr/bin/env python3
"""Compile/run the native co-op policy tests without a ROM or running game."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build-coop-tests'

def main():
    BUILD.mkdir(exist_ok=True)
    compiler = 'C:/msys64/mingw64/bin/gcc.exe' if os.name == 'nt' else shutil.which('cc')
    if not compiler:
        raise SystemExit('A C11 compiler is required')
    exe = BUILD / ('coop_session_test.exe' if os.name == 'nt' else 'coop_session_test')
    subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Isrc', 'src/mods/coop/coop_session.c',
                    'src/mods/coop/coop_state.c', 'src/mods/coop/coop_guest.c',
                    'test/coop/session_test.c',
                    '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], cwd=ROOT, check=True)

if __name__ == '__main__':
    main()
