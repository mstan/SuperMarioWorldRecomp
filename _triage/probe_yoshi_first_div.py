"""Bracket the first temporal divergence between recomp and snes9x.

Free-run, then sample WRAM byte counts that differ at increasing
emu_frame counts. Where 0 diffs → recomp still matches; >0 →
divergence has started. Bisect to first frame.

Once first frame is known, query the always-on WRAM-write rings on
BOTH sides for the divergence target address — first pair of writes
that differ identifies the offending instruction (recomp's call
stack via watchpoint; snes9x's PC via emu_wram_writes_at).
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
        # Sample at 0.5, 1, 2, 3, 4, 5, 6 seconds.
        samples = [0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
        last_t = 0.0
        results = []
        for t in samples:
            time.sleep(t - last_t)
            last_t = t
            emu_frame = cmd(sock, f, 'emu_frame').get('frame', '?')
            d = cmd(sock, f, 'find_first_divergence wram 0 0x1FFF 0')
            if d.get('match'):
                results.append((t, emu_frame, 0, None, None, None))
                print(f't={t:.1f}s emu_frame={emu_frame} diffs=0 (match)')
            else:
                results.append((t, emu_frame, d.get('diff_count'),
                                d.get('first_diff'), d.get('recomp'),
                                d.get('oracle')))
                print(f't={t:.1f}s emu_frame={emu_frame} '
                      f'diffs={d.get("diff_count")} '
                      f'first={d.get("first_diff")} '
                      f'r={d.get("recomp")} o={d.get("oracle")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
