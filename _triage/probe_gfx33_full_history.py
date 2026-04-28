"""Compare ALL writes to GFX33 cascade addresses across full history.

The previous probe used a wrong frame window. emu's frame counter is
WALL-CLOCK frames (per retro_run), recomp's is LOGICAL frames. They
don't directly compare.

This probe queries the ENTIRE history on both sides for $0D76 (and a
few siblings), then prints a side-by-side. The structural question:
does emu also write to $0D76 every "frame" once GM=7 starts, or only
sometimes?
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(s, f, line):
    s.sendall((line + '\n').encode())
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
        # Find GM transitions on both sides.
        ge = cmd(sock, f, 'emu_wram_writes_at 100 0 99999 64').get('matches', [])
        gr = cmd(sock, f, 'wram_writes_at 100 0 99999 64').get('matches', [])
        print('emu GM transitions:')
        for m in ge:
            print(f'  f={m.get("f")} -> {m.get("after")} pc={m.get("pc")}')
        print('rec GM transitions:')
        for m in gr:
            print(f'  f={m.get("f")} -> {m.get("val")} func={m.get("func")}')
        # Find emu logical-frame frame# corresponding to GM=7 entry.
        emu_gm7_frame = None
        for m in ge:
            try:
                v = int(m.get('after', '0x0'), 0)
            except Exception:
                continue
            if v == 7:
                emu_gm7_frame = m.get('f')
                break
        rec_gm7_frame = None
        for m in gr:
            try:
                v = int(m.get('val', '0x0'), 0)
            except Exception:
                continue
            if v == 7:
                rec_gm7_frame = m.get('f')
                break
        print(f'\nemu GM=7 transition at f={emu_gm7_frame}')
        print(f'rec GM=7 transition at f={rec_gm7_frame}')
        # Now query writes to $0D76 over the entire history.
        for addr in (0x0D76, 0x0D7C, 0x0D80, 0x0D85, 0x0D9F):
            ew = cmd(sock, f, f'emu_wram_writes_at {addr:x} 0 99999 256').get('matches', [])
            rw = cmd(sock, f, f'wram_writes_at {addr:x} 0 99999 256').get('matches', [])
            print(f'\n--- $7E:{addr:04x} ---')
            print(f'  emu count={len(ew)}: '
                  f'first_f={ew[0].get("f") if ew else None} '
                  f'last_f={ew[-1].get("f") if ew else None}')
            for m in ew[:4] + ew[-4:]:
                print(f'    f={m.get("f")} after={m.get("after")} pc={m.get("pc")}')
            print(f'  rec count={len(rw)}: '
                  f'first_f={rw[0].get("f") if rw else None} '
                  f'last_f={rw[-1].get("f") if rw else None}')
            for m in rw[:4] + rw[-4:]:
                print(f'    f={m.get("f")} val={m.get("val")} w={m.get("w")} '
                      f'func={m.get("func")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
