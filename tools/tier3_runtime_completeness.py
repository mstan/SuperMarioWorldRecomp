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


def collect_recomp_function_set() -> tuple[set[int], set[int]]:
    """Scan src/gen/smw_*_gen.c for two address sets:

      - func_entries: addresses with a `void <name>(...) { // BBAAAA`
        function-definition header. These are recomp's promoted
        function entries.
      - intra_block_pcs: addresses that recomp knows about as
        mid-function PCs (RDB_INSN_HOOK / RDB_BLOCK_HOOK arguments).
        Labelled `0xBBAAAA` inside the body. A snes9x call target
        landing on one of these is NOT a missed function — it's an
        intra-function jump (RTS-only stub at the tail of another
        function, dispatch entry pointing at a label_XXXX, etc.).

    Returns (func_entries, intra_block_pcs).
    """
    # Return type may be a pointer (`uint8*`, `RetY`, etc.).
    func_pat = re.compile(
        r'^[A-Za-z][A-Za-z0-9_ \*]*\s+\w+\s*\([^)]*\)\s*\{\s*//\s*'
        r'([0-9a-fA-F]{6})\s*$', re.MULTILINE)
    # RDB hooks have signature: RDB_*_HOOK(0xBBAAAA, ...). Capture
    # the first hex literal in the call.
    rdb_pat = re.compile(
        r'RDB_(?:INSN|BLOCK)_HOOK\s*\(\s*0x([0-9a-fA-F]{6})\s*,')
    func_entries: set[int] = set()
    intra_block_pcs: set[int] = set()
    for gen in sorted(GEN_DIR.glob('smw_*_gen.c')):
        text = gen.read_text(encoding='utf-8', errors='replace')
        for m in func_pat.finditer(text):
            func_entries.add(int(m.group(1), 16))
        for m in rdb_pat.finditer(text):
            intra_block_pcs.add(int(m.group(1), 16))
    # Function entries themselves also have hooks at their entry PC,
    # so subtract — `intra_block_pcs` should mean "known PC that is
    # NOT a function entry."
    intra_block_pcs -= func_entries
    return func_entries, intra_block_pcs


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
    func_entries, intra_block_pcs = collect_recomp_function_set()
    print(f'  recomp knows {len(func_entries)} function entries')
    print(f'  recomp knows {len(intra_block_pcs)} intra-function PCs')

    # The signal: call targets snes9x visited that recomp doesn't
    # know about as function entries AND that aren't recognized intra-
    # function PCs (label_XXXX, RTS-only stubs at function tails, etc.).
    # Surviving entries are real "missed function entries" — typically
    # dispatch-table truncation outcomes (the koopa-shell-pop class).
    known = func_entries | intra_block_pcs
    # WRAM-resident routines ($7E:XXXX / $7F:XXXX) can't be recompiled
    # — their bytes are mutable at runtime, so recomp never has a body
    # for them. SMW jumps into WRAM for HLE-style helpers. Filter
    # these from the missed-entries report; if they need any recomp-
    # side handling it's a separate runtime/HLE concern, not a
    # discovery-pass bug.
    missed_entries = {
        pc for pc in (call_targets - known)
        if (pc >> 16) not in (0x7E, 0x7F)
    }

    print()
    print(f'  all visited PCs:                 {len(all_visited)}')
    print(f'  call targets visited:            {len(call_targets)}')
    print(f'    - is known function entry:     '
          f'{len(call_targets & func_entries)}')
    print(f'    - is intra-function block:     '
          f'{len(call_targets & intra_block_pcs)}')
    print(f'    - MISSED by recomp entirely:   {len(missed_entries)}')

    if missed_entries:
        print()
        print(f'  Missed ROM function entries (snes9x called these,')
        print(f'  recomp has no function definition or known intra-')
        print(f'  block PC for them):')
        for pc in sorted(missed_entries):
            print(f'    {pc:06X}')
        print()
        print(f'  These are recomp framework bugs - usually dispatch-')
        print(f'  table truncation. Use Tier-1 (test_dispatch_extents)')
        print(f'  to find the JSL site whose table is short, then fix')
        print(f'  the recompiler so the missing entry is auto-promoted.')
        return 1
    print()
    print(f'  GREEN: every ROM call target snes9x visited is a known')
    print(f'  recomp function entry or intra-block PC, over '
          f'{args.frames} attract-demo frames.')
    print(f'  (WRAM-resident calls excluded — those are HLE-style and')
    print(f'  cannot be recompiled.)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
