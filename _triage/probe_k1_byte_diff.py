"""Per-byte WRAM diff at cascade k=1 (logical frame 201 vs 183).

Looks at the specific addresses flagged as divergent in the cascade
probe at k=1 and prints rec_byte, emu_byte for each. Eliminates
guesswork from coalesced run output.
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
        rec_anchor = 200
        emu_anchor = 182
        # Try k=0 and k=1.
        for k in (0, 1):
            rec_f = rec_anchor + k
            emu_f = emu_anchor + k
            print(f'\n=== k={k}  rec_f={rec_f}, emu_f={emu_f} ===')
            # Dump 256 bytes from $0000.
            rec_dump = cmd(sock, f, f'dump_frame_wram {rec_f} 0 256').get('hex', '').replace(' ', '')
            emu_dump = cmd(sock, f, f'emu_dump_frame_wram {emu_f} 0 256').get('hex', '').replace(' ', '')
            if not rec_dump or not emu_dump:
                print(f'  dump failed: rec_len={len(rec_dump)} emu_len={len(emu_dump)}')
                continue
            print('  zero-page diffs ($00-$FF):')
            for off in range(0x100):
                rec = int(rec_dump[off*2:off*2+2], 16)
                emu = int(emu_dump[off*2:off*2+2], 16)
                if rec != emu:
                    print(f'    ${off:04x}: rec=0x{rec:02x} emu=0x{emu:02x}')
            # Dump $0D70-$0DA0 (GFX33 region).
            rec_dump = cmd(sock, f, f'dump_frame_wram {rec_f} d70 48').get('hex', '').replace(' ', '')
            emu_dump = cmd(sock, f, f'emu_dump_frame_wram {emu_f} d70 48').get('hex', '').replace(' ', '')
            if rec_dump and emu_dump:
                print(f'  $0D70-$0DA0 diffs:')
                for off in range(0, len(rec_dump) // 2):
                    addr = 0xD70 + off
                    rec = int(rec_dump[off*2:off*2+2], 16)
                    emu = int(emu_dump[off*2:off*2+2], 16)
                    if rec != emu:
                        print(f'    ${addr:04x}: rec=0x{rec:02x} emu=0x{emu:02x}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
