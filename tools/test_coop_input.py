#!/usr/bin/env python3
"""Test real SMW input assignment configuration without opening a window."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    build = ROOT / 'build-coop-tests'
    build.mkdir(exist_ok=True)
    if os.name == 'nt':
        cc = 'C:/msys64/mingw64/bin/gcc.exe'
        flags = ['-I', 'C:/msys64/mingw64/include/SDL3',
                 '-L', 'C:/msys64/mingw64/lib', '-lSDL3']
    else:
        cc = shutil.which('cc')
        flags = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
    exe = build / ('input_config_test.exe' if os.name == 'nt' else 'input_config_test')
    subprocess.run([cc, '-std=c11', '-DSNESRECOMP_SDL3=1', '-DSDL_MAIN_HANDLED',
                    '-ffunction-sections', '-fdata-sections', '-Isrc', '-Isnesrecomp/runner/src',
                    'test/coop/input_config_test.c', 'src/config.c',
                    'snesrecomp/runner/src/util.c', '-Wl,--gc-sections', *flags,
                    '-o', str(exe)], cwd=ROOT, check=True)
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin;' + env.get('PATH', '')
    subprocess.run([str(exe), str(build / 'input-config.ini')], cwd=ROOT, env=env, check=True)

if __name__ == '__main__':
    main()
