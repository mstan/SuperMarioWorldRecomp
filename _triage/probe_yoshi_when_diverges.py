"""Find the frame range when snes9x writes 0x35 to slot 9 ($A7) but
recomp doesn't. At t=4s both sides have 0x05 at slot 9; at t=12s
snes9x has 0x35, recomp still has 0x05. Probe at 6, 8, 10s to bracket.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377
SLOT9_TYPE = 0xA7


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
        last_t = 0.0
        for t in [4.0, 6.0, 8.0, 10.0, 11.0, 12.0]:
            time.sleep(t - last_t); last_t = t
            emu_frame = cmd(sock, f, 'emu_frame').get('frame')
            rec_sp = cmd(sock, f, 'dump_ram 0x9e 12').get('hex', '').replace(' ', '')
            emu_sp = cmd(sock, f, 'emu_read_wram 9e 12').get('hex', '')
            rec_a7 = rec_sp[18:20] if len(rec_sp) >= 20 else '??'
            emu_a7 = emu_sp[18:20] if len(emu_sp) >= 20 else '??'
            print(f't={t:.0f}s emu_frame={emu_frame:>5} rec[A7]={rec_a7} emu[A7]={emu_a7} '
                  f'rec_full={rec_sp} emu_full={emu_sp}')
        # When divergence is established, dump ALL emu writes to A7
        e = cmd(sock, f, f'emu_wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        print(f'\nfinal snes9x writes to $A7: count={e.get("count")}')
        for m in e.get('matches', []):
            print(f'  f={m["f"]:>5} pc={m["pc"]} '
                  f'before={m["before"]} after={m["after"]}')
        r = cmd(sock, f, f'wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        print(f'\nfinal recomp writes to $A7: count={r.get("count")}')
        for m in r.get('matches', []):
            print(f'  f={m.get("f"):>5} func={m.get("func", "?")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
