#!/usr/bin/env python3
"""SMW renderer checks; live runs use isolated settings/saves and never pause.

--live runs a natural-input route to Yoshi's Island 2, defaulting to Fit at 100:9.
--resize changes only the child game's window after gameplay starts (Windows).
The BMPs are evidence for visual review, not a full-game acceptance oracle.
"""
import argparse
import csv
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
ROUTE = """wait 240
press start 5
wait 120
press start 5
wait 120
press start 5
wait 1400
press b 10
wait 300
press right 60
wait 120
press b 10
wait 200
press right 200
wait 5
press b 30
wait 30
press right 200
"""

def environment():
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(('SMW_RENDER_', 'SMW_ENEMY_', 'LNG_', 'SNESRECOMP_FORCE_', 'SNESRECOMP_TURBO_')):
            env.pop(key)
    if Path('C:/msys64/mingw64/bin').is_dir():
        env['PATH'] = 'C:/msys64/mingw64/bin;' + env['PATH']
    # Release runtimes leave execution coverage off unless explicitly asked.
    env['SNESRECOMP_TIER2_CAPTURE'] = '1'
    return env

def rows(path):
    if not path.exists(): return []
    return [r for r in csv.DictReader(path.open()) if r.get('unexplained_differences') is not None]

def resize_owned(process, root):
    user = c.WinDLL('user32', use_last_error=True)
    user.SetProcessDPIAware()
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.EnumWindows.argtypes = [callback, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.SetWindowPos.argtypes = [w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
    windows=[]
    @callback
    def visit(hwnd,_):
        pid=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(pid))
        if pid.value==process.pid and user.IsWindowVisible(hwnd):windows.append(hwnd)
        return True
    deadline=time.monotonic()+100
    while time.monotonic()<deadline:
        if process.poll() is not None:raise RuntimeError('game exited before resize')
        latest=rows(root/'frames.csv')
        if latest and int(latest[-1]['frame'])>=2725:break
        time.sleep(.2)
    else:raise TimeoutError('gameplay not reached')
    user.EnumWindows(visit,0)
    if not windows:raise RuntimeError('child window not found')
    hwnd=windows[0];samples=[]
    for width,height in ((960,540),(1280,600),(1600,450),(2000,180),(1440,540)):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value!=process.pid:raise RuntimeError('window ownership changed')
        outer,client=w.RECT(),w.RECT();user.GetWindowRect(hwnd,c.byref(outer));user.GetClientRect(hwnd,c.byref(client))
        if not user.SetWindowPos(hwnd,None,20,40,width+outer.right-outer.left-client.right,
                                 height+outer.bottom-outer.top-client.bottom,0x0014):raise c.WinError(c.get_last_error())
        expected=2*int(96*max(4/3,width/height)+.5)
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            latest=rows(root/'frames.csv')
            if latest and int(latest[-1]['width'])==expected:break
            time.sleep(.05)
        else:raise RuntimeError(f'no {expected}px surface after resize')
        time.sleep(.7)
        samples.append(dict(window=[width,height],render_width=expected))
        print(f'Live resize {width}x{height}: {expected} pixels',flush=True)
    return samples

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build',type=Path,default=ROOT/'build-adaptive')
    ap.add_argument('--live',action='store_true')
    ap.add_argument('--full-copy',action='store_true',help='Build the full-copy raster reference checks')
    ap.add_argument('--byte-compare',action='store_true',help='Check the portable path without write tracking')
    ap.add_argument('--resize',action='store_true')
    ap.add_argument('--aspect',default='Fit')
    ap.add_argument('--spawn',choices=['adaptive','original'],default='adaptive')
    ap.add_argument('--output',choices=['SDL','OpenGL'],default='SDL')
    ap.add_argument('--window',default='2000x180')
    ap.add_argument('--audio',action='store_true')
    ap.add_argument('--scenario',choices=['route','standing','yoshi','pipes'],default='route')
    ap.add_argument('--state',type=Path,help='local save for the yoshi (F1) or pipes (F2) scenario')
    ap.add_argument('--rom',type=Path,default=ROOT/'smw.sfc')
    args=ap.parse_args();build=args.build.resolve();build.mkdir(parents=True,exist_ok=True)
    env=environment();gcc=shutil.which('gcc',path=env['PATH'])
    if not gcc:ap.error('gcc required for the standalone checks')
    # The co-op renderer calls the shared extra-object compositor. Link that
    # real implementation and discard unused PPU entry points/dependencies.
    ppu_object=build/'ppu_compositor.o'
    subprocess.run([gcc,'-std=c11','-O2','-ffunction-sections','-fdata-sections',
        '-fno-asynchronous-unwind-tables','-fno-unwind-tables',
        '-DSNESRECOMP_TRACE=0','-Isnesrecomp/runner/src','-c',
        'snesrecomp/runner/src/snes/ppu.c','-o',str(ppu_object)],cwd=ROOT,env=env,check=True)
    test=build/(('renderer_test_full_copy' if args.full_copy else 'renderer_test')+('.exe' if os.name=='nt' else ''))
    subprocess.run([gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-DSNESRECOMP_TRACE=0',
        '-DSMW_RASTER_FULL_COPY='+str(int(args.full_copy)),
        '-DSMW_RASTER_TRACK_WRITES='+str(int(not args.byte_compare)),
        '-Isrc','-Isnesrecomp/runner/src','test/renderer/renderer_test.c',
        'src/smw_renderer_hooks.c','src/smw_video.c','src/smw_renderer.c',str(ppu_object),
        '-Wl,--gc-sections','-lm','-o',str(test)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(test)],env=env,check=True)
    import importlib.util
    spec=importlib.util.spec_from_file_location('hooks',ROOT/'tools/apply_renderer_hooks.py')
    hooks=importlib.util.module_from_spec(spec);spec.loader.exec_module(hooks)
    for entry in ('02A1A4','02A1A7'):
        source=f'    cpu_trace_block(cpu, 0x{entry});\n    if (cpu->_flag_Z == 0) {{ goto L_A211; }}\n'
        patched,hits=hooks.apply(source)
        assert 'fireball' in hits and patched.count('SmwRendererGuestHook(cpu, 0x02A1BEu)')==1
    patched,hits=hooks.apply('    cpu_trace_block(cpu, 0x02A823);\n    if (cpu->_flag_N == 1) { goto L_A84B; }\n')
    assert 'frontier' in hits and 'SmwRendererGuestHook(cpu, 0x02A826u)' in patched
    patched,hits=hooks.apply('    cpu_trace_block(cpu, 0x019E3C);\n    if (cpu->_flag_Z == 0) { goto L_9E93_M1X1; }\n')
    assert 'wing_cull' in hits and patched.count('SmwRendererGuestHook(cpu, 0x019E6Du)')==1
    _,hits=hooks.apply('    cpu_trace_block(cpu, 0x019E3C);\n    if (cpu->_flag_Z == 0) { goto L_OTHER; }\n')
    assert 'wing_cull' not in hits
    for path in (ROOT/'src/gen').glob('*.c'):
        a,_=hooks.apply(path.read_text());b,_=hooks.apply(a)
        assert a==b,path
    if not args.live:return
    if args.resize and os.name!='nt':ap.error('--resize requires Windows')
    if args.scenario!='route' and args.resize:ap.error('visibility scenarios use a fixed window')
    if args.scenario in ('yoshi','pipes') and not args.state:ap.error(f'--scenario {args.scenario} requires --state')
    root=Path(tempfile.mkdtemp(prefix='smw-renderer-',dir=build));print(f'Artifacts: {root}',flush=True)
    frames=4000 if args.resize else 3400
    capture=2800
    route=ROUTE
    if args.scenario=='standing':route=ROUTE.split('wait 200')[0];frames=2800;capture=2700
    if args.scenario in ('yoshi','pipes'):
        route='wait 240\nloadstate 0\n';frames=400;capture=300
        (root/'saves').mkdir();shutil.copy2(args.state.resolve(strict=True),root/'saves/save0.sav')
    if args.scenario=='pipes':
        route+='wait 60\npress left 150\npress right 110\n'
        frames=580;capture=','.join(str(f) for f in range(300,581,20))
    (root/'route.script').write_text(route)
    (root/'config.ini').write_text('[General]\nAutosave=0\nDisableFrameDelay=1\nSkipLauncher=1\n'
        f'[Graphics]\nWindowSize={args.window}\nNewRenderer=1\nNoSpriteLimits=1\nOutputMethod={args.output}\n'
        f'[Sound]\nEnableAudio={int(args.audio)}\n'
        # A foreground QA window must not interpret desktop typing as turbo,
        # pause, reset or state loading. Interactive playtest bindings stay normal.
        '[KeyMap]\nTurbo=\nPause=\nPauseDimmed=\nReset=\nLoad=\nSave=\n')
    env.update(SMW_RENDER_ASPECT=args.aspect,SMW_ENEMY_SPAWN=args.spawn,
               SMW_RENDER_DIAGNOSTICS=str(root),SMW_RENDER_CAPTURE_EVERY='100')
    if not args.resize:env['SMW_RENDER_CAPTURE_FRAME']=str(capture)
    exe=build/('SuperMarioWorldSNESRecomp.exe' if os.name=='nt' else 'SuperMarioWorldSNESRecomp')
    command=[str(exe),str(args.rom.resolve(strict=True)),'--config',str(root/'config.ini'),
             '--script',str(root/'route.script'),'--benchmark-audio' if args.audio else '--benchmark',str(frames)]
    samples=[]
    with (root/'stdout.log').open('w') as out,(root/'stderr.log').open('w') as err:
        process=subprocess.Popen(command,cwd=root,env=env,stdout=out,stderr=err)
        try:
            if args.resize:samples=resize_owned(process,root)
            code=process.wait(timeout=160)
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=10)
    if code:raise RuntimeError(f'game exited {code}')
    data=rows(root/'frames.csv')
    # Compare simulation and presentation counts separately. This harness
    # unbinds turbo, so every simulated frame must also have been rendered.
    benchmarks=[line.removeprefix('SNESRECOMP_BENCHMARK ') for line in
                (root/'stdout.log').read_text().splitlines() if line.startswith('SNESRECOMP_BENCHMARK ')]
    assert len(benchmarks)==1,'missing completed benchmark'
    simulated=json.loads(benchmarks[0])['frames']
    assert simulated==frames,simulated
    assert len(data)==simulated,f'missing rendered frames: {len(data)}/{simulated}'
    if args.scenario=='route':
        assert any(int(x['camera'])>100 and x['mode']=='20' for x in data),'route did not reach moving gameplay'
    report=dict(simulated_frames=simulated,rendered_frames=len(data),widths=sorted({int(x['width']) for x in data}),
        max_native_differences=max(int(x['native_differences']) for x in data),
        max_unexplained_differences=max(int(x['unexplained_differences']) for x in data),
        max_far_enemies=max(int(x['far']) for x in data),resize=samples)
    if args.scenario=='pipes':
        from renderer_visibility import check_pipes
        report['visibility']=check_pipes(root,[int(f) for f in capture.split(',')])
    elif args.scenario!='route':
        from renderer_visibility import check
        report['visibility']=check(root,capture,args.scenario)
    (root/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
    assert report['max_unexplained_differences']==0,'pixels changed outside identified OAM alias corrections'
    if args.spawn=='adaptive' and args.scenario not in ('yoshi','pipes'):assert report['max_far_enemies']>0,'no offscreen activation exercised'

if __name__=='__main__':main()
