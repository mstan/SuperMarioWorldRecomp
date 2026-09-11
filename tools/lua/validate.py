#!/usr/bin/env python3
"""Exercise the real SMW executable through TCP; requires the user's stock ROM."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--port", type=int, default=4381)
    parser.add_argument("--keep-running", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.engine.resolve() / "tools"))
    from lua_tcp import LuaClient
    exe, rom = args.exe.resolve(), args.rom.resolve()
    report = {"checks": [], "exe": str(exe), "rom_bytes": rom.stat().st_size}
    environment = dict(os.environ, SNESRECOMP_LUA_PORT=str(args.port),
                       SNESRECOMP_LUA_PAUSED="1", SNESRECOMP_NO_LAUNCHER="1",
                       SNESRECOMP_FORCE_TURBO="1")
    log = open(exe.parent / "lua-validation-game.log", "w")
    process = subprocess.Popen([str(exe), str(rom)], cwd=exe.parent, env=environment,
                               stdout=log, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    report["pid"] = process.pid
    client = None

    def checked(name, evidence=None):
        report["checks"].append({"name": name, "evidence": evidence})
        print(f"PASS {name}: {evidence}", flush=True)

    def values(source):
        return client.eval(source)["values"]

    def fails(source, expected):
        try:
            client.eval(source)
        except RuntimeError as error:
            assert expected in str(error), str(error)
        else:
            raise AssertionError(f"expected {expected}: {source}")

    def wait_script(timeout=60):
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            result = client.command("status")
            assert not result["error"], result
            if not result["running"]:
                return result
            time.sleep(.02)
        raise TimeoutError("Lua script did not finish")

    try:
        deadline = time.monotonic()+90
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"game exited {process.returncode}; see {log.name}")
            try:
                client = LuaClient(port=args.port, timeout=60)
                break
            except OSError:
                time.sleep(.2)
        if not client:
            raise TimeoutError("Lua server did not start")
        assert client.command("status")["frame"] == 0
        checked("boot paused with live TCP")
        # The untouched boot WRAM is restored before any frame is simulated.
        original = values("return mainmemory.read_u32_le(0x1ff00)")[0]
        assert values("memory.write_u32_le(0x1ff00,0x89abcdef); return memory.read_u32_be(0x1ff00),memory.read_s8(0x1ff00),memory.read_s24_le(0x1ff00)") == ["4023233417", "-17", "-5517841"]
        assert values("return memory.read_u8(0x7fff00,'System Bus'),memory.readbyterange(0x1ff00,2)[0],memory.read_bytes_as_array(0x1ff00,2)[1]") == ["239","239","239"]
        assert values("memory.usememorydomain('CARTROM'); return mainmemory.getsize(),memory.getmemorydomainsize(),memory.getcurrentmemorydomain()") == ["131072","524288","CARTROM"]
        values(f"mainmemory.write_u32_le(0x1ff00,{original}); memory.usememorydomain('WRAM')")
        for source, expected in [
            ("memory.write_u16_le(0x1ffff,1)", "outside WRAM"),
            ("memory.read_u8(-1)", "outside WRAM"),
            ("memory.read_u8(0x2100,'System Bus')", "WRAM only"),
            ("memory.read_u16_le(0x1fff,'System Bus')", "mirror boundary"),
            ("memory.write_u8(0,0,'CARTROM')", "read-only"),
            ("memory.read_u8(0x80000,'CARTROM')", "outside CARTROM"),
            ("while true do end", "budget exceeded"),
            ("local x=string.rep('x',32*1024*1024)", "not enough memory"),
            ("emu.frameadvance()", "TCP run script"),
        ]:
            fails(source, expected)
        checked("memory domains, endian/sign, bounds and runaway-script recovery")
        # Send fragmented then coalesced requests through the real socket.
        payload = b"eval " + b"return 42".hex().encode() + b"\n"
        client.socket.sendall(payload[:7]); time.sleep(.04)
        client.socket.sendall(payload[7:]+b"ping\n")
        assert json.loads(client.reader.readline())["values"] == ["42"]
        assert json.loads(client.reader.readline())["ok"]
        checked("fragmented and pipelined TCP framing")
        assert values("return true,false,nil,string.char(0,10,34,92,255)") == [True,False,None,'\x00\n"\\\xff']
        client.eval("counter=0; event.onframestart(function() counter=counter+1 end,'counter')")
        client.step(3)
        assert values("return counter") == ["3"]
        client.eval("event.unregisterbyname('counter')")
        client.step(2)
        assert values("return counter") == ["3"]
        client.eval("event.onframeend(function() error('callback broke') end,'bad')")
        result = client.step(1)
        assert "callback broke" in result["error"]
        assert values("return event.unregisterbyname('bad')") == [False]
        client.run("local f=emu.framecount(); for i=1,4 do emu.frameadvance() end; coroutine_frames=emu.framecount()-f")
        wait_script()
        assert values("return coroutine_frames") == ["4"]
        checked("frame callbacks, callback error isolation, coroutine frameadvance")
        client.eval((ROOT / "tools/lua/smw.lua").read_text())
        client.run((ROOT / "tools/lua/enter_level.lua").read_text())
        wait_script()
        assert values("return mainmemory.read_u8(0x100),mainmemory.read_u8(0x109)") == ["20","0"]
        checked("navigated into playable level with joypad.set", values("return smw.status(),mainmemory.read_u8(0x13bf)"))
        x, y = map(int, values("return mainmemory.read_u16_le(0x94),mainmemory.read_u16_le(0x96)"))
        client.eval(f"smw.invincible(true); smw.powerup(3); smw.teleport({x+16},{y}); mainmemory.write_u8(0x76,1)")
        assert values("return mainmemory.read_u16_le(0x94),mainmemory.read_u8(0x19)") == [str(x+16),"3"]
        checked("powerup and teleport", {"x_before": x, "x_after": x+16})
        slot = int(values("spawn_slot=smw.spawn(0x0f); return spawn_slot")[0])
        client.step(1)
        assert values(f"return mainmemory.read_u8(0x14c8+{slot}),mainmemory.read_u8(0x9e+{slot})") == ["8","15"]
        sx = int(values(f"return mainmemory.read_u8(0xe4+{slot})+256*mainmemory.read_u8(0x14e0+{slot})")[0])
        client.step(24)
        sx_after = int(values(f"return mainmemory.read_u8(0xe4+{slot})+256*mainmemory.read_u8(0x14e0+{slot})")[0])
        assert sx != sx_after, "spawned sprite did not move"
        checked("spawned Goomba initialized and moved under game simulation", {"slot":slot,"x_before":sx,"x_after":sx_after})
        assert slot <= 9, "ordinary enemies must be in the stock fireball collision scan"
        client.eval("demo_hit=false; event.onframeend(function() if mainmemory.read_u8(0x9e+spawn_slot)==0x21 then demo_hit=true end end,'demo.collision')")
        client.eval("shot_slot=smw.fireball(); assert(shot_slot)")
        fx = int(values("return mainmemory.read_u8(0x171f+shot_slot)+256*mainmemory.read_u8(0x1733+shot_slot)")[0])
        client.step(3)
        fx_after = int(values("return mainmemory.read_u8(0x171f+shot_slot)+256*mainmemory.read_u8(0x1733+shot_slot)")[0])
        assert fx != fx_after
        checked("fireball moved under game simulation", {"x_before":fx,"x_after":fx_after})
        # With both supported fireball slots occupied, do not spill into the
        # other eight extended slots (their player-fireball OAM offsets are
        # unaligned). This is the regression for the visible garbage pixels.
        client.eval("smw.autofire(0); saved_ext={}; for s=0,9 do saved_ext[s]=mainmemory.read_u8(0x170b+s) end; mainmemory.write_u8(0x1713,5); mainmemory.write_u8(0x1714,5)")
        assert values("return smw.fireball()") == [None]
        assert values("local unchanged=true; for s=0,7 do unchanged=unchanged and mainmemory.read_u8(0x170b+s)==saved_ext[s] end; return unchanged") == [True]
        client.eval("for s=0,9 do mainmemory.write_u8(0x170b+s,saved_ext[s]) end")
        checked("full stock fireball pool never spills into unsupported OAM slots")
        rates = {}
        for interval in (48,4):
            client.eval(f"smw.autofire(0); for s=0,9 do if mainmemory.read_u8(0x170b+s)==5 then mainmemory.write_u8(0x170b+s,0) end end; smw.shots=0; smw.missed_shots=0; smw.autofire({interval})")
            client.step(96)
            rates[interval] = list(map(int, values("return smw.shots,smw.missed_shots")))
            assert values("local valid=true; for s=0,7 do valid=valid and mainmemory.read_u8(0x170b+s)~=5 end; return valid") == [True]
        assert rates[48][0] == 2 and rates[4][0] > rates[48][0], rates
        assert values("return demo_hit") == [True], "fireballs did not convert the spawned enemy to a coin"
        checked("stock fireball collision reaches the allocated enemy slot")
        checked("changing fire cadence changes real projectile creation", rates)
        client.eval("smw.stop()")
        client.command("reset")
        assert values("return smw,counter,joypad.get(1).Y") == [None,None,False]
        client.step(2)
        checked("VM reset clears scripts, callbacks and input overrides")
        client.eval((ROOT / "tools/lua/smw.lua").read_text())
        checked("final playable state", values("return smw.status()"))
        report["ok"] = True
    finally:
        if client: client.close()
        if not args.keep_running or not report.get("ok"):
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        log.close()
        report_path = exe.parent / "lua-validation.json"
        report_path.write_text(json.dumps(report, indent=2)+"\n")
        print(f"Report: {report_path}", flush=True)


if __name__ == "__main__":
    main()
