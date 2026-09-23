#!/usr/bin/env python3
"""Install SMW host-renderer ownership and optional gameplay policy hooks.
Generated banks are never edited manually. Hooks are idempotent and every
required site must exist; an unsupported generator change fails the build.
"""
import argparse
from pathlib import Path
import re

PCS = (0x02A82E, 0x01B844, 0x01AC7C, 0x02D076, 0x03B8A8, 0x02A204, 0x019E93, 0x019F5A,
       0x02A916, 0x02AFB3, 0x029B0C, 0x029B12, 0x02A2BE, 0x00E49A)
# The normal sprite dispatcher is currently certified LLE-only. Its interpreter
# boundaries are always armed; instrument them too if a future generator emits it.
OPTIONAL_PCS = (0x0180AF, 0x0180B2)
MARKER = '/*SMW-HOST*/'
def apply(text):
    if '/*WS-' in text:
        raise SystemExit('Legacy widescreen hooks found; regenerate with tools/regen.sh --stock before building')
    # Replace our old hook text when rebuilding, preserving generator lines.
    text = re.sub(r'^[ \t]*/\*SMW-HOST\*/ \{[^\n]* \}\r?\n', '', text, flags=re.M)
    text = re.sub(r' /\*SMW-HOST\*/ \{[^\n]*? \}', '', text)
    found = set()
    out = []
    name = ''
    block = ''
    for line in text.splitlines(keepends=True):
        m = re.match(r'RecompReturn (\w+)\(CpuState', line)
        if m: name = m[1]
        m = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-F]+)\)', line)
        if m: block = m[1]
        hook = None
        if block == '01A7EB' and 'if (cpu->_flag_Z == 1)' in line and '$A7F7' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x01A7F3u); }\n')
            found.add('player_contact')
        if block == '00E482' and 'if (cpu->_flag_C == 1)' in line and 'goto L_E49F' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x00E498u); }\n')
            found.add('player_cull')
        if 'ExtSpr0D_Baseball' in name and 'if (cpu->_flag_Z == 1)' in line and 'goto L_A287' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x02A27Eu); }\n')
            found.add('baseball_cull')
        if block == '019E3C' and 'if (cpu->_flag_Z == 0)' in line and 'goto L_9E93' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x019E6Du); }\n')
            found.add('wing_cull')
        if block == '02A823' and 'if (cpu->_flag_N == 1)' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x02A826u); }\n')
            found.add('frontier')
        # Both public entries emit the same horizontal cull: the generic
        # entry includes LDY in its block, while FireballEntry starts after it.
        if block in ('02A1A4', '02A1A7') and 'if (cpu->_flag_Z == 0)' in line:
            out.append('    /*SMW-HOST*/ { extern void SmwRendererGuestHook(CpuState *, uint32_t); SmwRendererGuestHook(cpu, 0x02A1BEu); }\n')
            found.add('fireball')
        for pc in PCS + OPTIONAL_PCS:
            if f'cpu_trace_block(cpu, 0x{pc:06X});' in line:
                hook = f'SmwRendererGuestHook(cpu, 0x{pc:06X}u)'
                found.add(pc)
        if 'GetDrawInfo' in name and 'cpu_write8' in line and '0x15c4 + (uint32)cpu->X' in line:
            hook = 'SmwRendererDrawInfo(cpu)'
            found.add('draw')
        if 'ProcessNormalSprites_GetNormalSpriteOAMIndex' in name and 'cpu_write8' in line and '0x15ea + (uint32)cpu->X' in line:
            hook = 'SmwRendererGuestHook(cpu, 0x0180E5u)'
            found.add('sprite_allocation')
        if hook:
            symbol = hook.split('(')[0]
            declaration = ('CpuState *cpu, uint32_t pc' if symbol.endswith('GuestHook') else 'CpuState *cpu')
            line = line.rstrip('\r\n') + f' {MARKER} {{ extern void {symbol}({declaration}); {hook}; }}\n'
        out.append(line)
    return ''.join(out), found

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gen-dir', default='src/gen')
    args = parser.parse_args()
    found = set()
    paths = sorted(Path(args.gen_dir).glob('*.c'))
    if not paths: raise SystemExit('No generated banks; run tools/regen.sh first')
    updates = []
    for path in paths:
        text = path.read_text(encoding='utf-8')
        changed, hits = apply(text)
        found |= hits
        if changed != text: updates.append((path, changed))
    missing = set(PCS) | {'draw', 'fireball', 'frontier', 'wing_cull', 'sprite_allocation', 'baseball_cull', 'player_cull', 'player_contact'}
    missing -= found
    if missing: raise SystemExit(f'Missing required renderer hook sites: {missing}')
    for path, changed in updates: path.write_text(changed, encoding='utf-8', newline='\n')
    print(f'SMW host hooks: {len(found)} sites, {len(updates)} banks updated')

if __name__ == '__main__': main()
