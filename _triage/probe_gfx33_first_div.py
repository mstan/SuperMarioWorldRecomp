"""Trace the first-divergent writes on $7E:0D76+ (GFX33 pointers).

The cascade probe shows $7E:0D76-$7E:0D9A is the first cascade region
to diverge after the GM=0x07 anchor (k=1, rec_frame=201). These are
GFX33 source/dest pointers and DynGfx state (DynGfxTilePtr, HDMAEnable,
IRQNMICommand). They are written every frame by the GFX-update chain.

Strategy:
  1. Boot Oracle, wait for level-load (GM=0x07).
  2. Find rec/emu anchor frames.
  3. Query both sides' WRAM write rings for $0D76, $0D78, $0D7A, $0D7C,
     $0D7E, $0D80, $0D85, $0D87, $0D89, $0D9F (the diverging fields).
  4. For each address, find the first frame >= anchor where one side
     wrote a value that differs from what the other side wrote.
  5. The (frame, addr, recomp_func, emu_pc) identifies the codegen
     divergence point.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(sock, f, line):
    sock.sendall((line + '\n').encode())
    return json.loads(f.readline())


def find_recomp_anchor(sock, f, target=0x07):
    h = cmd(sock, f, 'history').get('history', {})
    oldest = h.get('oldest', -1); newest = h.get('newest', -1)
    if oldest < 0:
        return None
    fr = oldest
    while fr <= newest:
        end = min(fr + 499, newest)
        r = cmd(sock, f, f'frame_range {fr} {end}')
        for frec in r.get('frames', []):
            try:
                m = int(frec.get('mode', '0x0'), 0)
            except (ValueError, TypeError):
                continue
            if m == target:
                return frec.get('f')
        fr = end + 1
    return None


def find_emu_anchor(sock, f, target=0x07):
    h = cmd(sock, f, 'emu_history')
    oldest = h.get('oldest', -1); newest = h.get('newest', -1)
    if oldest < 0:
        return None
    for fr in range(oldest, newest + 1):
        r = cmd(sock, f, f'emu_wram_at_frame {fr} 100')
        try:
            v = int(r.get('val', '0x0'), 0)
        except (ValueError, TypeError):
            continue
        if v == target:
            return fr
    return None


def query_writes(sock, f, side, addr, lo_frame, hi_frame, limit=512):
    """Return list of (frame, val, attribution) for writes to addr in
    [lo_frame, hi_frame] on the given side ('rec' or 'emu')."""
    if side == 'rec':
        r = cmd(sock, f, f'wram_writes_at {addr:x} {lo_frame} {hi_frame} {limit}')
        out = []
        for m in r.get('matches', []):
            fr = m.get('f')
            v = m.get('val')
            try:
                v = int(v, 0) if isinstance(v, str) else v
            except (ValueError, TypeError):
                v = None
            out.append((fr, v, m.get('func', '?')))
        return out
    else:
        r = cmd(sock, f, f'emu_wram_writes_at {addr:x} {lo_frame} {hi_frame} {limit}')
        out = []
        for m in r.get('matches', []):
            fr = m.get('f')
            v = m.get('after', '0x0')
            try:
                v = int(v, 0) if isinstance(v, str) else v
            except (ValueError, TypeError):
                v = None
            out.append((fr, v, m.get('pc', '?')))
        return out


def main():
    _kill(); time.sleep(0.5)
    proc = subprocess.Popen([str(EXE)], cwd=str(REPO),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        sock = socket.socket()
        for _ in range(60):
            try:
                sock.connect(('127.0.0.1', PORT)); break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.2)
        f = sock.makefile('r')
        f.readline()  # banner
        time.sleep(8.0)
        rec_anchor = find_recomp_anchor(sock, f)
        emu_anchor = find_emu_anchor(sock, f)
        print(f'rec_anchor={rec_anchor} emu_anchor={emu_anchor}')
        if rec_anchor is None or emu_anchor is None:
            return 1
        # GFX33 cascade addresses (from cascade_run_2026_04_27.txt).
        # These are the bytes that first diverge at k=1 (frame 201).
        # Limit to 4 logical frames after anchor for a tight window.
        addrs = [0x0D76, 0x0D77, 0x0D78, 0x0D79, 0x0D7A, 0x0D7B,
                 0x0D7C, 0x0D7D, 0x0D80, 0x0D85, 0x0D9B, 0x0D9F]
        rec_lo = rec_anchor - 2; rec_hi = rec_anchor + 4
        emu_lo = emu_anchor - 2; emu_hi = emu_anchor + 4
        print(f'rec window {rec_lo}..{rec_hi}, emu window {emu_lo}..{emu_hi}')
        for addr in addrs:
            print(f'\n--- $7E:{addr:04x} ---')
            rec_w = query_writes(sock, f, 'rec', addr, rec_lo, rec_hi)
            emu_w = query_writes(sock, f, 'emu', addr, emu_lo, emu_hi)
            print(f'  rec writes ({len(rec_w)}):')
            for fr, v, fn in rec_w[:16]:
                vs = f'0x{v:02x}' if isinstance(v, int) else str(v)
                print(f'    f={fr} val={vs} func={fn}')
            print(f'  emu writes ({len(emu_w)}):')
            for fr, v, pc in emu_w[:16]:
                vs = f'0x{v:02x}' if isinstance(v, int) else str(v)
                print(f'    f={fr} val={vs} pc={pc}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()
    return 0


if __name__ == '__main__':
    main()
