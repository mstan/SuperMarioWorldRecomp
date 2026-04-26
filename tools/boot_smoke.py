"""Boot smoke test: launch the recomp, advance N frames, assert no
crash and (optionally) byte-identical WRAM/VRAM against a stored
baseline.

Catches boot-time regressions immediately — far faster than eyeballing
the attract demo. The "no-crash" check alone is cheap and catches the
common case where a recomp change introduces a NULL deref or OOB
access during boot. The baseline-compare check catches subtle
semantic regressions (a state-byte differs at frame N).

Usage:
    python tools/boot_smoke.py [--frames N] [--baseline PATH] [--update-baseline]

Modes:
    default            launch + step + assert no crash. Records the
                       state at frame N to stdout for inspection.
    --baseline PATH    after stepping, read WRAM + key VRAM regions
                       and assert they match the baseline file.
    --update-baseline  write the current state to the baseline path
                       (use only when an intentional change is
                       expected to alter boot state).

Requires:
    build/bin-x64-Release/smw.exe (or override --exe)
"""
from __future__ import annotations
import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO / 'build' / 'bin-x64-Release' / 'smw.exe'
DEFAULT_BASELINE = REPO / 'tools' / 'boot_smoke_baseline.json'
PORT = 4377


def kill_existing():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   capture_output=True, check=False)


def launch(exe: pathlib.Path) -> subprocess.Popen:
    if not exe.exists():
        sys.exit(f'recomp binary not found: {exe}')
    kill_existing()
    time.sleep(0.5)
    p = subprocess.Popen(
        [str(exe), '--paused'],
        cwd=str(REPO),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(
                ('127.0.0.1', PORT), timeout=0.3)
            s.close()
            time.sleep(0.3)
            return p
        except OSError:
            time.sleep(0.2)
    p.kill()
    sys.exit('boot_smoke: timeout waiting for recomp TCP port')


class Client:
    def __init__(self, port: int):
        self.sock = socket.create_connection(
            ('127.0.0.1', port), timeout=300)
        self.f = self.sock.makefile('rwb')
        self.f.readline()  # banner

    def cmd(self, line: str) -> dict:
        self.sock.sendall((line + '\n').encode())
        return json.loads(self.f.readline())

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def collect_state(client: Client) -> dict:
    """Pull a small fixed slice of WRAM that's load-bearing for boot
    progression. Keep deliberately compact so the baseline file is
    diffable and the comparison is fast.

    Slices chosen because they cover the visible boot-state knobs:
      - $0100      GameMode (1 byte): which game-mode are we in
      - $13D9      KeepGameModeActiveTimer
      - $1422      LevelMode (the bug-#8 byte)
      - $14C8      Sprite-status table (one byte per slot, 22 slots)
      - $7F:8000   1 byte: I_RESET sentinel
    """
    out = {'frame': client.cmd('frame').get('frame', -1)}
    for label, addr, n in [
        ('GameMode_0100', 0x0100, 1),
        ('KeepGameModeActiveTimer_13D9', 0x13D9, 1),
        ('LevelMode_1422', 0x1422, 1),
        ('SpriteStatus_14C8', 0x14C8, 22),
    ]:
        r = client.cmd(f'read_ram {addr:x} {n}')
        out[label] = r.get('hex', '?')
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=200)
    ap.add_argument('--exe', type=pathlib.Path, default=DEFAULT_EXE)
    ap.add_argument('--baseline', type=pathlib.Path,
                    default=DEFAULT_BASELINE)
    ap.add_argument('--update-baseline', action='store_true')
    ap.add_argument('--no-baseline', action='store_true',
                    help='skip baseline compare even if file exists')
    args = ap.parse_args()

    print(f'boot_smoke: {args.exe.name} -> {args.frames} frames')
    proc = launch(args.exe)
    try:
        client = Client(PORT)
        try:
            # step N issues an unpause+target to the main loop, then
            # blocks for up to ~5s waiting for the requested count.
            # For large N (or when oracle hooks slow the recomp below
            # realtime) we may need to follow up with a poll on the
            # `frame` command. Issue step then poll until the frame
            # counter reaches the target or `proc` exits.
            start_frame = client.cmd('frame').get('frame', 0)
            target_frame = start_frame + args.frames
            client.cmd(f'step {args.frames}')
            deadline = time.time() + 60  # generous: 60s cap
            cur = start_frame
            while time.time() < deadline:
                if proc.poll() is not None:
                    sys.stderr.write(
                        f'  FAIL: recomp process exited unexpectedly '
                        f'at frame {cur}\n')
                    return 1
                cur = client.cmd('frame').get('frame', cur)
                if cur >= target_frame:
                    break
                time.sleep(0.1)
            if cur < target_frame:
                sys.stderr.write(
                    f'  FAIL: only advanced {cur - start_frame}/'
                    f'{args.frames} frames in 60s wall-clock\n')
                return 1
            print(f'  advanced {cur - start_frame} frames OK '
                  f'(frame {start_frame} -> {cur})')

            state = collect_state(client)
        finally:
            client.close()
    finally:
        proc.kill()
        kill_existing()

    print(f'  state at frame {state["frame"]}:')
    for k, v in state.items():
        if k != 'frame':
            print(f'    {k} = {v}')

    if args.update_baseline:
        args.baseline.write_text(json.dumps(state, indent=2))
        print(f'\n  wrote baseline: {args.baseline}')
        return 0

    if args.no_baseline or not args.baseline.exists():
        print(f'\n  (no baseline; pass --update-baseline once to record)')
        return 0

    expected = json.loads(args.baseline.read_text())
    drift = []
    for k, v in expected.items():
        if k == 'frame':
            continue
        if state.get(k) != v:
            drift.append(f'  {k}: baseline={v} actual={state.get(k)}')
    if drift:
        sys.stderr.write(f'\n  FAIL: state drift vs baseline:\n')
        for d in drift:
            sys.stderr.write(d + '\n')
        return 1
    print(f'\n  GREEN: state matches baseline.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
