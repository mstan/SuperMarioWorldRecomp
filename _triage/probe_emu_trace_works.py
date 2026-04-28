"""Sanity-check emu wram_writes_at by querying TrueFrame ($13)."""
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
        time.sleep(5.0)
        # TrueFrame ($13) increments every frame; each write should appear.
        for addr in (0x13, 0x14, 0x100):
            r = cmd(sock, f, f'emu_wram_writes_at {addr:x} 0 99999 32')
            print(f'\nemu writes @ ${addr:04x} (count={len(r.get("matches", []))}):')
            for m in r.get('matches', [])[:8]:
                print(f'  f={m.get("f")} adr={m.get("adr")} '
                      f'before={m.get("before")} after={m.get("after")} '
                      f'pc={m.get("pc")}')
        # Recomp side same address
        for addr in (0x13, 0x14, 0x100):
            r = cmd(sock, f, f'wram_writes_at {addr:x} 0 99999 32')
            print(f'\nrec writes @ ${addr:04x} (count={len(r.get("matches", []))}):')
            for m in r.get('matches', [])[:8]:
                print(f'  f={m.get("f")} adr={m.get("adr")} '
                      f'old={m.get("old")} val={m.get("val")} '
                      f'w={m.get("w")} func={m.get("func")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
