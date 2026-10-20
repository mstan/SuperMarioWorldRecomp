#!/usr/bin/env python3
"""Run the actual SDL SRAM worker with a gated/failing storage device."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl', type=int, choices=(2, 3), default=3)
    args = parser.parse_args()
    windows = os.name == 'nt'
    compiler = 'C:/msys64/mingw64/bin/gcc.exe' if windows else shutil.which('cc')
    flags = ([f'-IC:/msys64/mingw64/include/SDL{args.sdl}', f'-lSDL{args.sdl}'] if windows else
             shlex.split(subprocess.check_output(
                 ['pkg-config', '--cflags', '--libs', f'sdl{args.sdl}'], text=True)))
    with tempfile.TemporaryDirectory(prefix='smw-coop-storage-queue-') as tmp:
        exe = Path(tmp) / ('queue.exe' if windows else 'queue')
        subprocess.run([
            compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            f'-DSNESRECOMP_SDL3={int(args.sdl == 3)}', '-DSDL_MAIN_HANDLED', '-Isrc',
            '-Isnesrecomp/runner/src', 'src/mods/coop/coop_storage.c',
            'test/coop/storage_queue_test.c', *flags, '-o', str(exe)],
            cwd=ROOT, check=True)
        env = os.environ.copy()
        if windows:
            env['PATH'] = 'C:/msys64/mingw64/bin' + os.pathsep + env.get('PATH', '')
        subprocess.run([str(exe)], cwd=tmp, env=env, check=True, timeout=15)


if __name__ == '__main__':
    main()
