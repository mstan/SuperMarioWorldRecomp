#!/usr/bin/env python3
"""Run one finite, muted native co-op bonus-game alpha probe.

The source snapshot is the retained keyhole/tape fixture. It stages the normal
keyhole and goal contacts, but the probe never writes bonus completion, symbol,
block-stop, life, or exit outcome fields. Gameplay inputs drive the native
transition and the bonus-game jumps. All output is isolated under --out.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def make_script(path, from_room_snapshot):
    lines = ['wait 60', 'loadstate 10']
    if from_room_snapshot:
        lines.append('wait 20')
    else:
        lines.extend(('wait 500', 'press right 40', 'press p2.right 40'))
    # Hold jump through the normal variable-height window, then release and
    # allow a full landing before the next ordinary jump.
    for i in range(40):
        lines.extend(('press b+p2.b 16', 'wait 36'))
        if i == 19:
            lines.append('savestate 8')
    lines.extend(('wait 500', 'savestate 9'))
    path.write_text('\n'.join(lines) + '\n', encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--room-snapshot', type=Path, required=True,
                        help='Use a snapshot produced by the native entry run; actors are positioned offline under separate columns')
    args = parser.parse_args()
    exe = ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe'
    rom = ROOT/'smw.sfc'
    source = args.room_snapshot.resolve()
    runner = ROOT/'tools/run_coop_probe.py'
    out = args.out.resolve()
    if out.exists():
        parser.error(f'output directory already exists: {out}')
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    out.parent.mkdir(parents=True, exist_ok=True)
    script = out.with_suffix('.script')
    snapshot = source
    if args.room_snapshot:
        from coop_fixture import Fixture
        fixture = Fixture(source, game_mode=None)
        ram = fixture.ram
        if fixture.level != 0x100 or fixture.data[ram+0x188f] or fixture.data[ram+0x1890]:
            parser.error('room snapshot is not an untouched, in-progress native bonus room')
        if any(fixture.data[ram+0xc3:ram+0xcb]):
            parser.error('room snapshot already has a stopped playable block')
        for player, x in ((0, 80), (1, 160)):
            guest, record = fixture.actors[player]
            # actor() takes native top-left coordinates; trace Y is the
            # collision center. Preserve the actual native standing Y.
            y = (fixture.data[guest+fixture.offsets[0x96]] |
                 (fixture.data[guest+fixture.offsets[0x97]] << 8))
            fixture.actor(player, x, y, fixture.word(record+12),
                          fixture.word(record+16))
        fixture.camera(0)
        snapshot = out.with_suffix('.stage.sav')
        fixture.save(snapshot)
    make_script(script, True)
    command = [sys.executable, str(runner), '--exe', str(exe), '--rom', str(rom),
               '--snapshot', str(snapshot), '--script', str(script), '--out', str(out),
               '--frames', '2800', '--native', '--wram-watch', 'c2:cb', '--framedump', '1600']
    # The probe runner supplies dummy SDL video/audio devices and a private
    # settings/save tree. The process runs a finite number of frames.
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode:
        raise SystemExit(completed.returncode)
    rows = list(csv.DictReader((out/'coop.csv').open(newline='', encoding='utf-8')))
    gameplay = [{k: int(v) for k, v in row.items()}
                for row in rows if row['level'] == '256']
    jump_evidence = {}
    for player in (0, 1):
        actor_rows = [row for row in gameplay if row['player'] == player]
        takeoffs = sum(a['y'] >= 387 and b['y'] < 387
                       for a, b in zip(actor_rows, actor_rows[1:]))
        abrupt_reversals = []
        for i in range(1, len(actor_rows)-1):
            rising = actor_rows[i]['y']-actor_rows[i-1]['y'] < 0
            falling = actor_rows[i+1]['y']-actor_rows[i]['y'] >= 8
            if rising and falling:
                abrupt_reversals.append(actor_rows[i+1]['frame'])
        jump_evidence[str(player)] = {
            'rows': len(actor_rows),
            'x_min': min(row['x'] for row in actor_rows),
            'x_max': max(row['x'] for row in actor_rows),
            'y_min': min(row['y'] for row in actor_rows),
            'takeoffs_from_floor': takeoffs,
            'abrupt_up_to_down_reversals': abrupt_reversals,
        }
    from coop_fixture import Fixture
    saved = out/'saves/native-coop-v1/save9.sav'
    final = Fixture(saved, game_mode=None)
    middle = Fixture(out/'saves/native-coop-v1/save8.sav', game_mode=None)
    ram = final.ram
    # These addresses are reused after the bonus exits. Do not report another
    # room's bytes as stopped bonus blocks or cleared bonus outcomes.
    still_in_bonus = final.level == 0x100 and final.data[ram+0x100] == 0x14
    middle_in_bonus = middle.level == 0x100 and middle.data[middle.ram+0x100] == 0x14
    block_stops = list(final.data[ram+0xc3:ram+0xcb]) if still_in_bonus else None
    middle_stops = list(middle.data[middle.ram+0xc3:middle.ram+0xcb]) if middle_in_bonus else None
    contact_rows = list(csv.DictReader((out/'contacts.csv').open(newline='', encoding='utf-8')))
    bonus_contact_responses = []
    for before, after in zip(contact_rows[::2], contact_rows[1::2]):
        if (before['phase'] == '0' and after['phase'] == '1'
                and before['frame'] == after['frame']
                and before['type'] == '130' and after['type'] == '130'
                and int(before['player_vy']) < 0 and int(after['player_vy']) == 16):
            bonus_contact_responses.append({'frame': int(before['frame']),
                                            'player': int(before['player']),
                                            'slot': int(before['slot'])})
    native_stops = [int(value, 16) for value in re.findall(
        r'^f\d+\s+\w+:00C[3-9A]=01 w1 .*?\bX=([0-9A-F]{4})\b.*?\bIPC=01DE7C\b',
        (out/'wram-writes.log').read_text(), re.M)]
    primary_rows = [r for r in gameplay if r['player'] == 0]
    life_changes = [{'frame': b['frame'], 'level': b['level'],
                     'before': a['lives'], 'after': b['lives']}
                    for a, b in zip(primary_rows, primary_rows[1:])
                    if a['lives'] != b['lives']]
    source_state = Fixture(source, game_mode=None)
    life_delta = final.data[ram+0x0dbe] - source_state.data[source_state.ram+0x0dbe]
    participants = sorted({r['player'] for r in bonus_contact_responses})
    observed_exit = final.level != 0x100 or final.data[ram+0x100] == 0x0e
    passed = (sorted(native_stops) == list(range(1, 9)) and participants == [0, 1]
              and observed_exit and 0 <= life_delta <= 8)
    summary = {
        'passed': passed,
        'exe_sha256': digest,
        'source_snapshot': str(source),
        'source_snapshot_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'probe_returncode': completed.returncode,
        'actor_rows': len(rows),
        'gameplay_rows': sum(r['mode'] == '20' for r in rows),
        'levels_seen': sorted(set(r['level'] for r in rows)),
        'bonus_room_actor_jump_evidence': jump_evidence,
        'final_actor_rows': rows[-2:] if len(rows) >= 2 else rows,
        'bonus_game_complete': final.data[ram+0x188f] if still_in_bonus else None,
        'bonus_game_1up_count': final.data[ram+0x1890] if still_in_bonus else None,
        'stopped_block_c2_slots_1_to_8': block_stops,
        'stopped_block_count': sum(value != 0 for value in block_stops) if block_stops is not None else None,
        'midpoint_stopped_block_c2_slots_1_to_8': middle_stops,
        'midpoint_stopped_block_count': sum(value != 0 for value in middle_stops) if middle_stops is not None else None,
        'native_stopped_slots': native_stops,
        'participating_players': participants,
        'observed_bonus_exit': observed_exit,
        'shared_life_delta': life_delta,
        'life_changes': life_changes,
        'bonus_contact_responses': bonus_contact_responses,
        'raw_lives_counter': final.data[ram+0x0dbe],
        'c2_write_log': str(out/'wram-writes.log'),
        'snapshot_after_play': str(out/'saves/native-coop-v1/save9.sav'),
    }
    (out/'bonus-alpha-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))
    return int(not passed)


if __name__ == '__main__':
    raise SystemExit(main())
