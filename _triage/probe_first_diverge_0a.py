"""Find the FIRST frame pre-fix and post-fix diverge on $7E:000A.

Earlier probe showed $0A timelines look identical at the tail, but
$0A VALUE differs at frame 600. The earlier writes must contain the
divergence — they were truncated by the 1024 limit. Query in
chunks, walk forward through frames, find first divergent write.
"""
from __future__ import annotations
import json, pathlib, socket, subprocess, time

REPO = pathlib.Path(__file__).parent.parent
BASE = REPO / '_triage' / 'baseline'
PRE  = BASE / 'smw_pre_fix.exe'
POST = BASE / 'smw_post_fix.exe'
PORT = 4377


def _kill():
    subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def cmd(sock, f, line):
    sock.sendall((line + '\n').encode())
    return json.loads(f.readline())


def capture_full_timeline(exe_path: pathlib.Path, addr: int,
                           run_seconds: float = 7.0):
    """Pull writes to addr in 200-frame chunks to bypass limit."""
    _kill(); time.sleep(0.6)
    proc = subprocess.Popen([str(exe_path)], cwd=str(REPO),
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    all_writes = []
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
        for from_f in range(0, 700, 100):
            r = cmd(sock, f, f'wram_writes_at {addr:x} {from_f} {from_f + 100} 4096')
            chunk = r.get('matches', [])
            all_writes.extend(chunk)
        return all_writes
    finally:
        try: sock.close()
        except Exception: pass
        proc.terminate()
        try: proc.wait(timeout=5)
        except subprocess.TimeoutExpired: proc.kill()
        _kill()


def main():
    print('Capturing pre-fix timeline of $0A...')
    pre = capture_full_timeline(PRE, 0x0A)
    print(f'  pre-fix: {len(pre)} writes')
    print('Capturing post-fix timeline of $0A...')
    post = capture_full_timeline(POST, 0x0A)
    print(f'  post-fix: {len(post)} writes')

    # Walk forward, find first divergence.
    n = min(len(pre), len(post))
    diverge = None
    for i in range(n):
        a, b = pre[i], post[i]
        same = (a.get('f') == b.get('f')
                and a.get('after') == b.get('after')
                and a.get('func') == b.get('func'))
        if not same:
            diverge = i
            break
    if diverge is None and len(pre) != len(post):
        diverge = n
    if diverge is None:
        print('No divergence in the captured timeline')
        return
    print(f'\nFirst divergence at index {diverge}:')
    for i in range(max(0, diverge - 3), min(len(pre), diverge + 4)):
        a = pre[i] if i < len(pre) else None
        b = post[i] if i < len(post) else None
        mark = ' <-- DIVERGE' if i == diverge else ''
        a_str = (f'f={a.get("f"):>4} after={a.get("after")} func={a.get("func", "?")[:35]}'
                 if a else '<end>')
        b_str = (f'f={b.get("f"):>4} after={b.get("after")} func={b.get("func", "?")[:35]}'
                 if b else '<end>')
        print(f'  [{i:>4}] pre:  {a_str}')
        print(f'         post: {b_str}{mark}')


if __name__ == '__main__':
    main()
