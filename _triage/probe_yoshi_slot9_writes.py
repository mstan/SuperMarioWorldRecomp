"""Compare slot-9 sprite-type writes pre/post fix vs snes9x.

snes9x has $35 (yoshi) in slot 9 at frame ~1070; recomp has $05.
Query both rings for ALL writes to $7E:00A7 ($9E + 9) over the
attract demo. Compare:
  - Recomp's writes: when, by which function, what value?
  - Snes9x's writes: when, by which PC, what value?

The first writes that DIFFER identify where divergence began.
Free-running drift is normal at boot; what matters is whether
recomp WROTE the right SEQUENCE of values to slot 9.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377
SLOT9_TYPE = 0xA7  # $7E:009E + 9


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
        time.sleep(12.0)
        emu_frame = cmd(sock, f, 'emu_frame').get('frame')
        print(f'snes9x frame at probe: {emu_frame}')
        # Snes9x always-on WRAM-write trace.
        e = cmd(sock, f, f'emu_wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        print(f'\nsnes9x writes to $7E:{SLOT9_TYPE:04x} (slot 9 type):')
        print(f'  count={e.get("count")}')
        for m in e.get('matches', [])[:24]:
            print(f'    f={m["f"]:>5} pc={m["pc"]} '
                  f'before={m["before"]} after={m["after"]}')
        # Recomp side: use wram_writes_at if available (Tier-1).
        r = cmd(sock, f, f'wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        if r.get('ok'):
            print(f'\nrecomp writes to $7E:{SLOT9_TYPE:04x}:')
            print(f'  count={r.get("count")}')
            for m in r.get('matches', [])[:24]:
                print(f'    f={m.get("f"):>5} func={m.get("func", "?")} '
                      f'before={m.get("before")} after={m.get("after")}')
        else:
            print(f'\nrecomp wram_writes_at not available: {r.get("error")}')
            # Fall back: dump current value.
            cur = cmd(sock, f, f'dump_ram 0x{SLOT9_TYPE:x} 1')
            print(f'recomp current $7E:{SLOT9_TYPE:04x} = {cur.get("hex")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
