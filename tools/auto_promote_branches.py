#!/usr/bin/env python3
"""Scan gen/*.c for 'treated as return --outside decoded range' warnings and
add auto-generated `name BBAAAA CODE_BBAAAA` entries to the corresponding
per-bank cfg. Lets the recompiler emit a tail call into a named (even if
otherwise unrecognized) target instead of a bare return, which preserves
any SEP/REP/mode-cleanup the real branch target runs before its RTS.

Idempotent: entries already present are skipped. Dry-run by default; pass
--apply to write the cfgs.
"""
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GEN_DIR = REPO / 'src' / 'gen'
CFG_DIR = REPO / 'recomp'

WARN_RE = re.compile(
    r"RECOMP_WARN: \w+ \$[0-9A-Fa-f]+ treated as return "
    r"--(?:outside decoded range|before func start) "
    r"--Fix: Add 'end:[0-9A-Fa-f]+' or 'name ([0-9A-Fa-f]{6}) "
)


def collect() -> dict:
    """Return {bank_hex: set(full_addr)} of all branch targets flagged."""
    targets = defaultdict(set)
    for gen in sorted(GEN_DIR.glob('smw_??_gen.c')):
        text = gen.read_text()
        for m in WARN_RE.finditer(text):
            addr = int(m.group(1), 16)
            bank = (addr >> 16) & 0xFF
            targets[f'{bank:02x}'].add(addr)
    return targets


def already_in_cfg(cfg_path: Path, full_addr: int) -> bool:
    """Name or func entry exists for this address."""
    text = cfg_path.read_text()
    # Match `func Foo AAAA` or `name BBAAAA Foo` with explicit hex match.
    offset = full_addr & 0xFFFF
    bank = (full_addr >> 16) & 0xFF
    hex_full = f'{bank:02X}{offset:04X}'.lower()
    hex_offset = f'{offset:04X}'.lower()
    patterns = [
        rf'\bfunc\s+\S+\s+{hex_offset}\b',
        rf'\bname\s+{hex_full}\s+',
    ]
    for pat in patterns:
        if re.search(pat, text, re.IGNORECASE):
            return True
    return False


def apply(targets: dict, dry_run: bool = True) -> int:
    """Append new name entries per bank. Returns count added."""
    added = 0
    for bank_hex, addrs in sorted(targets.items()):
        cfg_path = CFG_DIR / f'bank{bank_hex}.cfg'
        if not cfg_path.exists():
            print(f'  SKIP: {cfg_path} not found')
            continue
        new_entries = []
        for full_addr in sorted(addrs):
            if already_in_cfg(cfg_path, full_addr):
                continue
            offset = full_addr & 0xFFFF
            bank = (full_addr >> 16) & 0xFF
            codename = f'CODE_{bank:02X}{offset:04X}'
            new_entries.append(
                f'name {bank:02X}{offset:04X} {codename}  '
                f'# auto-promoted from treated-as-return branch'
            )
        if not new_entries:
            continue
        added += len(new_entries)
        print(f'{cfg_path.name}: +{len(new_entries)} entries')
        for e in new_entries:
            print(f'  {e}')
        if not dry_run:
            with cfg_path.open('a') as f:
                f.write('\n# === auto_promote_branches.py: tail-call targets ===\n')
                for e in new_entries:
                    f.write(e + '\n')
    return added


def main() -> int:
    dry_run = '--apply' not in sys.argv
    targets = collect()
    total = sum(len(a) for a in targets.values())
    print(f'Found {total} unique branch targets flagged across '
          f'{len(targets)} banks')
    added = apply(targets, dry_run=dry_run)
    if dry_run:
        print(f'\n(dry run) would add {added} entries. Re-run with --apply to write.')
    else:
        print(f'\nAdded {added} entries. Regenerate and rebuild.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
