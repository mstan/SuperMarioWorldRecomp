#!/usr/bin/env python3
"""Compile a ROM-free startup check against the pinned, real mod provider."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=ROOT / 'snesrecomp')
    parser.add_argument('--ui', type=Path, default=ROOT / 'recomp-ui')
    parser.add_argument('--mod-core', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    engine, ui = args.engine.resolve(), args.ui.resolve()
    core = (args.mod_core or engine / 'external/shared-cores/recomp-mod-core').resolve()
    cc = 'C:/msys64/mingw64/bin/gcc.exe' if os.name == 'nt' else shutil.which('cc')
    cxx = 'C:/msys64/mingw64/bin/g++.exe' if os.name == 'nt' else shutil.which('c++')
    if not cc or not cxx:
        raise SystemExit('C11 and C++17 compilers are required')
    includes = [ROOT / 'src', engine / 'runner/src', ui / 'src', core / 'include']
    flags = ['-O1', '-flto', '-ffunction-sections', '-fdata-sections', '-DRECOMP_LAUNCHER=1']
    for directory in includes:
        flags.extend(['-I', str(directory)])
    sources = [
        engine / 'runner/src/mod_runtime.cpp',
        engine / 'runner/src/crc32.c',
        engine / 'runner/src/sha256.c',
        engine / 'runner/src/sha1.c',
        engine / 'runner/src/rom_identity.c',
        engine / 'runner/src/snes/tier2_capture.c',
        core / 'src/runtime.cpp', core / 'src/rmod_sha256.c', core / 'src/rmod_crc32.c',
        ROOT / 'src/mods/smw_coop_plugin.c',
        *[ROOT / f'src/mods/coop/{name}.c' for name in
          ('coop_runtime', 'coop_machine', 'coop_session', 'coop_state', 'coop_guest')],
        ROOT / 'test/coop/mod_catalog_test.cpp',
    ]
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin;' + env.get('PATH', '')
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='smw-mod-catalog-', dir=args.out) as tmp:
        out = Path(tmp)
        # The production caller is C; the provider integration test is C++.
        for launcher in ([], ['-DRECOMP_LAUNCHER=1']):
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            *flags, '-URECOMP_LAUNCHER', *launcher,
                            '-x', 'c', '-fsyntax-only', str(ROOT / 'src/smw_mod_catalog.h')],
                           cwd=ROOT, env=env, check=True)
        objects = []
        for index, source in enumerate(sources):
            obj = out / f'{index}.o'
            is_cpp = source.suffix == '.cpp'
            command = [cxx if is_cpp else cc, '-std=c++17' if is_cpp else '-std=c11',
                       *flags]
            if source == ROOT / 'test/coop/mod_catalog_test.cpp':
                command += ['-Wall', '-Wextra', '-Werror']
            subprocess.run([*command, '-c', str(source), '-o', str(obj)],
                           cwd=ROOT, env=env, check=True)
            objects.append(str(obj))
        exe = out / ('mod_catalog_test.exe' if os.name == 'nt' else 'mod_catalog_test')
        subprocess.run([cxx, '-flto', *objects, '-Wl,--gc-sections', '-o', str(exe)],
                       cwd=ROOT, env=env, check=True)
        manifest = ROOT / 'mods/preloaded/packages' / (
            'super-mario-world.gameplay.native-coop/0.1.0/manifest.toml')
        subprocess.run([str(exe), str(out / 'profile'), str(manifest)],
                       cwd=out, env=env, check=True)


if __name__ == '__main__':
    main()
