#!/usr/bin/env python3
"""Muted checks for mounted recovery beside another rider.

Positions, waiting catch-up state/delay, and obstruction tiles are staged
offline. Native gameplay must perform safe placement; no recovery result is
written. Room tests preserve real mouth contents from the input state. Pipe
tests exercise the original transition and its normal mouth-clear rule.
These checks do not qualify the catch-up trigger or campaign terrain.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from test_coop_mount_transport_live import ids, pipe
from test_coop_mouth_live import same, set_actor


def state(out, slot):
    return Fixture(out/f'saves/native-coop-v1/save{slot}.sav', game_mode=None)


def obstruction(f, solid):
    rom = (ROOT/'smw.sfc').read_bytes()
    assert f.level == 0x106 and not f.data[f.ram+0x5b]
    # Known YI2 entrance patch: standing space at x=96 is untouched.
    for x in (*range(16, 81, 16), *range(128, 177, 16)):
        for y in range(336, 384, 16):
            at = rom[0x3a60]+(rom[0x3a9c] << 8)+(y & 0xf0)+(x >> 4)+(y & 0xff00)
            block = 0x130 if solid else 0x25
            f.data[f.ram+at] = block & 255
            f.data[f.ram+at+0x10000] = block >> 8


def room(source, output, bubble, blocked):
    f = Fixture(source)
    for p, identity in ids(f).items():
        set_actor(f, p, 96, 336, True)
        slot = f.entities[identity][2]
        for address, value in ((0xe4, 96), (0x14e0, 0), (0xd8, 96), (0x14d4, 1)):
            f.data[f.ram+address+slot] = value
    f.camera(0)
    f.put(f.focus+24, 0)
    f.put(f.focus+28, 0)
    if blocked:
        obstruction(f, True)
    waiting(f, bubble, 30)
    f.save(output)


def waiting(f, bubble, delay):
    guest, record = f.actors[bubble]
    f.put(record+8, 3)
    f.put(record+44, delay)
    f.data[guest+f.offsets[0x78]] = 255
    if bubble == 0:
        f.data[f.ram+0x78] = 255


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--cases', nargs='+', choices=('room', 'pipe', 'blocked'),
                    default=('room', 'pipe', 'blocked'))
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    original = Fixture(args.source)
    expected = ids(original)
    assert set(expected) == {0, 1} and original.level == 0x106
    results, parity = [], {}

    def run(name, snapshot, script, frames, narrow, bounce):
        path = root/f'{name}.script'
        path.write_text(script, encoding='ascii', newline='\n')
        out = root/name
        command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(snapshot.resolve()), '--script', str(path),
                   '--out', str(out), '--frames', str(frames), '--framedump', str(frames-10)]
        if narrow:
            command.append('--native')
        if bounce:
            command.append('--bounce')
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        report = json.loads((out/'result.json').read_text())
        assert report['returncode'] == 0 and not report['dispatch_misses'] and not report['timed_out'], name
        return out

    def record(key, name, out, slots, bounce):
        snapshots = [state(out, slot) for slot in slots]
        if bounce:
            for a, b in zip(snapshots, parity[key]):
                same(a, b)
        else:
            parity[key] = snapshots
        results.append(dict(case=key, bounce=bounce, status='passed', evidence=name,
                            parity_snapshots=slots))
        (root/'summary.json').write_text(json.dumps(dict(
            checks=results, campaign='unverified', catchup_trigger='staged, not tested'), indent=2)+'\n')

    def recovered(f, bubble):
        assert ids(f) == expected
        record = f.actors[bubble][1]
        anchor = f.actors[1-bubble][1]
        assert f.word(record+8) == 0 and not f.word(record+44)
        assert f.word(record+12) == original.word(original.actors[bubble][1]+12)
        assert f.word(record+40) == 0, 'catch-up must not grant damage protection'
        assert abs(f.word(record+20)-f.word(anchor+20)) >= 36
        assert f.word(record+24) == f.word(anchor+24)
        assert all(f.data[g+f.offsets[0x187a]] for g, _ in f.actors.values())

    for bounce in (False, True):
        for narrow in (True, False):
            width = 'standard' if narrow else 'wide'
            backend = 'bounce' if bounce else 'native'
            for bubble in (0, 1):
                for kind in args.cases:
                    key = f'{kind}-bubble{bubble}-{width}'
                    name = f'{key}-{backend}'
                    fixture = root/f'{name}.sav'
                    if kind == 'pipe':
                        intermediate = root/f'{name}-position.sav'
                        pipe(args.source, intermediate, 1-bubble)
                        f = Fixture(intermediate)
                        waiting(f, bubble, 180)
                        f.save(fixture)
                        button = 'down' if bubble == 1 else 'p2.down'
                        script = (f'wait 60\nloadstate 10\npress {button} 80\nwait 1\nsavestate 6\n'
                                  'wait 80\nsavestate 9\nwait 180\nsavestate 8\n'
                                  'wait 10\nsavestate 7\nloadstate 8\nwait 10\nsavestate 5\nwait 5\n')
                        out = run(name, fixture, script, 450, narrow, bounce)
                        ready, waiting_state, final = (state(out, slot) for slot in (6, 9, 8))
                        assert ready.entrance_prepared and ready.level == 0x1ca
                        assert waiting_state.level == final.level == 0x1ca
                        for f in (ready, waiting_state):
                            record_at = f.actors[bubble][1]
                            assert f.word(record_at+8) == 3 and f.word(record_at+44) > 150
                            assert ids(f) == expected
                        recovered(final, bubble)
                        for identity in expected.values():
                            assert final.data[final.ram+0x160e+final.entities[identity][2]] == 255
                        same(state(out, 7), state(out, 5))
                        record(key, name, out, (6, 9, 8, 7), bounce)
                        continue

                    room(args.source, fixture, bubble, kind == 'blocked')
                    out = run(name, fixture,
                              'wait 60\nloadstate 10\nwait 10\nsavestate 9\nwait 30\nsavestate 8\n'
                              'loadstate 9\nwait 30\nsavestate 7\nwait 5\n',
                              150, narrow, bounce)
                    first, final = state(out, 9), state(out, 8)
                    identity = expected[bubble]
                    mouth = original.data[original.ram+0x160e+original.entities[identity][2]]
                    assert mouth < 12 and first.word(first.actors[bubble][1]+8) == 3
                    assert first.mounts[identity][5] == original.mounts[identity][5]
                    assert first.mounts[expected[1-bubble]][5] < original.mounts[expected[1-bubble]][5]
                    assert final.data[final.ram+0x160e+final.entities[identity][2]] == mouth
                    assert final.data[final.ram+0x14c8+mouth] == 7
                    same(final, state(out, 7))
                    if kind == 'room':
                        recovered(final, bubble)
                        assert final.mounts[identity][5] < first.mounts[identity][5], 'mouth timer did not resume'
                    else:
                        assert final.word(final.actors[bubble][1]+8) == 3
                        assert final.word(final.actors[bubble][1]+44) == 0
                        assert final.mounts[identity][5] == original.mounts[identity][5]
                    record(key, name, out, (9, 8), bounce)
                    if kind == 'blocked':
                        clear = root/f'{name}-clear.sav'
                        obstruction(final, False)
                        final.save(clear)
                        key = f'clear-bubble{bubble}-{width}'
                        name = f'{key}-{backend}'
                        out = run(name, clear,
                                  'wait 60\nloadstate 10\nwait 10\nsavestate 9\n'
                                  'wait 10\nsavestate 8\nloadstate 9\nwait 10\nsavestate 7\nwait 5\n',
                                  110, narrow, bounce)
                        recovered(state(out, 9), bubble)
                        same(state(out, 8), state(out, 7))
                        record(key, name, out, (9, 8), bounce)
    print(f'{len(results)} mounted recovery checks passed with parity and exact replay')


if __name__ == '__main__':
    main()
