#!/usr/bin/env python3
"""One muted Morton encounter: actual alternating shots, defeat and scene exit.

The input is an existing original-loader boss-entry state. Stage prerequisites
were bypassed there. Between short shooting intervals only player protection
is refreshed; boss health, state, rewards and exit flags are never staged.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import struct
import sys

from coop_fixture import Fixture, ROOT
from smw_content_inventory import Stock


def prepare_overworld(f, rom):
    """Keep the cheated boss entrance attached to its actual stock map node."""
    stock = Stock(rom, ROOT / 'third_party/SMWDisX/SMW_U.sym')
    nodes = [i for i, tile in enumerate(stock.bytes(stock.symbols['OWL1TileData'], 0x800))
             if 0x56 <= tile < 0x81]
    offset = nodes[6]  # actual translevel 7: Morton's castle
    assert offset < 0x400
    x = ((offset & 15) | ((offset & 0x100) >> 4)) * 16 + 8
    y = (((offset >> 4) & 15) | ((offset & 0x200) >> 5)) * 16 + 8
    for address, value in ((0x1f17, x), (0x1f19, y)):
        struct.pack_into('<H', f.data, f.ram + address, value)
    return {'translevel': 7, 'map_offset': offset, 'party_x': x, 'party_y': y}


def observe(path):
    f = Fixture(path, game_mode=None)
    r = f.ram
    return {
        'mode': f.data[r + 0x100], 'level': f.level,
        'end_timer': f.data[r + 0x1493], 'boss_status': f.data[r + 0x14c8 + 9],
        'boss_phase': f.data[r + 0x151c + 9], 'boss_hits': f.data[r + 0x1626 + 9],
        'castle_flag': f.data[r + 0x1ea2 + 7],
        'lives': f.word(f.core + 36),
        'actors': {p: {'life': f.word(a + 8), 'power': f.word(a + 12)}
                   for p, (_, a) in f.actors.items()},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build-adaptive/playtest/goal-20261019/projectiles/acceptance-first/morton-entry-native/saves/native-coop-v1/save11.sav')
    parser.add_argument('--exe', type=Path, default=ROOT / 'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    parser.add_argument('--rom', type=Path, default=ROOT / 'smw.sfc')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source = args.source.resolve()
    assert observe(source)['level'] == 0xe5
    f = Fixture(source)
    context = prepare_overworld(f, args.rom)
    source = root / 'entry-with-map-context.sav'
    f.save(source)
    (root / 'map-context.json').write_text(json.dumps(context, indent=2) + '\n')
    results, contacts = [], []

    def run(name, source, text, frames, capture):
        script = root / f'{name}.script'
        script.write_text(text, encoding='ascii', newline='\n')
        out = root / name
        command = [sys.executable, str(ROOT / 'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(args.rom.resolve()),
                   '--snapshot', str(source), '--script', str(script),
                   '--out', str(out), '--frames', str(frames), '--native',
                   '--projectile-trace', '--capture', str(capture),
                   '--framedump', str(capture)]
        completed = subprocess.run(command, cwd=ROOT, check=False)
        report = json.loads((out / 'result.json').read_text())
        assert completed.returncode == 0 and report['returncode'] == 0, report
        assert not report['timed_out'] and not report['dispatch_misses'], report
        saved = out / 'saves/native-coop-v1/save9.sav'
        result = dict(name=name, snapshot=str(saved), state=observe(saved))
        results.append(result)
        log = out / 'projectiles.csv'
        if log.exists():
            contacts.extend(dict(row, run=name) for row in csv.DictReader(log.open()))
        (root / 'observations.json').write_text(json.dumps({
            'scope': 'one encounter; stage prerequisites bypassed; player protection refreshed',
            'checks': results, 'contacts': contacts,
        }, indent=2) + '\n')
        print(json.dumps(result), flush=True)
        return saved

    for interval in range(6):
        f = Fixture(source)
        assert all(f.word(a + 12) == 3 for _, a in f.actors.values()), 'Fire power lost during protected shooting interval'
        for player, (guest, _) in f.actors.items():
            f.data[guest + f.offsets[0x1497]] = 255
            if player == f.word(f.core + 20):
                f.data[f.ram + 0x1497] = 255
        prepared = root / f'shoot-{interval}.sav'
        f.save(prepared)
        text = 'wait 60\nloadstate 10\n'
        for shot in range(14):
            buttons = 'y' if shot % 2 == 0 else 'p2.y'
            text += f'press {buttons} 1\nwait 15\n'
        text += 'savestate 9\nwait 5\n'
        # Script entries consume an advance frame as well as hold/wait time.
        # Leave room for the trailing snapshot after the fourteenth shot.
        source = run(f'shoot-{interval}', prepared, text, 330, 280)
        state = observe(source)
        if state['boss_phase'] >= 4 or state['end_timer'] or state['mode'] != 0x14:
            break
    else:
        raise AssertionError('One encounter did not reach original boss defeat within six shooting intervals')

    accepted = [c for c in contacts if c['accepted'] == '1']
    assert len(accepted) == 12, accepted
    assert {c['actor'] for c in accepted} == {'0', '1'}, accepted
    source = run('defeat-transition', source,
                 'wait 60\nloadstate 10\nwait 1600\npress b 1\nwait 600\n'
                 'press b 1\nwait 400\nsavestate 9\nwait 5\n', 2680, 2650)
    final = observe(source)
    assert final['mode'] == 0x0e, final
    assert all(p['life'] == 0 for p in final['actors'].values()), final
    assert final['castle_flag'] != observe(args.source)['castle_flag'], final
    assert final['lives'] == observe(args.source)['lives'], final
    summary = {'passed': True, 'scope': 'one Morton defeat/overworld transition',
               'accepted_fire_hits': len(accepted), 'participants': [0, 1],
               'final': final, 'intervals': len(results) - 1}
    (root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
