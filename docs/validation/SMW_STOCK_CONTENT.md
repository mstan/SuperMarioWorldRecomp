# Stock SMW content inventory

Generated from the stock-US ROM and checked-in SMWDisX symbol map.
This inventory grants no gameplay passes. Coverage begins unverified.

## Reproduction

`python tools/smw_content_inventory.py --out <new-evidence-directory>`

ROM SHA256: `0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b`.

The JSON contains all 512 indexed headers, layer object/sprite locations,
512 secondary entrances, 9 Chocolate Island 2 table entries, 6 entrance
variants, original overworld node numbering and resolved screen-exit edges.
The coverage seed also includes intro, title, both bonus and both wings rooms.
Duplicate pointers remain labeled so aliases cannot hide variant settings.

## Source boundaries

- `CODE_04D7F2`: original overworld tile scan and translevel assignment.
- `CODE_05D8A2`: translevel/submap to primary level.
- `LoadLevelData`, `ExtOBJScreenExit`, `ExtOBJScreenJump`: object streams.
- `CODE_05D796`: screen exit and secondary entrance resolution.
- `ChocIsld2Layer1/Sprites/Layer2`, `CODE_05DB3E/6E/82`: dynamic rooms.
- `PtrsLong05D766/778`: castle, ghost-house and no-Yoshi entrances.
- `CODE_05DBAC`: bonus and Yoshi wings destination selection.

The Chocolate Island 2 coin tests use the native 8-bit N flag; its third
dragon-coin table entry is retained as unused, not invented as a route.

## Inventory status

- Indexed entries: 512; special table entries: 15.
- Original overworld nodes: 92 (includes warp tiles).
- Screen-exit edges with root context: 240.
- Room/variant records in the initial coverage seed: 238.
- Gameplay passes granted by extraction: **0**.

The screen graph is not a complete mechanics audit. Supplement it with
actual boss/switch/goal outcomes, alternate entrance prerequisites, native
door/pipe triggers, bonus and wings entry/return, overworld event destinations,
and special-scene execution. Derive remaining checks from those handlers;
do not treat table presence, teleporting, or loading as successful behavior.

## Overworld nodes

| Translevel | Level | Original tile | Kind | Source label | Screen graph rooms |
|---|---|---|---|---|---|
| `01` | `001` | `6E` | level | VS2Level001 | 2 |
| `02` | `002` | `72` | level | VS3Level002 | 2 |
| `03` | `003` | `6E` | level | TSALevel003 | 1 |
| `04` | `004` | `63` | level | DGHLevel004 | 7 |
| `05` | `005` | `6E` | level | DP3Level005 | 2 |
| `06` | `006` | `6E` | level | DP4Level006 | 3 |
| `07` | `007` | `5D` | level | C2Level007 | 5 |
| `08` | `008` | `76` | level | GSPLevel008 | 2 |
| `09` | `009` | `7B` | level | DP2Level009 | 3 |
| `0A` | `00A` | `74` | level | DS1Level00A | 2 |
| `0B` | `00B` | `58` | level | VFLevel00B | 4 |
| `0C` | `00C` | `6E` | level | BB1Level00C | 2 |
| `0D` | `00D` | `6E` | level | BB2Level00D | 2 |
| `0E` | `00E` | `5D` | level | C4Level00E | 5 |
| `0F` | `00F` | `70` | level | CBALevel00F | 2 |
| `10` | `010` | `6E` | level | CMLevel010 | 2 |
| `11` | `011` | `72` | level | SLLevel011 | 2 |
| `12` | `012` | `5A` | level | TestLevel | 1 |
| `13` | `013` | `63` | level | DSHLevel013 | 8 |
| `14` | `014` | `76` | level | YSPLevel014 | 2 |
| `15` | `015` | `70` | level | DP1Level015 | 3 |
| `16` | `016` | `5A` | level | DP1Level015 | 4 |
| `17` | `017` | `5D` | level | DP1Level015 | 4 |
| `18` | `018` | `7F` | level | SGSLevel018 | 3 |
| `19` | `019` | `56` | level | TestLevel | 1 |
| `1A` | `01A` | `5D` | level | C6Level01A | 3 |
| `1B` | `01B` | `58` | level | CFLevel01B | 3 |
| `1C` | `01C` | `6E` | level | CI5Level01C | 3 |
| `1D` | `01D` | `57` | level | CI4Level01D | 2 |
| `1E` | `01E` | `5A` | level | TestLevel | 1 |
| `1F` | `01F` | `58` | level | FFLevel01F | 3 |
| `20` | `020` | `5D` | level | C5Level020 | 2 |
| `21` | `021` | `63` | level | CGHLevel021 | 3 |
| `22` | `022` | `6E` | level | CI1Level022 | 6 |
| `23` | `023` | `70` | level | CI3Level023 | 2 |
| `24` | `024` | `70` | level | CI2Level024 | 12 |
| `25` | `101` | `5D` | level | C1Level101 | 3 |
| `26` | `102` | `72` | level | YI4Level102 | 3 |
| `27` | `103` | `6E` | level | YI3Level103 | 2 |
| `28` | `104` | `56` | level | YHLevel104 | 1 |
| `29` | `105` | `66` | level | YI1Level105 | 2 |
| `2A` | `106` | `66` | level | YI2Level106 | 2 |
| `2B` | `107` | `63` | level | VGHLevel107 | 4 |
| `2C` | `108` | `5A` | level | SlopeTestLevel108 | 1 |
| `2D` | `109` | `70` | level | VS1Level109 | 3 |
| `2E` | `10A` | `6F` | level | VD3Level10A | 3 |
| `2F` | `10B` | `6E` | level | DS2Level10B | 2 |
| `30` | `10C` | `5A` | level | TestLevel | 1 |
| `31` | `10D` | `60` | level | FDLevel10D | 12 |
| `32` | `10E` | `61` | level | BDLevel10E | 2 |
| `33` | `10F` | `70` | level | VoB4Level10F | 2 |
| `34` | `110` | `5D` | level | C7Level110 | 3 |
| `35` | `111` | `58` | level | VoBFLevel111 | 2 |
| `36` | `112` | `78` | level | TestLevel | 1 |
| `37` | `113` | `6E` | level | VoB3Level113 | 2 |
| `38` | `114` | `63` | level | VoBGHLevel114 | 6 |
| `39` | `115` | `71` | level | VoB2Level115 | 3 |
| `3A` | `116` | `6E` | level | VoB1Level116 | 3 |
| `3B` | `117` | `6E` | level | CSLevel117 | 5 |
| `3C` | `118` | `74` | level | VD2Level118 | 2 |
| `3D` | `119` | `6E` | level | VD4Level119 | 2 |
| `3E` | `11A` | `71` | level | VD1Level11A | 2 |
| `3F` | `11B` | `78` | level | RSPLevel11B | 2 |
| `40` | `11C` | `5D` | level | C3Level11C | 4 |
| `41` | `11D` | `7A` | level | FGHLevel11D | 6 |
| `42` | `11E` | `69` | level | FoI1Level11E | 1 |
| `43` | `11F` | `70` | level | FoI4Level11F | 3 |
| `44` | `120` | `74` | level | FoI2Level120 | 1 |
| `45` | `121` | `78` | level | BSPLevel121 | 2 |
| `46` | `122` | `6E` | level | FSALevel122 | 1 |
| `47` | `123` | `70` | level | FoI3Level123 | 3 |
| `48` | `124` | `5A` | level | TestLevel | 1 |
| `49` | `125` | `7D` | level | FunkyLevel125 | 1 |
| `4A` | `126` | `6E` | level | OutrageousLevel126 | 1 |
| `4B` | `127` | `7D` | level | MondoLevel127 | 3 |
| `4C` | `128` | `6E` | level | GroovyLevel128 | 1 |
| `4D` | `129` | `5F` | star_warp | TestLevel | 0 |
| `4E` | `12A` | `66` | level | GnarlyLevel12A | 3 |
| `4F` | `12B` | `7D` | level | TubularLevel12B | 1 |
| `50` | `12C` | `6E` | level | WayCoolLevel12C | 2 |
| `51` | `12D` | `7D` | level | AwesomeLevel12D | 1 |
| `52` | `12E` | `5F` | star_warp | TestLevel | 0 |
| `53` | `12F` | `5F` | star_warp | TestLevel | 0 |
| `54` | `130` | `68` | level | SW2Level130 | 2 |
| `55` | `131` | `5A` | level | TestLevel | 1 |
| `56` | `132` | `68` | level | SW3Level132 | 1 |
| `57` | `133` | `5F` | star_warp | TestLevel | 0 |
| `58` | `134` | `68` | level | SW1Level134 | 2 |
| `59` | `135` | `68` | level | SW4Level135 | 1 |
| `5A` | `136` | `68` | level | SW5Level136 | 1 |
| `5B` | `137` | `5F` | star_warp | TestLevel | 0 |
| `5C` | `138` | `5F` | star_warp | TestLevel | 0 |
