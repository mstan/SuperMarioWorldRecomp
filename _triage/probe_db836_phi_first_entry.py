"""Issue B: confirm forward-goto-without-phi bug at $0DB836.

Hypothesis: label_b836 is reached three ways in the regen:
  1. Forward JMP from $0DB820 (line 6660 in smw_0d_gen.c). NO phi
     emitted because label_b836 hadn't been laid down yet.
  2. Back-edge from $0DB823 (line 6757). Phi: v31=v41, v14=v39, v32=v42.
  3. Back-edge from $0DB82E (line 6770). Phi: v31=v44, v32=v45.

label_b836 reads v31 (A), v14 (X), v32 (Y). On FIRST entry via path 1,
v31 and v32 are zero-initialized — wrong. ROM expects A,Y from JSR
CODE_0DABFD's return at $0DB81D. Then BNE to label_b82e calls
SetMap16HighByteForCurrentObject_Page00(v32=0) instead of the
ROM-correct Y.

Probe: use always-on rings.
  - Recomp Tier-4 insn trace ring: capture (pc, a, x, y) at $0DB836.
  - Snes9x insn trace ring: capture (pc, a, x, y) at $0DB836.
  - Compare A and Y at the FIRST occurrence of $0DB836 in each ring.
  - Compare again at the second/third occurrence (post-back-edge —
    these should match because back-edge phi is in).

If first-entry A/Y differ between rings while later entries match,
the hypothesis is confirmed.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
PORT = 4377
TARGET_PC = 0x0DB836


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
        f.readline()  # banner
        # Free-run for a few seconds so attract demo loads a level
        # and runs the diagonal-ledge code in object-loading.
        time.sleep(4.0)
        # Pull recomp insn trace filtered to $0DB836 (paginated query).
        rec_hits = []
        offset = 0
        while True:
            r = cmd(sock, f, f'get_insn_trace pc_lo=0x0db836 pc_hi=0x0db836 '
                              f'idx_from={offset} idx_lim=2000')
            log = r.get('log', [])
            if not log:
                break
            rec_hits.extend(log)
            nxt = r.get('next_idx')
            if nxt is None or nxt == offset:
                break
            offset = nxt
            if offset > 1_000_000:
                break  # safety
        # Pull snes9x insn trace filtered to $0DB836.
        emu_hits = []
        offset = 0
        while True:
            r = cmd(sock, f, f'emu_get_insn_trace pc_lo=0x0db836 pc_hi=0x0db836 '
                              f'from={offset} limit=2000')
            log = r.get('log', [])
            if not log:
                break
            emu_hits.extend(log)
            emitted = r.get('emitted', 0)
            if emitted == 0:
                break
            offset += emitted + 1
            if offset > 1_000_000:
                break
        print(f'recomp $0DB836 hits: {len(rec_hits)}')
        print(f'snes9x $0DB836 hits: {len(emu_hits)}')
        # Print first 5 of each side-by-side.
        print('\n  idx   recomp           snes9x')
        print('  ----  ---------------  ---------------')
        for i in range(min(8, max(len(rec_hits), len(emu_hits)))):
            r = rec_hits[i] if i < len(rec_hits) else None
            e = emu_hits[i] if i < len(emu_hits) else None
            r_str = (f'a={r.get("a"):>6} y={r.get("y"):>6} x={r.get("x"):>6}'
                     if r else '<none>')
            e_str = (f'a={e.get("a"):>8} y={e.get("y"):>8} x={e.get("x"):>8}'
                     if e else '<none>')
            print(f'  [{i:>3}] {r_str}    {e_str}')
        # Specific test: on FIRST entry, do A and Y match?
        if rec_hits and emu_hits:
            r0, e0 = rec_hits[0], emu_hits[0]
            r_a = int(r0.get('a', '0'), 0) if isinstance(r0.get('a'), str) else r0.get('a', 0)
            r_y = int(r0.get('y', '0'), 0) if isinstance(r0.get('y'), str) else r0.get('y', 0)
            e_a = int(e0.get('a', '0'), 0) if isinstance(e0.get('a'), str) else e0.get('a', 0)
            e_y = int(e0.get('y', '0'), 0) if isinstance(e0.get('y'), str) else e0.get('y', 0)
            # Mask to 8-bit since recomp tracks A/Y 8-bit at $0DB836 (m=1, x=1)
            print(f'\nFIRST-ENTRY 8-bit comparison:')
            print(f'  recomp: A=0x{r_a & 0xFF:02x} Y=0x{r_y & 0xFF:02x}')
            print(f'  snes9x: A=0x{e_a & 0xFF:02x} Y=0x{e_y & 0xFF:02x}')
            a_match = (r_a & 0xFF) == (e_a & 0xFF)
            y_match = (r_y & 0xFF) == (e_y & 0xFF)
            print(f'  A match: {a_match}')
            print(f'  Y match: {y_match}')
            if not (a_match and y_match):
                print('  -> HYPOTHESIS CONFIRMED: forward-goto missing phi at $0DB836')
            else:
                print('  -> First-entry A/Y match; hypothesis FALSIFIED for this PC')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
