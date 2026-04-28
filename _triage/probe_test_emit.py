"""Print what the conditional-branch test ROM actually emits."""
import pathlib, sys
REPO = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'snesrecomp' / 'recompiler'))
import recomp


rom = bytes([
    0xA9, 0xAA,        # $8000 LDA #$AA
    0x85, 0x50,        # $8002 STA $50
    0xA2, 0x01,        # $8004 LDX #$01
    0x86, 0x00,        # $8006 STX $00
    0xF0, 0x07,        # $8008 BEQ $8011 (+7)
    0xA9, 0xBB,        # $800A LDA #$BB
    0x80, 0x03,        # $800C BRA $8011 (+3)
    0xEA, 0xEA, 0xEA,  # $800E-10 NOP NOP NOP
    0x85, 0x52,        # $8011 STA $52  (label_8011)
    0x86, 0x51,        # $8013 STX $51
    0xD0, 0xFA,        # $8015 BNE $8011 (-6)
    0x60,              # $8017 RTS
])
insns = recomp.decode_func(rom=rom, bank=0, start=0x8000, end=0x8018,
                           known_func_starts={0x8000})
lines = recomp.emit_function(
    name='test_fn', insns=insns, bank=0,
    func_names={}, func_sigs={},
    sig='void()', rom=rom, end_addr=0x8018,
)
for i, ln in enumerate(lines):
    print(f'{i:3d}: {ln}')
