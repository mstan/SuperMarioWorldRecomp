"""Tier-3 runtime function-discovery completeness check.

Runs the oracle (embedded snes9x) for N frames of attract demo and
asks: did the recompiler register every function the runtime
actually called? If snes9x visited a code address that recomp's
known function set doesn't include, recomp missed a dispatch
table entry or some other discovery target.

This is the game-agnostic counterpart to the SMWDisX-based Tier-1
test (snesrecomp/tests/test_dispatch_extents.py). It works for any
ROM the recomp has been pointed at because it asks the runtime
directly which addresses matter, instead of needing a pre-existing
disassembly.

Bug class caught: dispatch-table truncation (the koopa-shell-pop
class — recomp emits 6 entries, ROM has 13, snes9x visits handler
#9 at runtime, recomp doesn't know about it). Tier-1 catches the
same class via SMWDisX; Tier-3 catches it via runtime evidence.

Usage:
    python tools/tier3_runtime_completeness.py [--frames N]

Requires:
    build/bin-x64-Oracle/smw.exe (Oracle config build)
"""
from __future__ import annotations
import argparse
import json
import pathlib
import re
import socket
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
ORACLE_EXE = REPO / 'build' / 'bin-x64-Oracle' / 'smw.exe'
GEN_DIR = REPO / 'src' / 'gen'
PORT = 4377  # debug_server.c default


def kill_existing():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   capture_output=True, check=False)


def launch_oracle() -> subprocess.Popen:
    if not ORACLE_EXE.exists():
        sys.exit(f'oracle build missing: {ORACLE_EXE}\n'
                 f'  build with: msbuild smw.sln '
                 f'-p:Configuration=Oracle -p:Platform=x64')
    kill_existing()
    time.sleep(0.5)
    p = subprocess.Popen(
        [str(ORACLE_EXE), '--paused'],
        cwd=str(REPO),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(('127.0.0.1', PORT), timeout=0.3)
            s.close()
            time.sleep(0.3)
            return p
        except OSError:
            time.sleep(0.2)
    p.kill()
    sys.exit('timeout waiting for oracle TCP port')


class DebugClient:
    def __init__(self, port: int):
        self.sock = socket.create_connection(
            ('127.0.0.1', port), timeout=600)
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


def collect_visited_pcs(client: DebugClient, frames: int) -> tuple[set[int], set[int]]:
    """Run snes9x for `frames` frames with the insn trace on, then
    paginate through the trace and return:
      - all_visited: every (pc24) the CPU executed
      - call_targets: subset that immediately followed a JSL/JSR/JML
        (i.e. addresses that act as function entries at runtime)

    JSL=$22 / JSR=$20 / JML=$5C. The instruction AFTER one of these
    in the trace is the call target's first executed insn — exactly
    the address recomp needs to know about as a function entry.

    Pagination via from=/limit= because each call returns at most
    4096 records.
    """
    print(f'  arming insn trace + stepping {frames} frames...')
    client.cmd('emu_insn_trace_reset')
    client.cmd('emu_insn_trace_on')
    client.cmd(f'emu_step {frames}')
    total = client.cmd('emu_insn_trace_count').get('count', 0)
    print(f'  trace: {total} insns')

    all_visited: set[int] = set()
    call_targets: set[int] = set()
    prev_was_call = False
    from_idx = 0
    LIMIT = 4096
    while from_idx < total:
        r = client.cmd(f'emu_get_insn_trace from={from_idx} limit={LIMIT}')
        log = r.get('log', [])
        if not log:
            break
        for entry in log:
            pc = int(entry['pc'], 16)
            op = int(entry['op'], 16)
            all_visited.add(pc)
            if prev_was_call:
                call_targets.add(pc)
            prev_was_call = op in (0x22, 0x20, 0x5C)
        from_idx += len(log)
    return all_visited, call_targets


def collect_recomp_function_set() -> set[int]:
    """Scan src/gen/smw_*_gen.c for `void <name>() { // BBAAAA`-style
    function-definition headers — these are the addresses recomp
    knows about and emitted code for. Includes auto_BB_AAAA entries.

    Returns set of full 24-bit addresses.
    """
    pat = re.compile(
        r'^[A-Za-z][A-Za-z0-9_ ]*\s+\w+\s*\([^)]*\)\s*\{\s*//\s*'
        r'([0-9a-fA-F]{6})\s*$', re.MULTILINE)
    out: set[int] = set()
    for gen in sorted(GEN_DIR.glob('smw_*_gen.c')):
        text = gen.read_text(encoding='utf-8', errors='replace')
        for m in pat.finditer(text):
            out.add(int(m.group(1), 16))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=300,
                    help='attract-demo frames to run (default 300)')
    args = ap.parse_args()

    print(f'Tier-3 runtime function-discovery completeness check')
    print(f'  oracle: {ORACLE_EXE}')
    print(f'  frames: {args.frames}')
    print()

    proc = launch_oracle()
    try:
        client = DebugClient(PORT)
        try:
            all_visited, call_targets = collect_visited_pcs(
                client, args.frames)
        finally:
            client.close()
    finally:
        proc.kill()
        kill_existing()

    print(f'\n  collecting recomp function set from {GEN_DIR}/...')
    known = collect_recomp_function_set()
    print(f'  recomp knows {len(known)} function entries')

    # The signal: call targets snes9x visited that recomp doesn't
    # know about as function entries. These are real "missed function
    # entries" — typically dispatch-table truncation outcomes (the
    # koopa-shell-pop class). Each entry here is a candidate framework
    # bug.
    #
    # all_visited - known is noisier (intra-function blocks count) and
    # is reported as a sanity number, not as a failure list.
    missed_entries = call_targets - known

    print()
    print(f'  all visited PCs:          {len(all_visited)}')
    print(f'  call targets visited:     {len(call_targets)}')
    print(f'    - known to recomp:      {len(call_targets & known)}')
    print(f'    - MISSED by recomp:     {len(missed_entries)}')
    print(f'    (all_visited - known: {len(all_visited - known)} '
          f'— mostly intra-function blocks, expected)')

    if missed_entries:
        print()
        print(f'  Missed function entries (snes9x called these, recomp')
        print(f'  has no function definition for them):')
        for pc in sorted(missed_entries):
            print(f'    {pc:06X}')
        print()
        print(f'  These are recomp framework bugs — usually dispatch-')
        print(f'  table truncation. Use Tier-1 (test_dispatch_extents)')
        print(f'  to find the JSL site whose table is short, then fix')
        print(f'  the recompiler so the missing entry is auto-promoted.')
        return 1
    print()
    print(f'  GREEN: every call target snes9x visited is a known recomp')
    print(f'  function entry over {args.frames} attract-demo frames.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
