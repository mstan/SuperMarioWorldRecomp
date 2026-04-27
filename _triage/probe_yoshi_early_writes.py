"""Probe at multiple early time points before snes9x WRAM-write ring
saturates. We need to see the actual write that puts $35 (yoshi) into
$7E:00A7 in snes9x, and compare to whether recomp emits a similar
write. The 1M-entry ring saturates around 12-15s; probing at 4-6s
should capture early yoshi spawn if it happens then.
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
        # Probe at 4 seconds — past yoshi-egg-spawn but before ring sat.
        time.sleep(4.0)
        emu_frame = cmd(sock, f, 'emu_frame').get('frame')
        print(f'snes9x frame at probe: {emu_frame}')
        # Read sprite types both sides.
        rec_sp = cmd(sock, f, 'dump_ram 0x9e 12')
        emu_sp = cmd(sock, f, 'emu_read_wram 9e 12')
        print(f'\nrecomp $7E:009E: {rec_sp.get("hex", "").replace(" ", "")}')
        print(f'snes9x $7E:009E: {emu_sp.get("hex", "")}')
        # Dump ALL writes to slot 9 sprite type ($A7) on snes9x.
        e = cmd(sock, f, f'emu_wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        print(f'\nsnes9x writes to $7E:{SLOT9_TYPE:04x}: count={e.get("count")}')
        for m in e.get('matches', [])[:30]:
            print(f'  f={m["f"]:>5} pc={m["pc"]} '
                  f'before={m["before"]} after={m["after"]}')
        # Recomp side.
        r = cmd(sock, f, f'wram_writes_at {SLOT9_TYPE:x} 0 99999 256')
        print(f'\nrecomp writes to $7E:{SLOT9_TYPE:04x}: count={r.get("count")}')
        for m in r.get('matches', [])[:30]:
            print(f'  f={m.get("f"):>5} func={m.get("func", "?")} '
                  f'before={m.get("before")} after={m.get("after")}')
        # Also dump writes to slot 8 ($A6) — snes9x had 04 there.
        e8 = cmd(sock, f, 'emu_wram_writes_at a6 0 99999 256')
        print(f'\nsnes9x writes to $7E:00a6 (slot 8): count={e8.get("count")}')
        for m in e8.get('matches', [])[:10]:
            print(f'  f={m["f"]:>5} pc={m["pc"]} '
                  f'before={m["before"]} after={m["after"]}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
