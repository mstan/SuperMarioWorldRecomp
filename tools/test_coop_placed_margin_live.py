#!/usr/bin/env python3
"""Probe native placed-egg hatching at adaptive viewport margins.

The source must be the CNR8 ``placed-margin-waiting-before.sav`` state, with
slot 4 already waiting and its PEG1 pending-egg identity intact. Offline
positioning bypasses route/terrain qualification. Native egg activation,
contact and source accounting run in-game. The trace must establish the actual
native boundary and selected viewport before an activation result is accepted.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import struct
import sys

from coop_fixture import Fixture, ROOT
from test_coop_mouth_live import same, set_actor

SLOT = 4
EGG_X = 0x2C
EGG_Y = 352
WAITING_STATUS = 8


def state(out, number):
    return Fixture(out / f'saves/native-coop-v1/save{number}.sav')


def stage(source, output, case):
    f = Fixture(source)
    assert f.version == 8 and f.level == 0x106, (f.version, f.level)
    assert len(f.placed_eggs) == 1, f.placed_eggs
    base_record = f.placed_eggs[0]
    assert base_record[0] == 0x106 and base_record[4] == 0 and base_record[5] != 0, base_record
    identity = base_record[5]
    entity = f.entities.get(identity)
    assert entity and entity[2:4] == (SLOT, EGG_X), (identity, entity)
    r = f.ram
    assert f.data[r+0x14c8+SLOT] == WAITING_STATUS and f.data[r+0x187B+SLOT] == 1
    focus_offset = f.word(f.focus+4)-int.from_bytes(f.data[r+0x1a:r+0x1c], 'little')
    x, camera, actors = {
        'right': (288, 19, (272, 48)),
        'left': (128, 192, (112, 336)),
        'center': (128, 0, (112, 224)),
        'vertical': (288, 19, (272, 48)),
    }[case]
    # Preserve the waiting egg's native entity and PEG1 identity while moving
    # the source origin and physical slot together for the staged probe.
    for address, value in ((0xe4, x & 255), (0x14e0, x >> 8),
                           (0xd8, EGG_Y & 255), (0x14d4, EGG_Y >> 8)):
        f.data[r+address+SLOT] = value
    f.data[r+0x15A0+SLOT] = int(case in ('right', 'left', 'vertical'))
    f.camera(camera)
    # PEG1 records the original world-space origin used to recognize this
    # placed source on later frames. Move that test origin with the staged egg.
    assert len(f.placed_eggs) == 1, f.placed_eggs
    placed_record = f.placed_at + 8
    f.put(placed_record + 4, x)
    f.put(placed_record + 8, 32 if case == 'vertical' else EGG_Y)
    if case == 'vertical':
        # Put the egg above the vertical viewport. Let the native per-frame
        # offscreen routine calculate $186C and verify it in contacts.csv.
        f.data[r + 0xD8 + SLOT] = 32
        f.data[r + 0x14D4 + SLOT] = 0
        f.data[r + 0x1C:r + 0x1E] = (256).to_bytes(2, 'little')
        f.data[r + 0x1464:r + 0x1466] = (128).to_bytes(2, 'little')
        # Seed the matching vertical offscreen bit before the first gameplay
        # update so stale zero flags cannot allow a one-frame hatch.
        f.data[r + 0x186C + SLOT] |= 0x01
    for player, actor_x in enumerate(actors):
        set_actor(f, player, actor_x, EGG_Y, False)
    # A RAM camera relocation alone is inconsistent with the serialized focus
    # and is immediately corrected by native camera code. Stage a held focus
    # with the same measured focus-to-camera offset; this tests visibility,
    # not how gameplay establishes that camera position.
    f.put(f.focus+4, camera+focus_offset)
    f.put(f.focus+12, sum(actors)//2+8)
    f.put(f.focus+20, 2)
    f.put(f.focus+24, 1)
    f.put(f.focus+28, 1)
    f.save(output)
    return dict(case=case, egg_x=x,
                camera_x=camera,
                expected_relative_x=x-camera,
                player_x=actors[0], staged_status=f.data[r+0x14c8+SLOT],
                staged_187b=f.data[r+0x187B+SLOT],
                staged_15a0=f.data[r+0x15A0+SLOT],
                staged_186c=f.data[r+0x186C+SLOT])


def run(args, source, name, narrow, bounce):
    out = args.out.resolve() / name
    script = args.out.resolve() / f'{name}.script'
    # Two frames keep the native-width camera from scrolling this initially
    # offscreen egg into view. A longer wait tests a different visibility state.
    script.write_text('wait 60\nloadstate 10\nwait 2\nsavestate 9\n'
                      'wait 5\nloadstate 10\nwait 2\nsavestate 8\nwait 5\n',
                      encoding='ascii', newline='\n')
    command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
               '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
               '--snapshot', str(source.resolve()), '--script', str(script),
               '--out', str(out), '--frames', '90', '--framedump', '67',
               '--wram-watch', '186c:1870']
    if narrow:
        command.append('--native')
    if bounce:
        command.append('--bounce')
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    report = json.loads((out/'result.json').read_text())
    assert report['returncode'] == 0 and not report['timed_out'] and not report['dispatch_misses'], out
    return out


def trace_rows(out):
    with (out/'contacts.csv').open(newline='') as stream:
        return [row for row in csv.DictReader(stream) if int(row['slot']) == SLOT]


def viewport_for_camera(out, camera):
    if not (out/'frames.csv').exists():
        # Native rendering has no adaptive diagnostic CSV. Its actual frame
        # capture still proves the produced width; there is no margin offset.
        bitmap = (out/'frames/frame_000067.bmp').read_bytes()
        assert bitmap[:2] == b'BM' and struct.unpack_from('<i', bitmap, 18)[0] == 256
        return dict(frame=67, camera=camera, width=256, native_offset=0)
    with (out/'frames.csv').open(newline='') as stream:
        frames = [row for row in csv.DictReader(stream) if int(row['mode']) == 0x14]
    assert frames, f'{out}: renderer emitted no actual viewport frames'
    frame = min(frames, key=lambda row: abs(int(row['simulation_camera']) - camera))
    return dict(frame=int(frame['frame']), camera=int(frame['simulation_camera']),
                width=int(frame['width']), native_offset=int(frame['native_offset']))


def validate_trace(out, staged, case, narrow):
    rows = trace_rows(out)
    assert rows, f'{out}: no native contact trace for egg slot {SLOT}'
    matches = [row for row in rows
               if int(row['status']) == WAITING_STATUS
               and int(row['sprite_x']) == staged['egg_x']]
    assert matches, (out, rows[:3], staged)
    first = matches[0]
    relative_x = int(first['sprite_x']) - int(first['camera_x'])
    if case in ('right', 'vertical'):
        assert relative_x >= 256, (out, first)
    elif case == 'left':
        assert relative_x < 0, (out, first)
    else:
        assert 0 <= relative_x < 256, (out, first)
    assert abs(int(first['sprite_x']) - int(first['player_x'])) <= 24, (out, first)
    if case in ('right', 'left', 'vertical'):
        assert int(first['offscreen_x']) != 0, (out, first)
    viewport = viewport_for_camera(out, int(first['camera_x']))
    left = -viewport['native_offset']
    right = viewport['width'] - viewport['native_offset']
    visible = left <= relative_x < right
    if case in ('right', 'left', 'vertical'):
        assert visible == (not narrow), (out, first, viewport, visible)
    elif case == 'center':
        assert visible, (out, first, viewport)
    if case == 'vertical':
        assert int(first['offscreen_y']) != 0, (out, first)
        assert not 0 <= int(first['sprite_y']) - int(first['camera_y']) < 224, (out, first)
        vertical_rows = [row for row in matches
                         if not 0 <= int(row['sprite_y']) - int(row['camera_y']) < 224]
        distinct_frames = {int(row['frame']) for row in vertical_rows
                           if int(row['offscreen_y']) != 0}
        assert len(distinct_frames) >= 2, (out, first, vertical_rows[:4])
        assert state(out, 8).data[state(out, 8).ram+0x186C+SLOT] != 0, out
    return dict(trace_rows=len(rows), first_frame=first['frame'],
                offscreen_x=int(first['offscreen_x']), offscreen_y=int(first['offscreen_y']),
                camera_x=int(first['camera_x']), sprite_x=int(first['sprite_x']),
                relative_x=relative_x, viewport=viewport, visible_in_viewport=visible)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path, help='CNR8 placed-margin-waiting-before.sav waiting-egg state')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    ap.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    ap.add_argument('--expect-current-gap', action='store_true',
                    help='expect the wide right-margin waiting egg to remain unhatched')
    ap.add_argument('--cases', nargs='+', choices=('right', 'left', 'center', 'vertical'),
                    default=('right', 'left', 'center', 'vertical'),
                    help='select staged visibility probes')
    args = ap.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    staged = {}
    cases = args.cases
    for case in cases:
        path = root/f'{case}.sav'
        staged[case] = stage(args.source, path, case)

    results, parity = [], {}
    for case in cases:
        for narrow in (True, False):
            # At native width the camera refits the left-side active actor
            # into view. The centered control qualifies the inactive hook;
            # the left-margin scenario exists only in the wider viewport.
            if case == 'left' and narrow:
                continue
            for bounce in (False, True):
                view = 'standard' if narrow else 'wide'
                backend = 'bounce' if bounce else 'native'
                name = f'{case}-{view}-{backend}'
                out = run(args, root/f'{case}.sav', name, narrow, bounce)
                first, replay = state(out, 9), state(out, 8)
                same(first, replay)
                parity_key = (case, view)
                if bounce:
                    same(first, parity[parity_key])
                else:
                    parity[parity_key] = first
                assert len(first.placed_eggs) == len(replay.placed_eggs) == 1, out
                source_record = first.placed_eggs[0]
                assert source_record[:3] == (0x106, staged[case]['egg_x'],
                                              EGG_Y if case != 'vertical' else 32), (out, source_record)
                assert source_record[3] == 255, (out, source_record)
                margin = case in ('right', 'left')
                expect_hatch = (case == 'center' or (margin and not narrow and not args.expect_current_gap))
                if case == 'vertical':
                    expect_hatch = False
                uses, identity = source_record[4:6]
                if expect_hatch is True:
                    assert uses == 1, (out, source_record, expect_hatch)
                elif expect_hatch is False:
                    assert uses == 0 and identity != 0, (out, source_record, expect_hatch)
                else:
                    assert uses in (0, 1) and (uses != 0 or identity != 0), (out, source_record)
                trace = validate_trace(out, staged[case], case, narrow)
                results.append(dict(case=case, view=view, backend=backend,
                                    status='passed',
                                    expected_hatch=expect_hatch,
                                    source_uses=uses, egg_identity=identity, trace=trace,
                                    fixture_qualification='staged-probe'))
                (root/'summary.json').write_text(json.dumps(dict(
                    checks=results,
                    scope='staged viewport activation probes; not route/terrain qualification',
                    left_margin='staged position; actual visibility and native boundary asserted',
                    current_gap_mode=args.expect_current_gap), indent=2)+'\n')
    print(f'{len(results)} placed-egg viewport probes passed with replay parity')


if __name__ == '__main__':
    main()
