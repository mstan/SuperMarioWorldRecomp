#!/usr/bin/env python3
"""Replay real native contacts in isolated, muted game sessions.

Requires a local original ROM and a copied CNR5 YI2 entrance snapshot. Staged
coordinates are test inputs, not campaign or terrain-streaming evidence.
Every case retains its fixture, input, trace, screenshots, settings and logs.
The output directory must be new; no owner settings or saves are modified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from check_coop_contacts import check
from make_coop_contact_fixture import make

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_execution(out):
    results = []
    for floor in sorted(out.glob('floor-*')):
        bounce = out / floor.name.replace('floor-', 'bounce-', 1)
        for name in ('contacts.csv', 'coop.csv', 'frame-000070.bmp', 'frame-000080.bmp'):
            a, b = floor / name, bounce / name
            same = a.is_file() and b.is_file() and sha256(a) == sha256(b)
            results.append({'case': floor.name[6:], 'file': name,
                            'status': 'passed' if same else 'failed'})
    (out / 'parity.json').write_text(json.dumps(results, indent=2) + '\n')
    passed = bool(results) and all(r['status'] == 'passed' for r in results)
    print(f'Execution parity: {sum(r["status"] == "passed" for r in results)}/{len(results)} files',
          flush=True)
    return passed


def run(args, actor, kind, camera, bounce):
    name = f'{"bounce" if bounce else "floor"}-p{actor}-{kind}-{camera}'
    out = args.out / name
    out.mkdir()
    mods = out / 'mods/preloaded'
    shutil.copytree(args.catalog, mods / 'packages')
    (mods / 'state.toml').write_text('''format_version = 1
[[package]]
id = "super-mario-world.enhancement.widescreen"
version = "1.0.0"
[[package]]
id = "super-mario-world.gameplay.native-coop"
version = "0.1.0"
[[feature]]
package_id = "super-mario-world.enhancement.widescreen"
id = "widescreen"
enabled = true
[feature.values]
mode = "adaptive"
spawn = "adaptive"
[[feature]]
package_id = "super-mario-world.gameplay.native-coop"
id = "simultaneous"
enabled = true
''', encoding='utf-8')
    config = out / 'config.ini'
    config.write_text('''[General]
Autosave=0
SkipLauncher=1
[Graphics]
WindowSize=1280x548
FullScreen=0
NewRenderer=1
NoSpriteLimits=1
OutputMethod=SDL-Software
[Sound]
EnableAudio=0
[GamepadMap]
EnableGamepad1=false
EnableGamepad2=false
KeyboardPlayers=3
''', encoding='utf-8')
    saves = out / 'saves/native-coop-v1'
    saves.mkdir(parents=True)
    (saves / 'last-mode.txt').write_text('2', encoding='ascii')
    fixture = out / 'fixture.sav'
    make(args.source, fixture, kind, actor, camera)
    shutil.copy2(fixture, saves / 'save10.sav')
    script = out / 'input.script'
    movement = ('wait 60' if kind == 'stomp' else
                f'press p{actor + 1}.right'
                f'{f"+p{actor + 1}.y" if kind == "pickup" else ""} 60')
    script.write_text(f'wait 60\nloadstate 10\n{movement}\nwait 20\n', encoding='ascii')
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(('SMW_', 'SNESRECOMP_', 'SDL_')):
            del env[key]
    if os.name == 'nt':
        env['PATH'] = str(args.runtime_bin) + os.pathsep + env.get('PATH', '')
    env.update(SDL_VIDEODRIVER='dummy', SDL_VIDEO_DRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SMW_COOP_TRACE=str(out / 'coop.csv'),
               SMW_COOP_CONTACT_TRACE=str(out / 'contacts.csv'),
               SMW_RENDER_DIAGNOSTICS=str(out),
               SMW_RENDER_CAPTURE_FRAME='70,80',
               SNESRECOMP_LLE_BOUNCE=str(bounce))
    command = [str(args.exe), '--config', str(config), str(args.rom),
               '--script', str(script), '--benchmark', '150']
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
        completed = subprocess.run(
            command, cwd=out, env=env, stdout=stdout, stderr=stderr,
            timeout=120, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    result = dict(case=name, returncode=completed.returncode, status='failed')
    try:
        assert completed.returncode == 0, f'Game exited with {completed.returncode}'
        for directory in (out, args.exe.parent, args.exe.parent / 'playtest'):
            misses = directory / 'dispatch_misses.log'
            assert not misses.exists() or not misses.stat().st_size, f'Dispatch misses: {misses}'
        region = {0: 'right', 192: 'center', 384: 'left'}[camera]
        contact = check(out / 'contacts.csv', kind, actor, region)
        if 'status' in contact:
            contact['sprite_status'] = contact.pop('status')
        result.update(contact)
        assert (out / 'frame-000080.bmp').is_file(), 'Missing rendered evidence'
        result['status'] = 'passed'
    except (AssertionError, OSError, ValueError) as error:
        result['error'] = str(error)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'source', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--catalog', type=Path, default=ROOT / 'mods/preloaded/packages')
    parser.add_argument('--runtime-bin', type=Path, default=Path('C:/msys64/mingw64/bin'))
    parser.add_argument('--actors', type=int, nargs='+', choices=(0, 1), default=[0, 1])
    parser.add_argument('--kinds', nargs='+', choices=('stomp', 'kick', 'pickup', 'side'),
                        default=['stomp', 'kick', 'pickup', 'side'])
    parser.add_argument('--cameras', type=int, nargs='+', choices=(0, 192, 384),
                        default=[0, 192, 384])
    parser.add_argument('--execution', choices=('floor', 'bounce', 'both'), default='both')
    args = parser.parse_args()
    for name in ('exe', 'rom', 'source', 'out', 'catalog', 'runtime_bin'):
        setattr(args, name, getattr(args, name).resolve())
    for name in ('exe', 'rom', 'source'):
        assert getattr(args, name).is_file(), f'Missing {name}'
    args.out.mkdir(parents=True, exist_ok=False)
    provenance = {name: {'path': str(getattr(args, name)), 'sha256': sha256(getattr(args, name))}
                  for name in ('exe', 'rom', 'source')}
    (args.out / 'inputs.json').write_text(json.dumps(provenance, indent=2) + '\n')
    modes = [0, 1] if args.execution == 'both' else [int(args.execution == 'bounce')]
    results = []
    for bounce in modes:
        for actor in args.actors:
            for kind in args.kinds:
                for camera in args.cameras:
                    results.append(run(args, actor, kind, camera, bounce))
                    (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    parity_ok = compare_execution(args.out) if args.execution == 'both' else True
    return int(not parity_ok or any(r['status'] != 'passed' for r in results))


if __name__ == '__main__':
    sys.exit(main())
