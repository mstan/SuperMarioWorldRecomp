"""Golden-oracle classification of yoshi-spawn regression.

Both sides advance from cold-start with identical joypad inputs
(emu_oracle_run_frame is called from main.c each frame with the
same inputs as recomp). At any point during attract demo, recomp
WRAM should match snes9x WRAM byte-for-byte. If they diverge,
find_first_divergence pinpoints the first byte and gives a context
window.

Strategy: free-run for several seconds (well past where yoshi
should spawn), then byte-diff. The first divergence + write-trace
attribution tells us which function wrote a wrong byte and which
PC in the ROM caused it.

Outcome decoding:
  (a) recomp byte == snes9x byte at every WRAM offset → no
      divergence visible at probe time; need different probe
      (e.g., earlier sync, sprite-type-specific watch).
  (b) recomp byte != snes9x byte at some offset, recomp matches
      pre-fix HEAD → my fix is wrong (framework regression).
  (c) recomp byte != snes9x byte AND recomp's value matches what
      ROM would compute → snes9x diverged earlier (less likely)
      OR my fix produces ROM-correct values that downstream
      runtime/cfg can't handle (downstream-bug-exposed case).

This first probe is just identifying WHERE divergence is. Followup
probes will trace causality.
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
        # Free-run for ~12 seconds. Yoshi should spawn around frame 900
        # which is ~15s of demo at 60fps (a bit fewer with snes9x's
        # bridged frame pacing). 12s gets us into the demo well past
        # initial sprite spawns.
        time.sleep(12.0)
        # Snapshot frame counters to know where we are.
        rec_frame = cmd(sock, f, 'get_status').get('frame', '?')
        emu = cmd(sock, f, 'emu_frame')
        emu_frame = emu.get('frame', '?')
        print(f'recomp frame: {rec_frame}, snes9x frame: {emu_frame}')
        # Read sprite types on both sides (slot 0..11 at $7E:009E).
        rec_sp = cmd(sock, f, 'dump_ram 0x9e 12')
        rec_hex = rec_sp.get('hex', '').replace(' ', '')
        emu_sp = cmd(sock, f, 'emu_read_wram 9e 12')
        emu_hex = emu_sp.get('hex', '')
        print(f'recomp $7E:009E (sprite types): {rec_hex}')
        print(f'snes9x $7E:009E (sprite types): {emu_hex}')
        rec_has_yoshi = '35' in [rec_hex[i:i+2] for i in range(0, len(rec_hex), 2)]
        emu_has_yoshi = '35' in [emu_hex[i:i+2] for i in range(0, len(emu_hex), 2)]
        print(f'recomp has yoshi ($35 in slot): {rec_has_yoshi}')
        print(f'snes9x has yoshi ($35 in slot): {emu_has_yoshi}')
        # First divergence in zero-page + low WRAM (gameplay state).
        d = cmd(sock, f, 'find_first_divergence wram 0 0x1FFF 32')
        if d.get('match'):
            print('NO DIVERGENCE in $7E:0000-$7E:1FFF')
        else:
            print(f'first divergence: {d.get("first_diff")}')
            print(f'  recomp: {d.get("recomp")}, snes9x: {d.get("oracle")}')
            print(f'  diff_count in range: {d.get("diff_count")}')
            print('  context window:')
            for ctx in d.get('context', [])[:32]:
                marker = ' <--' if ctx.get('diff') else ''
                print(f'    {ctx["adr"]}: r={ctx["r"]} o={ctx["o"]}{marker}')
        # Also check sprite slot region $14C8 (status) explicitly.
        d2 = cmd(sock, f, 'find_first_divergence wram 0x14C8 0x14D3 6')
        if d2.get('match'):
            print('NO DIVERGENCE in sprite-status range $14C8-$14D3')
        else:
            print(f'sprite-status divergence: first={d2.get("first_diff")} '
                  f'recomp={d2.get("recomp")} snes9x={d2.get("oracle")}')
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


if __name__ == '__main__':
    main()
