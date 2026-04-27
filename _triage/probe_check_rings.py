"""Confirm always-on ring states + scan for $0DB836 reachability."""
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
        time.sleep(8.0)  # Let attract demo run further
        # Block trace count + sample
        r = cmd(sock, f, 'get_block_trace idx_from=0 idx_lim=1')
        print(f'recomp BLOCK ring: total={r.get("total")} next_idx={r.get("next_idx")}')
        # Insn trace (recomp) count
        r = cmd(sock, f, 'get_insn_trace idx_from=0 idx_lim=1')
        print(f'recomp INSN trace: total={r.get("total")}')
        # Emu insn count
        r = cmd(sock, f, 'emu_insn_trace_count')
        print(f'snes9x INSN trace count: {r.get("count")}')
        # Frame counter
        r = cmd(sock, f, 'emu_frame')
        print(f'snes9x frame: {r.get("frame")}')
        # Check if $0DB836 hits in either ring
        # Recomp insn trace might not have idx_from/idx_lim like block trace
        # Try via get_insn_trace with idx_from=0
        r = cmd(sock, f, 'get_insn_trace pc=0x0db836 idx_from=0 idx_lim=2000')
        rec_hits = r.get('log', [])
        print(f'recomp INSN $0db836 hits in first 2000: {len(rec_hits)}')
        # Try block trace at $0DB836 - block hooks fire at every block start
        r = cmd(sock, f, 'get_block_trace pc_lo=0x0db836 pc_hi=0x0db836 idx_from=0 idx_lim=200')
        rec_blocks = r.get('log', [])
        print(f'recomp BLOCK $0db836 hits: {len(rec_blocks)}')
        if rec_blocks:
            print(f'  first 3:')
            for b in rec_blocks[:3]:
                print(f'    {b}')
        # Block trace at any DiagonalLedge PC
        r = cmd(sock, f, 'get_block_trace pc_lo=0x0db7aa pc_hi=0x0db84d idx_from=0 idx_lim=200')
        rec_dl = r.get('log', [])
        print(f'recomp BLOCK in DiagLedge range $0DB7AA-$0DB84D: {len(rec_dl)}')
        if rec_dl:
            print(f'  unique PCs: {sorted(set(b["pc"] for b in rec_dl))[:20]}')
        # Snes9x INSN at $0DB836
        r = cmd(sock, f, 'emu_get_insn_trace pc_lo=0x0db836 pc_hi=0x0db836 limit=200')
        emu_hits = r.get('log', [])
        print(f'snes9x INSN $0db836 hits in first 200: {len(emu_hits)}')
        # Snes9x INSN at any DiagLedge
        r = cmd(sock, f, 'emu_get_insn_trace pc_lo=0x0db7aa pc_hi=0x0db84d limit=200')
        emu_dl = r.get('log', [])
        print(f'snes9x INSN in DiagLedge range: {len(emu_dl)}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
