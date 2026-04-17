#!/usr/bin/env python3
"""Rewrite recomp/funcs.h function declarations to match recompiler-authoritative sigs.

For each function defined in any bank cfg, compute the reconciled sig (see
recomp._reconcile_sig) and emit the matching C declaration. Non-function
lines (typedefs, preamble, #endif) are preserved verbatim.

Functions that appear in funcs.h but are not defined in any cfg (e.g. purely
hand-written runtime helpers, oracle externs) are left untouched.
"""
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RECOMP_PY = REPO / 'snesrecomp' / 'recompiler' / 'recomp.py'
RECOMP_DIR = REPO / 'recomp'
FUNCS_H = RECOMP_DIR / 'funcs.h'
BANKS = ['00', '01', '02', '03', '04', '05', '07', '0c', '0d']

sys.path.insert(0, str(RECOMP_PY.parent))
import recomp  # noqa: E402


def collect_cfg_sigs() -> dict:
    """Return {full_addr: (name, cfg_sig)} across every bank cfg."""
    sigs = {}
    for bank in BANKS:
        cfg_path = RECOMP_DIR / f'bank{bank}.cfg'
        cfg = recomp.parse_config(str(cfg_path))
        for fname, addr, sig, _end, _mo, _h in cfg.funcs:
            full_addr = (cfg.bank << 16) | addr
            sigs[full_addr] = (fname, sig)
        for full_addr, name in cfg.names.items():
            if full_addr not in sigs:
                sigs[full_addr] = (name, cfg.sigs.get(full_addr))
    return sigs


def cfg_sig_to_c_decl(fname: str, sig: str) -> str:
    """Convert a cfg-format sig (e.g. 'void(uint8_k)') to a C declaration string."""
    ret_type, params = recomp.parse_sig(sig)
    if not params:
        param_str = 'void'
    else:
        parts = []
        for ptype, pname in params:
            if pname.startswith('*'):
                if ptype in getattr(recomp, '_STRUCT_PTR_DP_BASE', set()):
                    parts.append(f'{ptype} {pname}')
                else:
                    parts.append(f'const uint8 {pname}')
            else:
                parts.append(f'{ptype} {pname}')
        param_str = ', '.join(parts)
    return f'{ret_type} {fname}({param_str});'


def main() -> int:
    funcs_h_sigs = recomp.parse_funcs_h(str(FUNCS_H))

    # Build {fname: cfg_sig} by preferring the intra-bank entry for each name
    # (the one the recompiler itself sees when generating that bank).
    cfg_sigs_by_name: dict = {}
    for _addr, (fname, sig) in collect_cfg_sigs().items():
        if sig is None:
            continue
        cfg_sigs_by_name.setdefault(fname, sig)

    # Reconcile per function. Only rewrite if the reconciled sig differs from
    # the current funcs.h sig; leave the original line otherwise.
    rewrites: dict = {}
    for fname, cfg_sig in cfg_sigs_by_name.items():
        fh_sig = funcs_h_sigs.get(fname)
        reconciled = recomp._reconcile_sig(cfg_sig, fh_sig)
        if reconciled is None or reconciled == fh_sig:
            continue
        rewrites[fname] = reconciled

    # Rewrite funcs.h line by line. Match declarations via the same regex
    # parse_funcs_h uses and substitute when a rewrite exists.
    decl_re = re.compile(r'^(\s*)(\w[\w\s\*]*?)\s+(\w+)\s*\(([^)]*)\)\s*;(.*)$')
    lines_out = []
    changes = 0
    with open(FUNCS_H) as fh:
        for raw in fh:
            m = decl_re.match(raw)
            if m:
                leading, _ret_raw, fname, _params_raw, trailing = m.groups()
                if fname in rewrites:
                    new_decl = cfg_sig_to_c_decl(fname, rewrites[fname])
                    old_line = raw.rstrip('\n')
                    new_line = f'{leading}{new_decl}{trailing}'
                    if new_line != old_line:
                        print(f'  [rewrite] {fname}: '
                              f'{funcs_h_sigs.get(fname)!r} -> {rewrites[fname]!r}')
                        lines_out.append(new_line + '\n')
                        changes += 1
                        continue
            lines_out.append(raw)

    FUNCS_H.write_text(''.join(lines_out))
    print(f'\nRewrote {changes} declarations in {FUNCS_H}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
