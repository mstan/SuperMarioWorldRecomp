#!/usr/bin/env python3
"""Optional ROM-backed test of a built SMW recorder and independent checkpoints.

Use a native executable from a build or extracted package. All writes and saves
stay in a new --out directory. Without private assets this reports unavailable.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path)
    parser.add_argument('--rom',type=Path)
    parser.add_argument('--out',type=Path)
    parser.add_argument('--frames',type=int,default=900)
    args=parser.parse_args()
    if not args.exe or not args.rom:
        print('UNAVAILABLE: supply a built SMW --exe and private USA --rom.');return 0
    if not args.out or args.frames<900:parser.error('use a new --out and at least 900 frames')
    exe=args.exe.resolve(strict=True);rom=args.rom.resolve(strict=True)
    if b'Non-interactive ROM selection requires' not in exe.read_bytes():
        raise RuntimeError('refusing to launch a build without the native-dialog guard')
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(exe.parent/'mods',out/'mods');(out/'logs').mkdir()
    state='format_version=1\n'
    for package,feature in [('super-mario-world.enhancement.widescreen','widescreen'),
                            ('super-mario-world.gameplay.native-coop','simultaneous'),
                            ('super-mario-world.developer.diagnostics','recording')]:
        state+=f'[[feature]]\npackage_id="{package}"\nid="{feature}"\nenabled=true\n'
        if feature=='widescreen':state+='[feature.values]\nmode="adaptive"\nspawn="adaptive"\n'
    (out/'mods/preloaded/state.toml').write_text(state,encoding='utf-8')
    saves=out/'saves/native-coop-v1';saves.mkdir(parents=True)
    (saves/'last-mode.txt').write_text('2',encoding='ascii')
    config=out/'config.ini'
    config.write_text('[General]\nAutosave=0\nSkipLauncher=1\n[Graphics]\nFullscreen=0\nWindowSize=1280x720\nOutputMethod=SDL\nNewRenderer=1\nNoSpriteLimits=1\n[Sound]\nEnableAudio=1\nAudioFreq=32040\n[GamepadMap]\nEnableGamepad1=false\nEnableGamepad2=false\nKeyboardPlayers=3\n',encoding='utf-8')
    script=out/'input.script'
    script.write_text('wait 240\npress start 2\nwait 100\npress down 1\nwait 4\npress start 2\nwait 180\npress start 2\nwait 180\npress b 2\nwait 180\npress right 70\nwait 60\npress b 2\nwait 370\npress b 2\nwait 300\npress right 60\nwait 100\npress b 2\nwait 200\n',encoding='ascii')
    env={k:v for k,v in os.environ.items() if not k.startswith(('SMW_','SNESRECOMP_','SDL_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_VIDEO_DRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SMW_REPLAY_RECORD='0',SMW_REPLAY_DIR=str(out/'logs'))
    env.update(SNESRECOMP_NO_DIALOGS='1',SNESRECOMP_NO_LAUNCHER='1',SNES_NET_SUPPRESS_ERROR_DIALOG='1',DISPLAY='',WAYLAND_DISPLAY='',WAYLAND_SOCKET='',QT_QPA_PLATFORM='offscreen',DBUS_SESSION_BUS_ADDRESS='unix:path=/tmp/smw-no-desktop-bus')
    argv=[str(exe),str(rom),'--config',str(config),'--script',str(script),'--benchmark-audio-paced',str(args.frames)]
    with (out/'capture.log').open('wb') as log:
        result=subprocess.run(argv,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=max(60,args.frames/20))
    if result.returncode:raise RuntimeError(f'capture failed; see {out/"capture.log"}')
    captures=sorted((out/'logs').glob('*.replay'))
    if len(captures)<2:raise RuntimeError('test did not cover an independent mid-session checkpoint')
    helper=Path(__file__).with_name('replay_diagnostic.py')
    for index,capture in enumerate(captures):
        argv=[sys.executable,str(helper),str(capture),'--exe',str(exe),'--rom',str(rom),'--headless','--work-dir',str(out/f'replay-{index}')]
        with (out/f'replay-{index}.log').open('wb') as log:
            result=subprocess.run(argv,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        if result.returncode:raise RuntimeError(f'independent replay failed; see {out/f"replay-{index}.log"}')
    print(f'PASS: {len(captures)} independent checkpoints replayed with audio enabled.');return 0


if __name__=='__main__':
    raise SystemExit(main())
