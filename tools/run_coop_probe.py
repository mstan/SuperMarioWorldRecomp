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
import wave

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
    for source, target in ((args.normal_sram, out/'saves/save.srm'),
                           (args.coop_sram, selection/'save.srm')):
        if source:
            shutil.copy2(source, target)
    saves = selection if args.players > 1 and not args.disabled else out / 'saves'
    if args.snapshot:
        shutil.copy2(args.snapshot, saves / 'save10.sav')
    extra_states = {}
    for value in args.extra_state:
        slot_text, path_text = value.split('=', 1)
        slot, path = int(slot_text), Path(path_text).resolve()
        if not 0 <= slot < 16 or slot in extra_states or (slot == 10 and args.snapshot):
            raise ValueError('Extra snapshot slot must be unique and between 0 and 15')
        shutil.copy2(path, saves/f'save{slot}.sav')
        extra_states[slot] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
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
               SMW_COOP_ARBITRATION_TRACE=str(out / 'arbitration.csv'),
               SMW_RENDER_DIAGNOSTICS=str(out),
               SMW_RENDER_CAPTURE_FRAME=args.capture,
               SNESRECOMP_AUDIO_STATS=str(out / 'audio-stats.txt'),
               SNESRECOMP_LLE_BOUNCE=str(int(args.bounce)))
    if args.turbo_burst:
        env['SNESRECOMP_TURBO_BURST'] = args.turbo_burst
    if args.audio_frames:
        env['SMW_AUDIO_FRAME_TRACE'] = str(out / 'audio-frames.csv')
    if args.audit_queries:
        env['SMW_COOP_QUERY_AUDIT'] = '1'
    if args.storage_trace:
        env['SMW_COOP_STORAGE_TRACE'] = str(out / 'storage.csv')
    if args.projectile_trace:
        env['SMW_COOP_PROJECTILE_TRACE'] = str(out / 'projectiles.csv')
    if args.extended_trace:
        env['SMW_COOP_EXTENDED_TRACE'] = str(out / 'extended.csv')
    if args.mouth_trace:
        env['SMW_COOP_MOUTH_TRACE'] = str(out / 'mouths.csv')
    if args.wram_watch:
        lo, hi = (int(value, 16) for value in args.wram_watch.split(':'))
        if not 0 <= lo <= hi <= 0xffff:
            raise ValueError('WRAM watch must be an inclusive 16-bit hex range')
        env['SNESRECOMP_WLOG_ADDR'] = f'{lo:x}:{hi:x}:{out / "wram-writes.log"}'
        env['SNESRECOMP_WLOG_STATE'] = '1'
    if args.audio_capture:
        env['SMW_AUDIO_OUTPUT_PCM'] = str(out / 'output.pcm')
    if args.minimal_trace:
        for key in ('SMW_COOP_TRACE', 'SMW_COOP_CONTACT_TRACE', 'SMW_COOP_ARBITRATION_TRACE', 'SMW_RENDER_DIAGNOSTICS',
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
                  'timeout_seconds': args.timeout_seconds,
                  'native': args.native, 'bounce': args.bounce,
                  'disabled': args.disabled, 'players': args.players,
                  'minimal_trace': args.minimal_trace, 'audio_frames': args.audio_frames,
                  'audio_capture': args.audio_capture, 'audit_queries': args.audit_queries,
                  'wram_watch': args.wram_watch, 'storage_trace': args.storage_trace,
                  'projectile_trace': args.projectile_trace,
                  'extended_trace': args.extended_trace, 'mouth_trace': args.mouth_trace,
                  'extra_states': extra_states,
                  'inputs': {}}
    for name in ('exe', 'rom', 'snapshot', 'script', 'normal_sram', 'coop_sram'):
        path = getattr(args, name)
        if path:
            provenance['inputs'][name] = {
                'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (out / 'inputs.json').write_text(json.dumps(provenance, indent=2) + '\n')
    with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
        try:
            result = subprocess.run(
                command, cwd=out, env=env, stdout=stdout, stderr=stderr,
                timeout=args.timeout_seconds or max(120, args.frames / 30),
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            returncode, timed_out = result.returncode, False
        except subprocess.TimeoutExpired:
            returncode, timed_out = 124, True
    report = {'returncode': returncode, 'timed_out': timed_out, 'dispatch_misses': []}
    for directory in (out, args.exe.parent, args.exe.parent / 'playtest'):
        misses = directory / 'dispatch_misses.log'
        if misses.exists() and misses.stat().st_size:
            report['dispatch_misses'].append(str(misses))
    for line in (out / 'stdout.log').read_text().splitlines():
        if line.startswith('SNESRECOMP_BENCHMARK {'):
            report['benchmark'] = json.loads(line.split(' ', 1)[1])
    if args.audio_capture and (out / 'output.pcm').exists():
        pcm = (out / 'output.pcm').read_bytes()
        if len(pcm) % 4:
            raise ValueError('Incomplete stereo PCM capture')
        with wave.open(str(out / 'output.wav'), 'wb') as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(32040)
            wav.writeframes(pcm)
        report['captured_output_frames'] = len(pcm) // 4
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)
    return returncode or int(bool(report['dispatch_misses']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'rom', 'script', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--extra-state', action='append', default=[], metavar='SLOT=PATH',
                        help='Copy another snapshot to an isolated numbered slot')
    parser.add_argument('--normal-sram', type=Path, help='Copy normal SRAM into the isolated test directory')
    parser.add_argument('--coop-sram', type=Path, help='Copy co-op SRAM into the isolated test directory')
    parser.add_argument('--frames', type=int, default=800)
    parser.add_argument('--timeout-seconds', type=float,
                        help='Explicit positive process deadline for a busy validation host')
    parser.add_argument('--capture', default='')
    parser.add_argument('--framedump', type=int, help='Capture one completed frame, including native width')
    parser.add_argument('--players', type=int, choices=(1, 2), default=2)
    parser.add_argument('--audio-paced', action='store_true')
    parser.add_argument('--audio-frames', action='store_true',
                        help='Record audio counters around input/state load and guest execution')
    parser.add_argument('--audit-queries', action='store_true',
                        help='Reject unexpected WRAM changes during native contact queries')
    parser.add_argument('--storage-trace', action='store_true',
                        help='Record buffered namespace-selection I/O phase timings')
    parser.add_argument('--projectile-trace', action='store_true',
                        help='Record buffered accepted/rejected native fire contacts')
    parser.add_argument('--extended-trace', action='store_true',
                        help='Record buffered per-actor extended hazard/cloud-coin contacts')
    parser.add_argument('--mouth-trace', action='store_true',
                        help='Record buffered tongue candidates and committed claims')
    parser.add_argument('--wram-watch', help='Passive guest write log, inclusive hex LO:HI')
    parser.add_argument('--audio-capture', action='store_true',
                        help='Capture muted device output to a stereo WAV (audio-paced only)')
    parser.add_argument('--minimal-trace', action='store_true',
                        help='Retain benchmark/audio counters without per-frame CSV/captures')
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--disabled', action='store_true')
    parser.add_argument('--bounce', action='store_true')
    parser.add_argument('--turbo-burst', help='Process-local start_frame,count')
    args = parser.parse_args()
    if args.timeout_seconds is not None and args.timeout_seconds <= 0:
        parser.error('--timeout-seconds must be positive')
    if args.audio_capture and not args.audio_paced:
        parser.error('--audio-capture requires --audio-paced')
    for name in ('exe', 'rom', 'snapshot', 'script', 'out', 'normal_sram', 'coop_sram'):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    if args.turbo_burst:
        start, count = map(int, args.turbo_burst.split(','))
        if start < 0 or count <= 0 or start + count >= args.frames:
            parser.error('Turbo range must be within the run')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
