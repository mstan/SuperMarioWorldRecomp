#!/usr/bin/env python3
"""Extract stock-US content tables for a reproducible campaign audit.

This is a content inventory, not evidence that a route or mechanic works.
Runtime/scripted transitions supplement the screen-exit graph. No ROM bytes,
save files, success flags or active game state are changed.
"""
import argparse
from collections import defaultdict, deque
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA256 = '0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b'
CHECKS = ('terrain_collision', 'scrolling_visibility', 'enemy_interactions',
          'audio', 'checkpoint_retry', 'death_recovery', 'boss_mechanics',
          'exit_trigger', 'destination_transition')


class Stock:
    def __init__(self, rom, symbols):
        self.rom = rom.read_bytes()
        if hashlib.sha256(self.rom).hexdigest() != STOCK_SHA256:
            raise ValueError('Inventory requires the original unheadered stock-US ROM')
        self.symbols = {name: int(address, 16) for address, name in re.findall(
            r'^([0-9A-Fa-f]{8}) (\S+)$', symbols.read_text(), re.M)}
        self.labels = defaultdict(list)
        for name, address in self.symbols.items():
            self.labels[address].append(name)

    def bytes(self, address, count):
        if address & 0x8000 == 0 or address >> 16 in (0x7e, 0x7f):
            raise ValueError(f'Not a stock LoROM address: ${address:06X}')
        offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff)
        data = self.rom[offset:offset+count]
        if len(data) != count:
            raise ValueError(f'ROM bounds at ${address:06X}')
        return data

    def number(self, address, count=1):
        return int.from_bytes(self.bytes(address, count), 'little')

    def table(self, name, index, width=1):
        return self.number(self.symbols[name]+index*width, width)

    def label(self, address):
        return self.labels.get(address, [])

    def objects(self, pointer, vertical):
        page, at = 0, pointer+5
        objects, exits = [], []
        for _ in range(4096):
            a = self.number(at)
            if a == 0xff:
                return dict(pointer=f'{pointer:06X}', labels=self.label(pointer),
                            end=f'{at:06X}', objects=objects, exits=exits)
            a, b, size = self.bytes(at, 3)
            address = at
            at += 3
            kind = ((a & 0x60) >> 1) | (b >> 4)
            page += bool(a & 0x80)
            if kind == 0 and size == 0:
                exits.append(dict(address=f'{address:06X}', screen=a & 31,
                                  low=self.number(at), encoded_high=b & 1,
                                  secondary_mode=b >> 1))
                at += 1
            elif kind == 0 and size == 1:
                page = a & 31
            else:
                x, y = page*256+(b & 15)*16, (a & 31)*16
                if vertical:
                    x, y = y, x
                objects.append(dict(address=f'{address:06X}', screen=page,
                                    object=kind, size_or_extended_id=size, x=x, y=y))
        raise ValueError(f'Unterminated objects at ${pointer:06X}')

    def sprites(self, pointer, vertical):
        header = self.number(pointer)
        at, result = pointer+1, []
        for _ in range(512):
            if self.number(at) == 0xff:
                return dict(pointer=f'{pointer:06X}', labels=self.label(pointer),
                            header=header, entries=result, end=f'{at:06X}')
            a, b, sprite = self.bytes(at, 3)
            screen = ((a & 2) << 3) | (b & 15)
            x, y = screen*256+(b & 0xf0), (a & 1)*256+(a & 0xf0)
            if vertical:
                x, y = y, x
            result.append(dict(address=f'{at:06X}', sprite=sprite,
                               extra=(a & 12) >> 2, screen=screen, x=x, y=y))
            at += 3
        raise ValueError(f'Unterminated sprites at ${pointer:06X}')

    def room(self, key, l1, l2, sprite, level=None):
        header = self.bytes(l1, 5)
        mode = header[1] & 31
        vertical = self.table('VerticalTable', mode)
        layer1 = self.objects(l1, bool(vertical & 1))
        layer2 = None
        # LoadLevel: modes with no layer-2 object pass; FF-bank points to BG.
        if l2 >> 16 != 0xff and mode not in (0, 0xa, 0xc, 0xd, 0xe, 0x11, 0x1e):
            layer2 = self.objects(l2, bool(vertical & 2))
        sprites = self.sprites(sprite, bool(vertical & 1))
        result = dict(id=key, level=level, labels=self.label(l1),
                      mode=mode, vertical=bool(vertical & 1),
                      screens=(header[0] & 31)+1, tileset=header[4] & 15,
                      music_selector=(header[2] >> 4) & 7,
                      sprite_tileset=header[2] & 15,
                      scripted_boss_mode=mode in (9, 0xb, 0x10),
                      layer1=layer1, layer2=layer2,
                      layer2_pointer=f'{l2:06X}', sprites=sprites)
        exits = layer1['exits'] + (layer2['exits'] if layer2 else [])
        # $192A is a room-wide secondary-exit flag written by the last exit
        # object, not a per-screen bit. Retain every raw record for auditing.
        result['secondary_mode'] = exits[-1]['secondary_mode'] if exits else 0
        result['screen_exits'] = list({e['screen']: e for e in exits}.values())
        result['exit_sprites'] = [
            dict(entry=i, sprite=e['sprite'], x=e['x'], y=e['y'],
                 kind=('secret_goal' if e['extra'] else 'normal_goal')
                 if e['sprite'] == 0x7b else
                 'keyhole' if e['sprite'] == 0x0e else 'goal_sphere')
            for i, e in enumerate(sprites['entries'])
            if e['sprite'] in (0x7b, 0x0e, 0x4a)]
        if level is not None:
            result['secondary_header'] = [
                self.table(name, level) for name in
                ('DATA_05F000', 'DATA_05F200', 'DATA_05F400', 'DATA_05F600')]
        return result

    def inventory(self):
        rooms = {}
        for level in range(512):
            key = f'{level:03X}'
            rooms[key] = self.room(key, self.table('Layer1Ptrs', level, 3),
                                   self.table('Layer2Ptrs', level, 3),
                                   0x070000 | self.table('Ptrs05EC00', level, 2), level)
        dynamic = []
        for index in range(9):
            key = f'chocolate2-{index}'
            rooms[key] = self.room(
                key, 0x060000 | self.table('ChocIsld2Layer1', index, 2),
                0xff0000 | self.table('ChocIsld2Layer2', index, 2),
                0x070000 | self.table('ChocIsld2Sprites', index, 2))
            dynamic.append(dict(room=key, table_index=index,
                                selector=('dragon_coins', 'coins', 'timer')[index//3],
                                selected_by_stock_code=index != 2,
                                condition=(
                                    'DragonCoinsShown == 4',
                                    'DragonCoinsShown != 4',
                                    'unused duplicate table entry',
                                    'N(coins - 22) and N(coins - 10)',
                                    'N(coins - 22) and not N(coins - 10)',
                                    'not N(coins - 22)',
                                    'hundreds < 2 or tens < 3 or (tens == 3 and ones < 5)',
                                    'otherwise, tens < 5',
                                    'otherwise, tens >= 5')[index]))
        intros = []
        for index in range(6):
            key = f'entrance-{index}'
            rooms[key] = self.room(key, self.table('PtrsLong05D766', index, 3),
                                   self.table('PtrsLong05D778', index, 3), 0x07c3ee)
            intros.append(key)
        secondary = []
        for entry in range(512):
            secondary.append(dict(id=entry, destination=(entry & 0x100) |
                                  self.table('DATA_05F800', entry),
                                  y_camera=self.table('DATA_05FA00', entry),
                                  x_screen=self.table('DATA_05FC00', entry),
                                  entrance_flags=self.table('DATA_05FE00', entry)))
        roots = []
        # CODE_04D7F2 numbers every original map tile in [56,81) in layout
        # order. CODE_05D8A2 maps that translevel plus submap to a level ID.
        for offset, tile in enumerate(self.bytes(self.symbols['OWL1TileData'], 0x800)):
            if not 0x56 <= tile < 0x81:
                continue
            translevel = len(roots)+1
            level = (translevel if translevel < 0x25 else translevel-0x24)
            if offset >= 0x400:
                level |= 0x100
            kind = 'star_warp' if tile == 0x5f else 'level'
            roots.append(dict(translevel=translevel, level=f'{level:03X}',
                              tile=tile, map_offset=offset, kind=kind,
                              labels=rooms[f'{level:03X}']['labels']))
        routes = []
        for root in roots:
            if root['kind'] != 'level':
                continue
            bank = int(root['level'], 16) & 0x100
            queue, visited = deque([root['level']]), set()
            while queue:
                key = queue.popleft()
                if key in visited:
                    continue
                visited.add(key)
                room = rooms[key]
                for e in room['screen_exits']:
                    entry = bank | e['low']
                    target = secondary[entry]['destination'] if room['secondary_mode'] else entry
                    target_key = f'{target:03X}'
                    routes.append(dict(root=root['translevel'], source=key,
                                       screen=e['screen'], secondary_entry=entry
                                       if room['secondary_mode'] else None,
                                       destination=target_key))
                    queue.append(target_key)
                if root['translevel'] == 0x24:
                    queue.extend(v['room'] for v in dynamic if v['selected_by_stock_code'])
            root['screen_exit_reachable_rooms'] = sorted(visited)
        return dict(schema=1, rom_sha256=STOCK_SHA256, rooms=rooms,
                    overworld_nodes=roots, secondary_entrances=secondary,
                    screen_exit_routes=routes, chocolate2_variants=dynamic,
                    entrance_variants=intros,
                    scene_rooms=[
                        dict(room='000', kind='bonus', source='CODE_05DBAC'),
                        dict(room='100', kind='bonus', source='CODE_05DBAC'),
                        dict(room='0C8', kind='yoshi_wings', source='CODE_05DBAC'),
                        dict(room='1C8', kind='yoshi_wings', source='CODE_05DBAC'),
                        dict(room='0C5', kind='intro', source='IntroLevel0C5'),
                        dict(room='0C7', kind='title_demo', source='TitleScrLevel0C7'),
                    ],
                    scope_notes=[
                        '512 indexed level entries include aliases and unused/test data.',
                        'Overworld node numbers include warp tiles; they are not stage counts.',
                        'Screen-exit reachability is not proof of navigability or prerequisites.',
                        'Scripted door choices, bonus/wing warps, bosses and scene transitions require supplementary runtime coverage.',
                        'Chocolate Island 2 selectors must be exercised through the native branch conditions.',
                    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT/'smw.sfc')
    parser.add_argument('--symbols', type=Path, default=ROOT/'third_party/SMWDisX/SMW_U.sym')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    stock = Stock(args.rom, args.symbols)
    result = stock.inventory()
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out/'inventory.json').write_text(json.dumps(result, indent=2)+'\n', newline='\n')
    coverage = []
    required = set(result['entrance_variants'])
    required.update(scene['room'] for scene in result['scene_rooms'])
    for root in result['overworld_nodes']:
        required.update(root.get('screen_exit_reachable_rooms', []))
    for key in sorted(required):
        coverage.append(dict(room=key, checks={check: dict(status='unverified', evidence=[])
                                              for check in CHECKS}))
    (args.out/'coverage-seed.json').write_text(json.dumps(coverage, indent=2)+'\n', newline='\n')
    summary = [
        '# Stock SMW content inventory', '',
        'Generated from the stock-US ROM and checked-in SMWDisX symbol map.',
        'This inventory grants no gameplay passes. Coverage begins unverified.', '',
        '## Reproduction', '',
        '`python tools/smw_content_inventory.py --out <new-evidence-directory>`', '',
        f'ROM SHA256: `{STOCK_SHA256}`.', '',
        'The JSON contains all 512 indexed headers, layer object/sprite locations,',
        '512 secondary entrances, 9 Chocolate Island 2 table entries, 6 entrance',
        'variants, original overworld node numbering and resolved screen-exit edges.',
        'The coverage seed also includes intro, title, both bonus and both wings rooms.',
        'Duplicate pointers remain labeled so aliases cannot hide variant settings.', '',
        '## Source boundaries', '',
        '- `CODE_04D7F2`: original overworld tile scan and translevel assignment.',
        '- `CODE_05D8A2`: translevel/submap to primary level.',
        '- `LoadLevelData`, `ExtOBJScreenExit`, `ExtOBJScreenJump`: object streams.',
        '- `CODE_05D796`: screen exit and secondary entrance resolution.',
        '- `ChocIsld2Layer1/Sprites/Layer2`, `CODE_05DB3E/6E/82`: dynamic rooms.',
        '- `PtrsLong05D766/778`: castle, ghost-house and no-Yoshi entrances.',
        '- `CODE_05DBAC`: bonus and Yoshi wings destination selection.', '',
        'The Chocolate Island 2 coin tests use the native 8-bit N flag; its third',
        'dragon-coin table entry is retained as unused, not invented as a route.', '',
        '## Inventory status', '',
        f'- Indexed entries: 512; special table entries: {len(result["rooms"])-512}.',
        f'- Original overworld nodes: {len(result["overworld_nodes"])} (includes warp tiles).',
        f'- Screen-exit edges with root context: {len(result["screen_exit_routes"])}.',
        f'- Room/variant records in the initial coverage seed: {len(required)}.',
        '- Gameplay passes granted by extraction: **0**.', '',
        'The screen graph is not a complete mechanics audit. Supplement it with',
        'actual boss/switch/goal outcomes, alternate entrance prerequisites, native',
        'door/pipe triggers, bonus and wings entry/return, overworld event destinations,',
        'and special-scene execution. Derive remaining checks from those handlers;',
        'do not treat table presence, teleporting, or loading as successful behavior.', '',
        '## Overworld nodes', '',
        '| Translevel | Level | Original tile | Kind | Source label | Screen graph rooms |',
        '|---|---|---|---|---|---|',
    ]
    for root in result['overworld_nodes']:
        summary.append(f'| `{root["translevel"]:02X}` | `{root["level"]}` | '
                       f'`{root["tile"]:02X}` | {root["kind"]} | '
                       f'{", ".join(root["labels"])} | '
                       f'{len(root.get("screen_exit_reachable_rooms", []))} |')
    (args.out/'inventory.md').write_text('\n'.join(summary)+'\n', newline='\n')
    print(json.dumps(dict(indexed_rooms=512, special_rooms=len(result['rooms'])-512,
                          overworld_nodes=len(result['overworld_nodes']),
                          indexed_routes=len(result['screen_exit_routes']),
                          required_by_screen_graph=len(required),
                          verified_mechanics=0)))


if __name__ == '__main__':
    main()
