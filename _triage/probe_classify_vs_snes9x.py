"""Classify whether post-fix diagonal-ledge run matches snes9x.

If post-fix matches snes9x at frame 94 (diagonal-ledge processing
window), then the fix is ROM-correct and yoshi-regression is a
downstream bug exposed by correct codegen. If post-fix differs from
snes9x, the fix itself is buggy.

Probe: compare snes9x's WRAM-write count at frame 94 to recomp's
(both pre-fix and post-fix). The function-attribution pattern at
$0F during frame 94 should match snes9x's pattern in the
correct version.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
BASE = REPO / '_triage' / 'baseline'
PRE  = BASE / 'smw_pre_fix.exe'
POST = BASE / 'smw_post_fix.exe'
PORT = 4377


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(sock, f, line):
    sock.sendall((line + '\n').encode())
    return json.loads(f.readline())


def capture(exe_path: pathlib.Path, run_seconds: float):
    _kill(); time.sleep(0.6)
    proc = subprocess.Popen([str(exe_path)], cwd=str(REPO),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    out = {}
    try:
        sock = socket.socket()
        for _ in range(60):
            try:
                sock.connect(('127.0.0.1', PORT)); break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.2)
        f = sock.makefile('r')
        f.readline()
        time.sleep(run_seconds)
        # Recomp side: writes to $0F at frame 94 (diagonal-ledge window).
        r = cmd(sock, f, 'wram_writes_at f 0 200 1024')
        out['rec_0f'] = r.get('matches', [])
        # Snes9x side: writes to $0F across full window (its frame counter
        # runs slower than recomp's, so diagonal-ledge may be at frame 200+).
        e = cmd(sock, f, 'emu_wram_writes_at f 0 99999 4096')
        out['emu_0f'] = e.get('matches', [])
        # Show snes9x current frame for context.
        out['emu_frame'] = cmd(sock, f, 'emu_frame').get('frame')
        # Slot 0 sprite type ($9E) — same window.
        out['rec_9e'] = cmd(sock, f, 'wram_writes_at 9e 0 700 256').get('matches', [])
        out['emu_9e'] = cmd(sock, f, 'emu_wram_writes_at 9e 0 700 256').get('matches', [])
        return out
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


def summarize_writes_at_frame(label, writes, target_frame, addr_label):
    matches = [w for w in writes if w.get('f') == target_frame]
    print(f'  {label} writes to {addr_label} at frame {target_frame}: {len(matches)}')


def main():
    for label, exe in [('PRE-FIX', PRE), ('POST-FIX', POST)]:
        print(f'\n=== {label} ===')
        d = capture(exe, 2.5)
        # Count writes to $0F at frame 94 in both rec and emu.
        rec_0f_94 = [w for w in d['rec_0f'] if w.get('f') == 94]
        print(f'  recomp $0F writes at frame 94 (recomp clock): {len(rec_0f_94)}')
        # Find which snes9x frame has the highest concentration of $0F writes
        # — that's snes9x's diagonal-ledge frame.
        from collections import Counter
        emu_0f_by_frame = Counter(w.get('f') for w in d['emu_0f'])
        if emu_0f_by_frame:
            top = emu_0f_by_frame.most_common(3)
            print(f'  snes9x $0F writes by frame (top 3 frames): {top}')
            print(f'  total $0F writes in snes9x ring: {sum(emu_0f_by_frame.values())}')
        else:
            print(f'  snes9x $0F: no writes at all in window')
        print(f'  snes9x current frame at probe: {d["emu_frame"]}')
        # And slot 0 progression.
        rec_9e_seq = [(w.get('f'), w.get('func', '?')[:35]) for w in d['rec_9e']]
        emu_9e_seq = [(w.get('f'), w.get('pc', '?')) for w in d['emu_9e']]
        print(f'  recomp $9E (slot 0) writes: {len(rec_9e_seq)}')
        for f, fn in rec_9e_seq[:8]:
            print(f'    f={f} {fn}')
        print(f'  snes9x $9E (slot 0) writes: {len(emu_9e_seq)}')
        for f, pc in emu_9e_seq[:8]:
            print(f'    f={f} pc={pc}')


if __name__ == '__main__':
    main()
