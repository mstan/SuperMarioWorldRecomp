"""Find the first frame pre-fix and post-fix recomp diverge on
$7E:000A using the always-on WRAM-write ring.

Two launches total. Each runs free for ~12s, then queries
wram_writes_at $0A 0 99999 1024 to retrieve the full write timeline.
Diff the two timelines frame-by-frame to find the first divergent
write. The function attribution on that write tells us which recomp
function emits different code under my fix.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
BASE = REPO / '_triage' / 'baseline'
PRE  = BASE / 'smw_pre_fix.exe'
POST = BASE / 'smw_post_fix.exe'
PORT = 4377

# Addresses to track. $0A is the first byte that diverged at t=6s.
# $0E is FindFreeSlot's slot output. $9E is sprite-type slot 0.
TRACK = [0x000A, 0x000E, 0x000F, 0x009E, 0x00A6]


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(sock, f, line):
    sock.sendall((line + '\n').encode())
    return json.loads(f.readline())


def capture_writes(exe_path: pathlib.Path, run_seconds: float):
    """One launch; free-run; pull all writes to TRACK addresses."""
    _kill(); time.sleep(0.6)
    proc = subprocess.Popen([str(exe_path)], cwd=str(REPO),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    out = {}
    try:
        sock = socket.socket()
        for _ in range(60):
            try:
                sock.connect(('127.0.0.1', PORT)); break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.2)
        f = sock.makefile('r')
        f.readline()
        time.sleep(run_seconds)
        for addr in TRACK:
            r = cmd(sock, f, f'wram_writes_at {addr:x} 0 99999 1024')
            out[addr] = r.get('matches', [])
        return out
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


def diff_timeline(addr, pre_writes, post_writes):
    """Compare two write timelines side-by-side, find first divergent
    write."""
    print(f'\n=== $7E:{addr:04x} ===')
    print(f'  pre-fix:  {len(pre_writes)} writes')
    print(f'  post-fix: {len(post_writes)} writes')
    n = min(len(pre_writes), len(post_writes))
    diverged_at = None
    for i in range(n):
        a, b = pre_writes[i], post_writes[i]
        same = (a.get('f') == b.get('f')
                and a.get('after') == b.get('after')
                and a.get('func') == b.get('func'))
        if not same:
            diverged_at = i
            break
    if diverged_at is None and len(pre_writes) != len(post_writes):
        diverged_at = n  # one side has more writes
    if diverged_at is None:
        print('  TIMELINES IDENTICAL up to common length')
    else:
        print(f'  First divergent write at index {diverged_at}:')
        # Show context: 2 before, divergent, 2 after on each side.
        ctx_lo = max(0, diverged_at - 2)
        ctx_hi = diverged_at + 3
        print('       idx  pre-fix                       | post-fix')
        for i in range(ctx_lo, ctx_hi):
            a = pre_writes[i] if i < len(pre_writes) else None
            b = post_writes[i] if i < len(post_writes) else None
            mark = ' <-- DIVERGE' if i == diverged_at else ''
            a_str = (f'f={a.get("f"):>4} after={a.get("after")} '
                     f'func={a.get("func", "?")[:30]}'
                     if a else '<end>')
            b_str = (f'f={b.get("f"):>4} after={b.get("after")} '
                     f'func={b.get("func", "?")[:30]}'
                     if b else '<end>')
            print(f'  [{i:>3}] {a_str:<55} | {b_str}{mark}')


def main():
    print('Capturing pre-fix...')
    pre = capture_writes(PRE, 12.0)
    print('Capturing post-fix...')
    post = capture_writes(POST, 12.0)
    for addr in TRACK:
        diff_timeline(addr, pre.get(addr, []), post.get(addr, []))


if __name__ == '__main__':
    main()
