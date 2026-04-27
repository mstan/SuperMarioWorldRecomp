import socket, json, subprocess, time, pathlib
EXE = pathlib.Path('_triage/baseline/smw_post_fix.exe')
subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.6)
proc = subprocess.Popen([str(EXE)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sock = socket.socket()
for _ in range(60):
    try:
        sock.connect(('127.0.0.1', 4377)); break
    except (ConnectionRefusedError, OSError):
        time.sleep(0.2)
f = sock.makefile('r')
f.readline()
time.sleep(2.0)
sock.sendall(b'dump_ram 0 8192\n')
resp = json.loads(f.readline())
hx = resp.get('hex', '').replace(' ', '')
print(f'returned hex chars: {len(hx)}; bytes: {len(hx)//2}')
print(f'addr: {resp.get("addr")} len: {resp.get("len")}')
print(f'first 40 hex: {hx[:80]}')
proc.terminate()
subprocess.run(['taskkill', '/F', '/IM', 'smw.exe'],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
