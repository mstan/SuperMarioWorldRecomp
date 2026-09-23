#!/usr/bin/env python3
"""Reproducible native co-op boundaries in every emitted stock variant.

All hooks are runtime gated. A transfer uses the existing tail ABI, preserving
the enclosing guest return boundary in both generated and interpreted code.
"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
MARKER = '/*SMW-NATIVE-COOP*/'

def sites():
    source = (ROOT / 'src/mods/coop/coop_hooks.def').read_text()
    return {int(s, 16) for s in re.findall(r'COOP_HOOK\((0x[0-9A-Fa-f]+)\)', source)}

def apply(text, required):
    text = re.sub(r'^.*?/\*SMW-NATIVE-COOP\*/[^\n]*\n', '', text, flags=re.M)
    found = set()
    out = []
    for line in text.splitlines(keepends=True):
        m = re.search(r'cpu_trace_block\(cpu, 0x([0-9A-Fa-f]+)\);', line)
        if m and int(m[1], 16) in required:
            pc = int(m[1], 16)
            # This is after the label but before the block's first instruction.
            # Existing renderer observers remain in the same generated block.
            out.append(f'    {MARKER} {{ extern uint32_t SmwCoopGuestHook(CpuState *, uint32_t); uint32_t target = SmwCoopGuestHook(cpu, 0x{pc:06X}u); if (target) {{ RecompStackPop(); return interp_tier_dispatch_tail(cpu, target, 0x{pc:06X}u, _entry_s, _hrv); }} }}\n')
            found.add(pc)
        out.append(line)
    return ''.join(out), found

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
    print(f'Native co-op hooks: {len(found)} sites, {len(updates)} banks updated')

if __name__ == '__main__':
    main()
