"""Same sprite (type 0x01 → 0x35 yoshi-egg-stunned) is placed in
DIFFERENT slots: recomp slot 0, snes9x slot 8. snes9x's slot 8
hatches into yoshi $35 by frame 731. Recomp's slot 0 stays $01.

Hypothesis: FindFreeNormalSpriteSlot_HighPriority should return the
HIGHEST free slot. snes9x picks slot 8 (correct — slots 9, 10, 11
have other sprites). Recomp picks slot 0 (LOWEST free) — wrong order.

Probe: query writes to all 12 sprite-type slots ($A0-$AB) on both
sides at t=8s. The first divergent write tells us which slot
allocator path differs.
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
        time.sleep(8.0)
        # Snes9x writes to slot 8 type ($A6).
        for slot in (0, 8, 9):
            addr = 0x9e + slot
            e = cmd(sock, f, f'emu_wram_writes_at {addr:x} 0 800 64')
            print(f'snes9x writes to slot{slot} ($7E:{addr:04x}): count={e.get("count")}')
            for m in e.get('matches', []):
                print(f'  f={m["f"]:>4} pc={m["pc"]} '
                      f'before={m["before"]} after={m["after"]}')
            r = cmd(sock, f, f'wram_writes_at {addr:x} 0 800 64')
            print(f'recomp writes to slot{slot} ($7E:{addr:04x}): count={r.get("count")}')
            for m in r.get('matches', []):
                print(f'  f={m.get("f"):>4} func={m.get("func", "?")}')
            print()
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
