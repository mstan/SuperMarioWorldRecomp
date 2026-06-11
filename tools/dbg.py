#!/usr/bin/env python3
"""Tiny client for the snesrecomp debug server (TCP 127.0.0.1:4377).

Lets the agent measure/drive the running game without the human in the loop:
read WRAM, inject controller input, read the frame counter, etc. Text protocol
— one command line per request, one newline-terminated JSON reply.

Reusable as a library (import) or CLI:
    python tools/dbg.py frame
    python tools/dbg.py read_ram 1a 4
    python tools/dbg.py set_controller start
"""
import json
import socket
import sys
import time

HOST, PORT = "127.0.0.1", 4377

# button name -> mask (matches debug_server.c parse_controller_mask)
BTN = {"b": 0x001, "y": 0x002, "select": 0x004, "start": 0x008,
       "up": 0x010, "down": 0x020, "left": 0x040, "right": 0x080,
       "a": 0x100, "x": 0x200, "l": 0x400, "r": 0x800}


class Dbg:
    def __init__(self, host=HOST, port=PORT, timeout=5.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        self.buf = b""
        self.greeting = self._readline()  # server sends {"connected":true,...}

    def _readline(self):
        while b"\n" not in self.buf:
            chunk = self.s.recv(65536)
            if not chunk:
                break
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        txt = line.decode(errors="replace").strip()
        try:
            return json.loads(txt)
        except Exception:
            return {"raw": txt}

    def cmd(self, line):
        self.s.sendall((line.strip() + "\n").encode())
        return self._readline()

    # --- helpers ---
    def frame(self):
        return self.cmd("frame").get("frame")

    def read(self, addr, length):
        """Return bytes from WRAM at addr (int)."""
        r = self.cmd(f"read_ram {addr:x} {length}")
        hexs = r.get("hex", "")
        return bytes(int(b, 16) for b in hexs.split()) if hexs else b""

    def u8(self, addr):
        b = self.read(addr, 1)
        return b[0] if b else None

    def u16(self, addr):
        b = self.read(addr, 2)
        return (b[0] | (b[1] << 8)) if len(b) == 2 else None

    def press(self, *names, frames=8, release=True):
        """Hold the given buttons for ~frames frames, then release."""
        mask = 0
        for n in names:
            mask |= BTN[n.lower()]
        self.cmd(f"set_controller 0x{mask:03x}")
        self._wait_frames(frames)
        if release:
            self.cmd("set_controller none")
        return mask

    def _wait_frames(self, n):
        start = self.frame()
        if start is None:
            time.sleep(n / 60.0)
            return
        deadline = start + n
        for _ in range(2000):
            if (self.frame() or 0) >= deadline:
                return
            time.sleep(0.004)

    def wait(self, n):
        self._wait_frames(n)

    # --- OAM timing diagnosis ---
    def oam_timing(self, frames=4, slots=16):
        """Interleave the always-on OAM write + render rings to answer:
        'when/where does ppu->oam get each frame's real sprite tiles vs when
        does the renderer read them?'  For each of the most recent render
        snapshots, print its seq + active-sprite count, then the OAM writes
        whose seq immediately precede it (the DMA burst that fed it):
        whether they landed before the read (seq ordering) and whether the
        written Y bytes were real (not the parked 0xF0 clear)."""
        wr = self.cmd("oam_write_get 4000")
        rd = self.cmd(f"oam_render_get {frames} {slots}")
        snaps = rd.get("snaps", [])
        writes = wr.get("events", [])
        lines = []
        for sn in snaps:
            rseq, f, active = sn["seq"], sn["f"], sn["active"]
            # writes that landed strictly before this render-read
            before = [w for w in writes if w["seq"] < rseq]
            # the contiguous burst right before the read (same frame f or f-… )
            burst = [w for w in before if w["f"] in (f, f - 1)]
            lowwords = [w for w in burst if w["h"] == 0]
            ys = [(int(w["v"], 16) >> 8) for w in lowwords if (w["i"] & 1) == 0]
            real_ys = [y for y in ys if y < 0xE0]
            funcs = {}
            for w in burst:
                funcs[w["func"]] = funcs.get(w["func"], 0) + 1
            top = sorted(funcs.items(), key=lambda kv: -kv[1])[:3]
            lines.append(
                f"frame {f}: render seq={rseq} active={active} | "
                f"writes-before-read={len(burst)} "
                f"low-Y real(<0xE0)={len(real_ys)}/{len(ys)} | "
                f"funcs={top}")
            # show the first few rendered slots
            for i, s in enumerate(sn.get("slot", [])[:6]):
                y, xl, xh, tile, attr = s
                lines.append(f"    slot{i:02d} y={y:3d} xlow={xl:3d} xhigh={xh} "
                             f"tile=0x{tile:02x} attr=0x{attr:02x}")
        return "\n".join(lines) if lines else "(no snapshots — is the game running with a level loaded?)"

    # --- sprite-table -> OAM correlation ---
    def oam_corr(self, nslots=12):
        """Correlate the live sprite tables with the latest OAM render
        snapshot: for each active regular sprite slot print its status,
        signed screen-x and assigned OAM region, then the OAM entries there
        with the 9-bit x the PPU will present and whether the x-high bit
        matches the sign of the sprite's true screen-x (the 9-bit wrap
        check that catches margin sprites drawn on the wrong side)."""
        cam = self.u16(0x1A)
        gm = self.u8(0x100)
        rd = self.cmd("oam_render_get 1 128")
        snaps = rd.get("snaps", [])
        if not snaps:
            return "(no render snapshot — is the game running with a level loaded?)"
        sn = snaps[0]
        slots = sn["slot"]
        lines = [f"game_mode=0x{gm:02x} camera={cam} "
                 f"render_frame={sn['f']} active={sn['active']}"]
        for k in range(nslots):
            st = self.u8(0x14C8 + k)
            if st == 0:
                continue
            wx = self.u8(0xE4 + k) | (self.u8(0x14E0 + k) << 8)
            sx = (wx - cam) & 0xFFFF
            if sx >= 0x8000:
                sx -= 0x10000
            oamidx = self.u8(0x15EA + k)
            base = oamidx >> 2
            lines.append(f" spr slot{k:2d} st=0x{st:02x} screenx={sx:5d} "
                         f"oamidx=0x{oamidx:02x} -> OAM slot {base}:")
            for s in range(base, min(base + 4, 128)):
                y, xl, xh, tile, attr = slots[s]
                x9 = xl | (xh << 8)
                psx = x9 if x9 < 256 else x9 - 512
                want_xh = 1 if (sx < 0 or sx >= 256) else 0
                note = "" if y >= 0xE0 else (
                    " OK" if xh == want_xh
                    else f"  <-- WRONG xhigh (want {want_xh})")
                lines.append(f"     oam{s:3d} y={y:3d} xlow={xl:3d} xhigh={xh}"
                             f" -> ppu_screenx={psx:5d} tile=0x{tile:02x}{note}")
        return "\n".join(lines)


if __name__ == "__main__":
    d = Dbg()
    if len(sys.argv) > 1 and sys.argv[1] == "oam_timing":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 4
        s = int(sys.argv[3]) if len(sys.argv) > 3 else 16
        print(d.oam_timing(n, s))
    elif len(sys.argv) > 1 and sys.argv[1] == "oam_corr":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 12
        print(d.oam_corr(n))
    elif len(sys.argv) > 1:
        print(json.dumps(d.cmd(" ".join(sys.argv[1:]))))
    else:
        print(json.dumps(d.cmd("frame")))
