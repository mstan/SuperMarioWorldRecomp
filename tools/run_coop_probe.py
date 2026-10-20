#!/usr/bin/env python3
"""Run a finite SMW probe with isolated settings/saves and muted output.

An optional snapshot is copied to slot 10. The supplied script must actually
load it. Audio-paced mode retains audio emulation and uses a dummy output
device; throughput mode is not audio qualification. No runtime pauses.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    mods = out / 'mods/preloaded'
    shutil.copytree(ROOT / 'mods/preloaded/packages', mods / 'packages')
    state = 'format_version = 1\n'
    for package, feature, version, enabled in [
        ('super-mario-world.enhancement.widescreen', 'widescreen', '1.0.0', not args.native),
        ('super-mario-world.gameplay.native-coop', 'simultaneous', '0.1.0', not args.disabled),
    ]:
        state += (f'[[package]]\nid = "{package}"\nversion = "{version}"\n'
                  f'[[feature]]\npackage_id = "{package}"\nid = "{feature}"\n'
                  f'enabled = {str(enabled).lower()}\n')
        if feature == 'widescreen':
            state += '[feature.values]\nmode = "adaptive"\nspawn = "adaptive"\n'
    (mods / 'state.toml').write_text(state, encoding='utf-8')
    config = out / 'config.ini'
    config.write_text(f'''[General]
Autosave=0
SkipLauncher=1
[Graphics]
WindowSize=1280x548
FullScreen=0
NewRenderer=1
NoSpriteLimits=1
OutputMethod=SDL-Software
[Sound]
EnableAudio={int(args.audio_paced)}
AudioFreq=32040
[GamepadMap]
EnableGamepad1=false
EnableGamepad2=false
KeyboardPlayers=3
''', encoding='utf-8')
    selection = out / 'saves/native-coop-v1'
    selection.mkdir(parents=True)
    (selection / 'last-mode.txt').write_text(str(args.players), encoding='ascii')
    saves = selection if args.players > 1 and not args.disabled else out / 'saves'
    if args.snapshot:
        shutil.copy2(args.snapshot, saves / 'save10.sav')
    script = out / 'input.script'
    shutil.copy2(args.script, script)
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(('SMW_', 'SNESRECOMP_', 'SDL_')):
            del env[key]
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin' + os.pathsep + env.get('PATH', '')
    env.update(SDL_VIDEODRIVER='dummy', SDL_VIDEO_DRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SMW_COOP_TRACE=str(out / 'coop.csv'),
               SMW_COOP_CONTACT_TRACE=str(out / 'contacts.csv'),
               SMW_RENDER_DIAGNOSTICS=str(out),
               SMW_RENDER_CAPTURE_FRAME=args.capture,
               SNESRECOMP_AUDIO_STATS=str(out / 'audio-stats.txt'),
               SNESRECOMP_LLE_BOUNCE=str(int(args.bounce)))
    if args.turbo_burst:
        env['SNESRECOMP_TURBO_BURST'] = args.turbo_burst
    if args.minimal_trace:
        for key in ('SMW_COOP_TRACE', 'SMW_COOP_CONTACT_TRACE', 'SMW_RENDER_DIAGNOSTICS',
                    'SMW_RENDER_CAPTURE_FRAME'):
            env.pop(key, None)
    command = [str(args.exe), '--config', str(config), str(args.rom),
               '--script', str(script),
               '--benchmark-audio-paced' if args.audio_paced else '--benchmark',
               str(args.frames)]
    if args.framedump is not None:
        command += ['--framedump', str(out / 'frames')]
        env['SNESRECOMP_FRAMEDUMP_START'] = str(args.framedump)
        env['SNESRECOMP_FRAMEDUMP_END'] = str(args.framedump)
        env['SNESRECOMP_FRAMEDUMP_PIXELS'] = '1'
    provenance = {'command': command, 'turbo_burst': args.turbo_burst,
                  'minimal_trace': args.minimal_trace, 'inputs': {}}
    for name in ('exe', 'rom', 'snapshot', 'script'):
        path = getattr(args, name)
        if path:
            provenance['inputs'][name] = {
                'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (out / 'inputs.json').write_text(json.dumps(provenance, indent=2) + '\n')
    with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
        result = subprocess.run(
            command, cwd=out, env=env, stdout=stdout, stderr=stderr,
            timeout=max(120, args.frames / 30),
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    report = {'returncode': result.returncode, 'dispatch_misses': []}
    for directory in (out, args.exe.parent, args.exe.parent / 'playtest'):
        misses = directory / 'dispatch_misses.log'
        if misses.exists() and misses.stat().st_size:
            report['dispatch_misses'].append(str(misses))
    for line in (out / 'stdout.log').read_text().splitlines():
        if line.startswith('SNESRECOMP_BENCHMARK {'):
            report['benchmark'] = json.loads(line.split(' ', 1)[1])
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)
    return result.returncode or int(bool(report['dispatch_misses']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'script', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--frames', type=int, default=800)
    parser.add_argument('--capture', default='')
    parser.add_argument('--framedump', type=int, help='Capture one completed frame, including native width')
    parser.add_argument('--players', type=int, choices=(1, 2), default=2)
    parser.add_argument('--audio-paced', action='store_true')
    parser.add_argument('--minimal-trace', action='store_true',
                        help='Retain benchmark/audio counters without per-frame CSV/captures')
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--disabled', action='store_true')
    parser.add_argument('--bounce', action='store_true')
    parser.add_argument('--turbo-burst', help='Process-local start_frame,count')
    args = parser.parse_args()
    for name in ('exe', 'rom', 'snapshot', 'script', 'out'):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    if args.turbo_burst:
        start, count = map(int, args.turbo_burst.split(','))
        if start < 0 or count <= 0 or start + count >= args.frames:
            parser.error('Turbo range must be within the run')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
