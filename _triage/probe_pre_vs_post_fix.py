"""Compare PRE-FIX and POST-FIX recomp WRAM at frames 442 and 579.

Both binaries get the same demo inputs (snes9x oracle is in both,
attract demo runs deterministically). At frame 442 both versions
should be byte-identical; at frame 579 we expect them to diverge —
the FIRST byte that differs identifies what my fix changed.
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


def capture(exe_path: pathlib.Path, target_t_seconds: float):
    """Launch exe, wait target_t_seconds (~target frame), dump WRAM."""
    _kill(); time.sleep(0.6)
    proc = subprocess.Popen([str(exe_path)], cwd=str(REPO),
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
        time.sleep(target_t_seconds)
        emu_frame = cmd(sock, f, 'emu_frame').get('frame')
        wram = cmd(sock, f, 'dump_ram 0 8192')
        wram_hex = wram.get('hex', '').replace(' ', '')
        sp = cmd(sock, f, 'dump_ram 0x9e 12').get('hex', '').replace(' ', '')
        return emu_frame, wram_hex, sp
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


def diff_wram(name_a, hex_a, name_b, hex_b, max_show=24):
    """Print first N bytes that differ."""
    n = min(len(hex_a), len(hex_b))
    diffs = []
    for byte_i in range(0, n, 2):
        a = hex_a[byte_i:byte_i+2]
        b = hex_b[byte_i:byte_i+2]
        if a != b:
            diffs.append((byte_i // 2, a, b))
    print(f'{name_a} vs {name_b}: {len(diffs)} byte diffs in $0-$1FFF')
    for adr, a, b in diffs[:max_show]:
        print(f'  $7E:{adr:04x}: {name_a}=0x{a} {name_b}=0x{b}')


def main():
    # Capture at t=4s (≈ frame 442 — both sides should match).
    print('=== Sample 1: t=4s (expect convergence) ===')
    pre_f, pre_w, pre_s = capture(PRE, 4.0)
    print(f'pre-fix:  emu_frame={pre_f} sprite_types={pre_s}')
    post_f, post_w, post_s = capture(POST, 4.0)
    print(f'post-fix: emu_frame={post_f} sprite_types={post_s}')
    diff_wram('pre', pre_w, 'post', post_w)

    print('\n=== Sample 2: t=6s (expect divergence) ===')
    pre_f, pre_w, pre_s = capture(PRE, 6.0)
    print(f'pre-fix:  emu_frame={pre_f} sprite_types={pre_s}')
    post_f, post_w, post_s = capture(POST, 6.0)
    print(f'post-fix: emu_frame={post_f} sprite_types={post_s}')
    diff_wram('pre', pre_w, 'post', post_w)

    print('\n=== Bracket: t=4.5, 4.8, 5.0, 5.2 ===')
    for t in [4.5, 4.8, 5.0, 5.2]:
        pre_f, pre_w, pre_s = capture(PRE, t)
        post_f, post_w, post_s = capture(POST, t)
        n_diffs = sum(1 for i in range(0, min(len(pre_w), len(post_w)), 2)
                      if pre_w[i:i+2] != post_w[i:i+2])
        print(f't={t} pre_f={pre_f} post_f={post_f} diffs={n_diffs}')
        if 0 < n_diffs < 8:
            for byte_i in range(0, min(len(pre_w), len(post_w)), 2):
                a = pre_w[byte_i:byte_i+2]
                b = post_w[byte_i:byte_i+2]
                if a != b:
                    print(f'  $7E:{byte_i//2:04x}: pre=0x{a} post=0x{b}')


if __name__ == '__main__':
    main()
