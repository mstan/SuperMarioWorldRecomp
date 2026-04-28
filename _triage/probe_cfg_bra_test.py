import pathlib, sys
REPO = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'snesrecomp' / 'recompiler'))
import recomp, cfg as cfg_mod

rom = bytes([
    0x80, 0x01,        # $8000 BRA $8003 (+1 from $8002)
    0xEA,              # $8002 NOP (unreachable)
    0x60,              # $8003 RTS
])
insns = recomp.decode_func(rom=rom, bank=0, start=0x8000, end=0x8004,
                           known_func_starts={0x8000})
print(f'decoded {len(insns)} instructions:')
for i in insns:
    print(f'  ${i.addr:06x} {i.mnem} mode={i.mode} operand=${i.operand:x}')
decoded_pcs = {(i.addr & 0xFFFF) for i in insns}
print(f'\ndecoded_pcs: {sorted(hex(p) for p in decoded_pcs)}')

valid = set()
for ins in insns:
    if ins.mnem in ('BRA','BRL','JMP','BPL','BMI','BEQ','BNE','BCC','BCS','BVS','BVC'):
        if ins.operand in decoded_pcs:
            valid.add(ins.operand)
print(f'valid_branch_targets: {sorted(hex(p) for p in valid)}')

c = cfg_mod.build_cfg(insns, valid, bank=0, func_start=0x8000)
print(f'\nblocks: {sorted(hex(b) for b in c.blocks)}')
for s, bb in sorted(c.blocks.items()):
    print(f'  ${s:04x}: term={bb.terminator} ft={bb.has_fall_through} '
          f'succ={[hex(p) for p in bb.successors]} '
          f'pred={[hex(p) for p in bb.predecessors]}')
