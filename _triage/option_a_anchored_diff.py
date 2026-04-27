"""Option A primitive: logical-frame anchored WRAM diff.

Anchors by a WRAM-state event (GameMode at $0100 transitioning to a
target value) instead of wall-clock frame number. Finds the FIRST
frame on each side where the anchor condition holds, then byte-
diffs WRAM at that frame.

This is the minimum-viable deterministic-sync primitive: both sides
run from the same reset state through identical demo inputs, so the
LOGICAL state at GameMode-transition is the same ROM event. Comparing
WRAM there tells us whether recomp's codegen produces ROM-correct
state at that event, independently of clock drift.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377

GAME_MODE_ADDR = 0x0100


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(sock, f, line):
    sock.sendall((line + '\n').encode())
    return json.loads(f.readline())


def find_recomp_anchor_frame(sock, f, target_val):
    """Walk recomp's frame history forward via frame_range (efficient
    bulk query). Returns first frame where GameMode == target_val."""
    h = cmd(sock, f, 'history').get('history', {})
    oldest = h.get('oldest', -1); newest = h.get('newest', -1)
    if oldest < 0 or newest < 0:
        return None
    # frame_range returns up to 500 per call.
    fr = oldest
    while fr <= newest:
        end = min(fr + 499, newest)
        r = cmd(sock, f, f'frame_range {fr} {end}')
        for frec in r.get('frames', []):
            mode_hex = frec.get('mode', '0x00')
            try:
                mode = int(mode_hex, 0) if isinstance(mode_hex, str) else mode_hex
            except (ValueError, TypeError):
                continue
            if mode == target_val:
                return frec.get('f')
        fr = end + 1
    return None


def find_emu_anchor_frame(sock, f, addr, target_val):
    h = cmd(sock, f, 'emu_history')
    oldest = h.get('oldest', -1); newest = h.get('newest', -1)
    if oldest < 0 or newest < 0:
        return None
    for fr in range(oldest, newest + 1):
        r = cmd(sock, f, f'emu_wram_at_frame {fr} {addr:x}')
        v = r.get('val', '')
        try:
            if int(v, 0) == target_val:
                return fr
        except (ValueError, TypeError):
            continue
    return None


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
        f.readline()
        time.sleep(8.0)  # Past attract-demo level load.
        # Diagnostic: GameMode trajectory on both sides.
        h = cmd(sock, f, 'history').get('history', {})
        rec_oldest, rec_newest = h.get('oldest', -1), h.get('newest', -1)
        print(f'recomp history: count={h.get("count")} oldest={rec_oldest} newest={rec_newest}')
        for fr in [rec_oldest, rec_oldest + 50, rec_oldest + 150, rec_oldest + 300, rec_newest]:
            if fr < rec_oldest or fr > rec_newest:
                continue
            r = cmd(sock, f, f'frame_range {fr} {fr}')
            frames = r.get('frames', [])
            mode = frames[0].get('mode', '?') if frames else '?'
            print(f'  rec frame {fr}: GameMode={mode}')
        eh = cmd(sock, f, 'emu_history')
        emu_oldest, emu_newest = eh.get('oldest', -1), eh.get('newest', -1)
        print(f'snes9x history: count={eh.get("count")} oldest={emu_oldest} newest={emu_newest}')
        for fr in [emu_oldest, emu_oldest + 50, emu_oldest + 200, emu_oldest + 400, emu_newest]:
            if fr < emu_oldest or fr > emu_newest:
                continue
            r = cmd(sock, f, f'emu_wram_at_frame {fr} {GAME_MODE_ADDR:x}')
            print(f'  emu frame {fr}: GameMode={r.get("val", "?")}')
        # Anchor by GameMode = 0x07 (attract-demo level running).
        target = 0x07
        rec_anchor = find_recomp_anchor_frame(sock, f, target)
        emu_anchor = find_emu_anchor_frame(sock, f, GAME_MODE_ADDR, target)
        print(f'\nrecomp anchor (first frame GameMode=0x{target:02x}): {rec_anchor}')
        print(f'snes9x anchor (first frame GameMode=0x{target:02x}): {emu_anchor}')
        if rec_anchor is None or emu_anchor is None:
            return
        # Diff WRAM at anchor frame: scan key regions.
        # Region map (per SMW WRAM layout):
        #   $00-$FF       zero page (scratch + DP-mapped game state)
        #   $65-$67       Map16LowPtr (3-byte long pointer)
        #   $6E-$70       Map16HighPtr (3-byte long pointer)
        #   $9E-$AB       sprite types (12 slots)
        #   $14C8-$14D3   sprite statuses
        #   $1933-$1956   buffered tile data (used by ?-block / yoshi-block)
        regions = [
            ('zero page',     0x0000, 0x0100),
            ('low DP',        0x0100, 0x0200),
            ('sprite tables', 0x009E, 0x00C0),
            ('Map16 ptrs',    0x0065, 0x0072),
            ('1933 tiles',    0x1933, 0x1957),
        ]
        rec_w = cmd(sock, f, f'dump_frame_wram {rec_anchor} 0 8192').get('hex', '').replace(' ', '')
        for name, lo, hi in regions:
            diffs = []
            for off in range(lo, hi):
                r = cmd(sock, f, f'emu_wram_at_frame {emu_anchor} {off:x}')
                emu_v = r.get('val', '0x00')
                try:
                    emu = int(emu_v, 0) if isinstance(emu_v, str) else emu_v
                except (ValueError, TypeError):
                    emu = 0
                rec_pos = off * 2
                if rec_pos + 2 > len(rec_w):
                    break
                rec = int(rec_w[rec_pos:rec_pos+2], 16)
                if rec != emu:
                    diffs.append((off, rec, emu))
            print(f'\n  {name} ($7E:{lo:04x}-${hi-1:04x}): {len(diffs)}/{hi-lo} byte diffs')
            for off, r, e in diffs[:8]:
                print(f'    $7E:{off:04x}: rec=0x{r:02x} emu=0x{e:02x}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
