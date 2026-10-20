#!/usr/bin/env python3
"""Small native co-op enemy/ability follow-up; staged inputs, real outcomes.

The default is a bounded set of representative encounters, not a product of
players, render modes or execution paths. Original ROM tweakers are used.
Private inputs, snapshots, traces and failed attempts stay under --out.
"""
import argparse
import csv
import json
import re
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT
from make_coop_carry_fixture import loose_key

CASES = ('cape-mario', 'cape-luigi', 'chuck-side', 'chuck-stomp',
         'boo-watched', 'boo-unwatched', 'chuck-target', 'star', 'spin',
         'platform-riders', 'platform-recovery', 'water-recovery',
         'pipe-mario', 'door-luigi', 'hazard-fire', 'hazard-hammer-star')


def native_cape_hits(out):
    # Interpreted writes name the exact IPC; compiled writes identify their
    # emitted original routine instead. Both must be the actual timer writer.
    return sum(bool(re.search(r'\b154C=08\b', line) and
                    (re.search(r'\bIPC=029406\b', line) or
                     'CheckPlayerAttackToNormalSpriteColl_029404_' in line))
               for line in (out/'wram-writes.log').read_text().splitlines())


def field(f, player, address, value=None):
    at = f.actors[player][0] + f.offsets[address]
    if value is None:
        return f.data[at]
    f.data[at] = value
    if player == f.word(f.core+20):
        f.data[f.ram+address] = value


def sprite(f, kind, x, y):
    slot = loose_key(f, x, y, slot=0)
    rom = (ROOT/'smw.sfc').read_bytes()
    for table, original in ((0x1656, 0x3f26c), (0x1662, 0x3f335),
                            (0x166e, 0x3f3fe), (0x167a, 0x3f4c7),
                            (0x1686, 0x3f590), (0x190f, 0x3f659)):
        f.data[f.ram+table+slot] = rom[original+kind]
    for address, value in ((0x9e, kind), (0x14c8, 8), (0x1588, 4),
                           (0x15f6, f.data[f.ram+0x166e] & 15)):
        f.data[f.ram+address+slot] = value


def tile(f, x, y, block):
    rom=(ROOT/'smw.sfc').read_bytes()
    assert not f.data[f.ram+0x5b] & 1
    screen=x>>8
    at=rom[0x3a60+screen]+256*rom[0x3a9c+screen]+(y&0xf0)+((x&255)>>4)+(y&0xff00)
    f.data[f.ram+at]=block&255
    f.data[f.ram+at+0x10000]=block>>8


def make(source, output, case):
    if case in ('star', 'spin'):
        from make_coop_arbitration_fixture import make as arbitration
        return arbitration(source, output, 'star' if case=='star' else 'spiny', 192)
    if case.startswith('hazard-'):
        from test_coop_extended_live import make as extended
        extended(source,output,2 if case=='hazard-fire' else 4,
                 'secondary' if case=='hazard-fire' else 'star-first',
                 'right' if case=='hazard-fire' else None)
        return 'wait 60\nloadstate 10\nwait 2\nsavestate 11\nwait 5\n'
    if case in ('pipe-mario','door-luigi'):
        from make_coop_pipe_fixture import make as pipe
        pipe(source,output,'0' if case=='pipe-mario' else '1')
        f=Fixture(output)
        if case=='door-luigi':
            f.actor(1,3888,320,3,4)
            # Substitute an ordinary door at the real screen-15 pipe route.
            # Destination tables are untouched; this is a door-mechanic check,
            # not a claim that the stock level has a door here. VRAM is not rebuilt.
            tile(f,3888,320,0x25)
            tile(f,3888,336,0x20)
            tile(f,3888,352,0x130)
            staged=output.with_suffix('.door.sav')
            f.save(staged)
            output.write_bytes(staged.read_bytes())
        return ('wait 60\nloadstate 10\npress '+('down' if case=='pipe-mario' else 'p2.up')+
                ' 80\nwait 100\nsavestate 11\nwait 5\n')
    f = Fixture(source)
    assert f.level == 0x106 and not f.mounts
    f.data[f.ram+0x14c8:f.ram+0x14d4] = bytes(12)
    for player in f.actors:
        f.actor(player, 24, 352, 1, player+1)
        for address, value in ((0x1490, 0), (0x1497, 0), (0x140d, 0),
                               (0x13e8, 0), (0x14a6, 0), (0x76, 1)):
            field(f, player, address, value)
    if case.startswith('platform-'):
        sprite(f,0x55,80,320)
        f.actor(0,88,289,1,1)
        f.actor(1,112,289,1,2,life=3 if case=='platform-recovery' else 0,
                recovery=2 if case=='platform-recovery' else 0)
        script='wait 18\n'
    elif case=='water-recovery':
        for x in range(48,192,16):
            for y in range(288,432,16):
                tile(f,x,y,0)
        f.actor(0,88,336,1,1)
        f.actor(1,136,336,1,2,life=3,recovery=2)
        script='wait 18\n'
    elif case.startswith('cape-'):
        actor = int(case == 'cape-luigi')
        f.actor(actor, 100, 352, 2, actor+1)
        sprite(f, 5, 116, 368)
        script = f'press p{actor+1}.y 18\n'
    elif case.startswith('chuck-'):
        for actor in f.actors:
            f.actor(actor, 100, 312 if case == 'chuck-stomp' else 352, 1, actor+1)
            if case == 'chuck-stomp':
                for address, value in ((0x72, 0x24), (0x77, 0), (0x7d, 0x20)):
                    field(f, actor, address, value)
        sprite(f, 0x91, 108, 352)
        if case=='chuck-target':
            f.actor(0, 24, 352, 1, 1)
            f.actor(1, 230, 352, 1, 2)
        script = 'wait 18\n'
    else:
        # The nearest player watches the ghost in one encounter and looks
        # away in the other; the farther player faces the opposite way.
        nearest = int(case == 'boo-unwatched')
        f.actor(nearest, 100, 352, 1, nearest+1)
        f.actor(1-nearest, 230, 352, 1, 2-nearest)
        field(f, nearest, 0x76, int(case == 'boo-watched'))
        field(f, 1-nearest, 0x76, int(case != 'boo-watched'))
        sprite(f, 0x37, 148, 332)
        script = 'wait 18\n'
    f.camera(0)
    f.put(f.core+92, 256)
    for offset, value in ((4, 128), (12, 128), (16, 372), (20, 2), (24, 1), (28, 1)):
        f.put(f.focus+offset, value)
    f.save(output)
    return 'wait 60\nloadstate 10\n' + script + 'savestate 11\nwait 5\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'build-adaptive/playtest/mount-feedback/entrance-v5.sav')
    parser.add_argument('--exe', type=Path, default=ROOT/'build-adaptive/SuperMarioWorldSNESRecomp.exe')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    parser.add_argument('--bounce', action='store_true', help='One selected interpreter-path sample')
    parser.add_argument('--analyze-existing', action='store_true', help='Recheck retained observations without launching/repositioning')
    args = parser.parse_args()
    root = args.out.resolve()
    if args.analyze_existing:
        assert root.is_dir()
        previous=root/'results.json'
        backup=root/'results-before-reanalysis.json'
        if previous.exists() and not backup.exists():
            backup.write_bytes(previous.read_bytes())
    else:
        root.mkdir(parents=True, exist_ok=False)
    results = []
    for case in args.cases:
        snapshot, script = root/f'{case}.sav', root/f'{case}.script'
        if not args.analyze_existing:
            script.write_text(make(args.source, snapshot, case))
        out = root/case
        command = [sys.executable, str(ROOT/'tools/run_coop_probe.py'),
                   '--exe', str(args.exe.resolve()), '--rom', str(ROOT/'smw.sfc'),
                   '--snapshot', str(snapshot), '--script', str(script),
                   '--out', str(out), '--frames', '260' if case in ('pipe-mario','door-luigi') else '170' if case in ('star','spin') else '95',
                   '--framedump', '72', '--wram-watch', '1500:168f']
        if case!='hazard-fire':
            command.append('--native')
        if case.startswith('hazard-'):
            command.extend(('--extended-trace','--audit-queries'))
        elif case.startswith('chuck-'):
            command.append('--audit-queries')
        if args.bounce:
            command.append('--bounce')
        result = dict(case=case, status='failed')
        try:
            if not args.analyze_existing:
                subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            run=json.loads((out/'result.json').read_text())
            assert run['returncode']==0 and not run['dispatch_misses'] and not run['timed_out']
            f = Fixture(out/'saves/native-coop-v1/save11.sav')
            contact_trace=out/'contacts.csv'
            rows=list(csv.DictReader(contact_trace.open())) if contact_trace.is_file() else []
            arbitration=out/'arbitration.csv'
            attacks=list(csv.DictReader(arbitration.open())) if arbitration.is_file() else []
            frames=list(csv.DictReader((out/'coop.csv').open()))
            assert frames and all(int(r['stack'])==511 for r in frames)
            actors = {p: dict(power=field(f,p,0x19), animation=field(f,p,0x71),
                             spin=field(f,p,0x14a6), cape=field(f,p,0x13e8),
                             cape_x=field(f,p,0x13e9)+256*field(f,p,0x13ea),
                             cape_y=field(f,p,0x13eb)+256*field(f,p,0x13ec),
                             y=field(f,p,0x96)+256*field(f,p,0x97),
                             standing=field(f,p,0x1471),swimming=field(f,p,0x75),
                             life=f.word(f.actors[p][1]+8),
                             vy=field(f,p,0x7d)) for p in f.actors}
            result.update(actors=actors, sprite_status=f.data[f.ram+0x14c8],
                          sprite_type=f.data[f.ram+0x9e], state=f.data[f.ram+0xc2],
                          hits=f.data[f.ram+0x1528],
                          arbitration_rows=len(attacks),
                          bound_players=sorted({int(r['player']) for r in rows if r['slot']=='0'}))
            if case.startswith('platform-'):
                surface=f.data[f.ram+0xd8]+256*f.data[f.ram+0x14d4]
                result['platform_y']=surface
                assert all(a['life']==0 and a['standing'] and abs(a['y']+31-surface)<=1 for a in actors.values()), 'Both actors must stand/recover on the native platform'
                assert all(int(r['camera_x'])==0 for r in frames), 'Stationary-survivor camera jumped'
            elif case=='water-recovery':
                assert all(a['life']==0 and a['swimming'] for a in actors.values()), 'Water recovery/swimming did not complete'
            elif case in ('pipe-mario','door-luigi'):
                result['destination']=f.level
                assert f.level==0x1ca and f.data[f.ram+0x100]==0x14, 'Wrong destination or unfinished entrance'
                assert f.data[f.ram+0x141a]==1 and all(a['life']==0 and a['animation']==0 for a in actors.values())
                assert [a['power'] for a in actors.values()]==[1,3]
            elif case.startswith('hazard-'):
                extended=list(csv.DictReader((out/'extended.csv').open()))
                responses=[r for r in extended if r['phase']=='0']
                assert responses and all(int(r['query_audits'])>0 for r in responses), 'No audited native hazard response'
                result['hazard_players']=sorted({int(r['player']) for r in responses})
                assert result['hazard_players']==([1] if case=='hazard-fire' else [0,1])
                assert actors[0]['power']==1 and actors[1]['power']==0, 'Independent hazard/protection response differs'
                if case=='hazard-hammer-star':
                    assert f.data[f.ram+0x170b]==1, 'Native star did not destroy the hammer'
            elif case.startswith('cape-'):
                assert result['sprite_status'] == 9, 'Cape did not apply the original Koopa stun response'
                result['native_cape_hits']=native_cape_hits(out)
                assert result['native_cape_hits'] > 0, 'No original cape-response writer executed'
            elif case == 'chuck-side':
                assert all(a['power']==0 for a in actors.values()), 'Both contacting actors must take native damage'
            elif case == 'chuck-stomp':
                assert result['hits'] == 1, 'One shared Chuck hit expected'
                bounced = {int(r['player']) for r in rows if r['phase']=='1' and int(r['player_vy'])<0}
                assert bounced == {0,1}, ('Both valid stompers must bounce', bounced)
                assert all(a['power']==1 for a in actors.values()), 'Stompers took side damage'
            elif case=='chuck-target':
                entity=next(e for e in f.entities.values() if e[1]==0 and e[2]==0)
                assert result['state'] in (1,2) and entity[5]==0, 'No committed native attack at first checkpoint'
                # Keep the actual attack state, changing only the bystander's
                # position so Luigi becomes nearer during Mario's attack.
                second_out=root/'chuck-target-nearer-luigi'
                if not args.analyze_existing:
                    f.actor(1,168,352,1,2)
                    second=root/'chuck-target-nearer-luigi.sav'
                    f.save(second)
                    second_script=root/'chuck-target-nearer-luigi.script'
                    second_script.write_text('wait 60\nloadstate 10\nwait 6\nsavestate 11\nwait 5\n')
                    second_command=command.copy()
                    second_command[second_command.index('--snapshot')+1]=str(second)
                    second_command[second_command.index('--script')+1]=str(second_script)
                    second_command[second_command.index('--out')+1]=str(second_out)
                    subprocess.run(second_command,check=True,stdout=subprocess.DEVNULL)
                later=Fixture(second_out/'saves/native-coop-v1/save11.sav')
                later_entity=next(e for e in later.entities.values() if e[1]==0 and e[2]==0)
                result['committed_target_after_nearer_player']=later_entity[5]
                assert later_entity[5]==0 and later.data[later.ram+0xc2] in (1,2), 'Committed attack changed target'
            elif case=='star':
                defeats=[r for r in attacks if r['slot']=='0' and r['phase']=='1' and r['kind']=='1']
                assert len(defeats)==1 and defeats[0]['player']=='0' and defeats[0]['status']=='2', 'Expected one native star defeat/reward'
                assert all(a['power']==0 and a['life']==0 for a in actors.values())
            elif case=='spin':
                bounces={int(r['player']) for r in attacks if r['slot']=='0' and r['phase']=='1' and r['kind']=='2' and int(r['vy'])<0}
                assert result['sprite_status']==8 and bounces=={0,1}, 'Spiny spin response differed'
            else:
                assert result['state'] == int(case=='boo-watched'), 'Nearest-player facing response differs'
            result['status']='passed'
        except (AssertionError, OSError, subprocess.CalledProcessError) as error:
            result['error']=str(error)
        results.append(result)
        print(json.dumps(result), flush=True)
        (root/'results.json').write_text(json.dumps(results,indent=2))
    return int(any(r['status']!='passed' for r in results))


if __name__ == '__main__':
    raise SystemExit(main())
