#!/usr/bin/env python3
"""Reproducible native co-op boundaries in every emitted stock variant.

All hooks are runtime gated. Native transfers keep local generated labels or
the engine's inherited paired-call frame; they never enter the interpreter.
"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MARKER = '/*SMW-NATIVE-COOP*/'

def sites():
    source = (ROOT / 'src/mods/coop/coop_hooks.def').read_text()
    return {int(s, 16) for s in re.findall(r'COOP_HOOK(?:_INSIDE)?\((0x[0-9A-Fa-f]+)', source)}

def containing_entries():
    source = (ROOT / 'src/mods/coop/coop_hooks.def').read_text()
    return {int(pc,16):int(owner,16) for pc,owner in re.findall(
        r'COOP_HOOK_INSIDE\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)',source)}

def apply(text, required):
    text = re.sub(r'^.*?/\*SMW-NATIVE-COOP\*/[^\n]*\n', '', text, flags=re.M)
    found = set()
    out = []
    simulation = '\n'.join((ROOT/'src/mods/coop'/name).read_text()
                           for name in ('coop_simulation.c','coop_runtime.c'))
    targets = {int(value, 16) for expression in re.findall(r'\breturn\s+([^;]+);', simulation)
               for value in re.findall(r'0x[0-9a-fA-F]+', expression)}
    functions = re.split(r'(?=^RecompReturn \w+\(CpuState \*cpu\) \{)', text, flags=re.M)
    for function in functions:
        labels = re.findall(r'^\s*(L_([0-9A-Fa-f]{4})_M([01])X([01])):', function, re.M)
        for line in function.splitlines(keepends=True):
            m = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-Fa-f]+)\);', line)
            if m and int(m[1], 16) in required:
                pc = int(m[1], 16)
                jumps = []
                for label, address, mf, xf in labels:
                    target = (pc & 0xff0000) | int(address, 16)
                    if target in targets:
                        jumps.append(f'if (target == 0x{target:06X}u && cpu->m_flag == {mf} && cpu->x_flag == {xf}) goto {label}; ')
                # Keep the enclosing generated frame for an interior return.
                # A cross-function transfer adopts that same frame explicitly.
                out.append(f'    {MARKER} {{ extern uint32_t SmwCoopGuestHook(CpuState *, uint32_t); extern RecompReturn SmwCoopNativeTail(CpuState *, uint32_t, uint16_t, uint8_t); uint32_t target = SmwCoopGuestHook(cpu, 0x{pc:06X}u); if (target) {{ '+''.join(jumps)+'RecompStackPop(); return SmwCoopNativeTail(cpu, target, _entry_s, _hrv); } }\n')
                found.add(pc)
            out.append(line)
    return ''.join(out), found

def interpreted_entries(dispatch):
    # An explicit all-NULL dispatch row routes every M/X variant through the
    # interpreter, where coop_hooks.def installs the same boundary observer.
    # Merely failing to find a generated block is never sufficient evidence.
    return {int(pc,16) for pc in re.findall(
        r'\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*\{\s*NULL\s*,\s*NULL\s*,\s*NULL\s*,\s*NULL\s*\}',
        dispatch)}

def interpreted_coverage(dispatch,owners):
    entries=interpreted_entries(dispatch)
    # Interior boundaries require an audited containing entry in the manifest.
    # If even one variant of that entry becomes compiled, these interior hooks
    # must be found in its emitted blocks or the build stops for a new audit.
    return entries|{pc for pc,owner in owners.items() if owner in entries}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gen-dir', type=Path, required=True)
    args = parser.parse_args()
    required = sites()
    found, updates = set(), []
    for path in sorted(args.gen_dir.glob('bank*.c')):
        text = path.read_text(encoding='utf-8')
        changed, seen = apply(text, required)
        found.update(seen)
        if changed != text:
            updates.append((path, changed))
    missing = required - found
    if missing:
        raise SystemExit('Missing native co-op boundaries: ' + ', '.join(f'{pc:06X}' for pc in sorted(missing)))
    for path, text in updates:
        path.write_text(text, encoding='utf-8', newline='\n')
    print(f'Native co-op hooks: {len(found)} compiled sites, {len(updates)} banks updated')

if __name__ == '__main__':
    main()
