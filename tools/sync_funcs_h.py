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
GEN_DIR = REPO / 'src' / 'gen'
FUNCS_H = RECOMP_DIR / 'funcs.h'
ROM_PATH = REPO / 'smw.sfc'
BANKS = ['00', '01', '02', '03', '04', '05', '07', '0c', '0d']

sys.path.insert(0, str(RECOMP_PY.parent))
import recomp  # noqa: E402
from snes65816 import load_rom  # noqa: E402


_GEN_BODY_RE = re.compile(
    r'^(?P<ret>\w[\w\s\*]*?)\s+(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*\{\s*//\s*(?P<addr>[0-9a-f]{6})',
    re.MULTILINE,
)


def collect_gen_sigs() -> dict:
    """Scan every src/gen/smw_*_gen.c body header for its declared return
    type + parameter list + source address. Return {full_addr: (name, sig)}
    using the same `ret(param_type_param_name,...)` encoding parse_sig
    understands.

    Gen files are the ground truth for what the recompiler emitted this
    cycle, so using them sidesteps divergence between sync_funcs_h's
    simulated augment and run_config's full pipeline. Whatever the gen
    file says the function looks like, that's what funcs.h has to match.
    """
    out: dict = {}
    for p in sorted(GEN_DIR.glob('smw_??_gen.c')):
        text = p.read_text()
        for m in _GEN_BODY_RE.finditer(text):
            ret_raw = m.group('ret').strip()
            name = m.group('name')
            params_raw = m.group('params').strip()
            addr = int(m.group('addr'), 16)
            if ret_raw == 'extern' or ret_raw.startswith('static'):
                continue  # skip decls
            # Convert C-style params back to cfg-style tokens.
            if not params_raw or params_raw == 'void':
                sig = f'{ret_raw}()'
            else:
                toks = []
                for p_ in params_raw.split(','):
                    p_ = p_.strip()
                    if not p_:
                        continue
                    if '*' in p_:
                        star = p_.index('*')
                        pname = p_[star:].replace(' ', '')
                        type_part = p_[:star].strip()
                        words = [w for w in type_part.split()
                                 if w not in ('const', 'volatile', 'struct')]
                        if words and words[-1] not in (
                                'uint8', 'uint16', 'int8', 'int16', 'char', 'void'):
                            toks.append(f'{words[-1]}_{pname}')
                        else:
                            toks.append(pname)
                    else:
                        toks.append('_'.join(p_.split()))
                sig = f'{ret_raw}(' + ','.join(toks) + ')'
            out[addr] = (name, sig)
    return out


def collect_cfg_sigs() -> dict:
    """Return {full_addr: (name, cfg_sig)}. When multiple banks specify
    different sigs for the same address (typically the defining bank as a
    `func ... sig:void()` and a caller bank as `name ... sig:uint8()`),
    the more-specific sig wins: uint8/uint16/complex > void.

    This resolves cross-bank disagreement so funcs.h ends up with the sig
    callers actually expect. The disagreement itself usually indicates that
    the defining bank's AUTO sig was wrong — the function returns a value
    (Y register, typically) that cross-bank callers DO consume."""

    # Use recomp._sig_specificity so this tool picks the same "more
    # informative" sig the recompiler would prefer at reconciliation time.
    specificity = recomp._sig_specificity

    rom = load_rom(str(ROM_PATH))
    # Load the CURRENT funcs.h so we seed each bank's cfg.sigs with the
    # same info run_config would see at reconciliation time. Without this
    # step, a function declared `uint8 foo(uint8 k);` in funcs.h looks
    # like a plain `void()` to the augment pass (cfg alone has no sig),
    # so the RetAY caller-usage promotion never fires in sync_funcs_h
    # even though it does in the per-bank regen. That disagreement
    # would then surface as C2371 (decl vs funcs.h mismatch) at build
    # time after sync stops rewriting the outdated declaration.
    funcs_h_sigs = recomp.parse_funcs_h(str(FUNCS_H))
    sigs = {}
    for bank in BANKS:
        cfg_path = RECOMP_DIR / f'bank{bank}.cfg'
        cfg = recomp.parse_config(str(cfg_path))
        # Mirror the preprocessing pipeline that run_config performs, so
        # inferred parameters reach funcs.h and cross-bank callers pass the
        # right register values. (Rule 0: ROM is authoritative.)
        #   1. Seed cfg.sigs from funcs.h for every name entry that funcs.h
        #      declares but cfg doesn't. Matches run_config's step-1
        #      reconciliation so void/uint8 return types are visible to
        #      the augment pass (and the void/uint8 -> RetY/RetAY
        #      promotions fire on the same basis in both contexts).
        #   2. Auto-promote intra-bank branch targets so BRA/BCC into
        #      sibling-function bodies become sub-entries (matches
        #      run_config's auto_promote_branch_targets pass).
        #   3. Promote `name ...` sub-entries to real funcs so their
        #      bodies get liveness inference applied.
        #   4. Run live-in inference to derive A/X/Y register parameters.
        for addr, name in cfg.names.items():
            if addr in cfg.sigs:
                continue
            fh_sig = funcs_h_sigs.get(name)
            if fh_sig is not None:
                cfg.sigs[addr] = fh_sig
        recomp.auto_promote_branch_targets(rom, cfg)
        recomp.promote_sub_entries(rom, cfg)
        recomp.augment_cfg_sigs_from_livein(rom, cfg)
        for fname, addr, sig, _end, _mo, _h in cfg.funcs:
            full_addr = (cfg.bank << 16) | addr
            # cfg.sigs was augmented by the live-in pass, so it's the
            # single source of truth. Tuple sig is the raw cfg declaration.
            sig = cfg.sigs.get(full_addr, sig)
            existing = sigs.get(full_addr)
            if existing is None or specificity(sig) > specificity(existing[1]):
                sigs[full_addr] = (fname, sig)
        for full_addr, name in cfg.names.items():
            name_sig = cfg.sigs.get(full_addr)
            existing = sigs.get(full_addr)
            if existing is None or specificity(name_sig) > specificity(existing[1]):
                sigs[full_addr] = (name, name_sig)
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


def _rom_authoritative_sig(cfg_sig: str, fh_sig: str) -> str:
    """Merge a cfg-derived sig with funcs.h's existing declaration so funcs.h
    reflects exactly what the recompiler emits without losing hand-analyzed
    conventions.

    Two information sources, two different strengths:
      * cfg + live-in inference (ROM-derived): authoritative for which
        register parameters (A/X/Y) a function consumes, and for any sigs
        the cfg declares explicitly.
      * funcs.h (hand-decompiled): authoritative for complex return types
        (struct returns, pointer returns, bool-via-carry) and for direct-
        page / pointer parameter conventions that don't show up as live-in
        registers.

    Strategy: union the parameter lists (funcs.h first, appending any cfg
    params by name that aren't in funcs.h), and pick the more informative
    return type (non-void and complex beats void).
    """
    if cfg_sig is None:
        return fh_sig
    if fh_sig is None:
        return cfg_sig
    cfg_ret, cfg_params = recomp.parse_sig(cfg_sig)
    fh_ret, fh_params = recomp.parse_sig(fh_sig)

    # Return type: prefer funcs.h when it declares a real return and cfg
    # flattened to void (common when cfg had no explicit return info and the
    # live-in pass left the void default). Cfg's explicit non-void return
    # still wins over funcs.h's void.
    if cfg_ret != 'void':
        chosen_ret = cfg_ret
    elif fh_ret != 'void':
        chosen_ret = fh_ret
    else:
        chosen_ret = 'void'

    # Params: funcs.h first (preserves DP / pointer param conventions), then
    # append cfg params whose bare names don't already appear in funcs.h.
    def _bare(name):
        return name.lstrip('*')
    fh_bare_names = {_bare(n) for _t, n in fh_params}
    merged_params = list(fh_params)
    for t, n in cfg_params:
        if _bare(n) not in fh_bare_names:
            merged_params.append((t, n))
            fh_bare_names.add(_bare(n))

    if not merged_params:
        return f'{chosen_ret}()'
    return f'{chosen_ret}(' + ','.join(f'{t}_{n}' for t, n in merged_params) + ')'


def main() -> int:
    funcs_h_sigs = recomp.parse_funcs_h(str(FUNCS_H))

    # Build {fname: cfg_sig}. When multiple addresses share a name — e.g. a
    # bank-crossing trampoline `name 01:801A foo` alongside the real body
    # `func foo 02:D294` — prefer the more-specific sig so funcs.h reflects
    # the actual body's return convention (RetAY) rather than the
    # trampoline's flattened view (uint8). Without this, recomp.py regen
    # promotes the body to RetAY locally while funcs.h still says uint8,
    # and every caller's assignment `v = foo(...)` fails with
    # `cannot convert from 'uint8' to 'RetAY'`.
    #
    # Layered: first the cfg-derived sigs (what the sim-augment computed),
    # then overlay the actual gen-file body sigs. Gen is authoritative for
    # the *current regen cycle*: whatever recomp.py actually emitted into
    # a `Type Fname(uint8 k) { // ADDR` body header is what callers are
    # going to link against, so funcs.h must agree with that exact shape.
    specificity = recomp._sig_specificity
    cfg_sigs_by_name: dict = {}
    for _addr, (fname, sig) in collect_cfg_sigs().items():
        if sig is None:
            continue
        existing = cfg_sigs_by_name.get(fname)
        if existing is None or specificity(sig) > specificity(existing):
            cfg_sigs_by_name[fname] = sig
    # Track which fnames have an actual emitted body in a gen file. For those,
    # the body sig is the single source of truth — funcs.h must match exactly,
    # no param union with the existing funcs.h line. Without this override
    # the union path keeps stale params from a prior buggy regen (real case:
    # a dispatch target widened to (uint8 k, uint8 a) by an earlier augment
    # bug stayed widened in funcs.h even after recomp.py narrowed the body
    # back to (uint8 k), because the union appended the stale `a`).
    gen_body_fnames: set = set()
    for _addr, (fname, sig) in collect_gen_sigs().items():
        if sig is None:
            continue
        cfg_sigs_by_name[fname] = sig
        gen_body_fnames.add(fname)

    # Reconcile per function. Only rewrite if the reconciled sig differs from
    # the current funcs.h sig; leave the original line otherwise.
    rewrites: dict = {}
    for fname, cfg_sig in cfg_sigs_by_name.items():
        fh_sig = funcs_h_sigs.get(fname)
        if fname in gen_body_fnames:
            # Gen body is authoritative; funcs.h must match verbatim.
            reconciled = cfg_sig
        else:
            reconciled = _rom_authoritative_sig(cfg_sig, fh_sig)
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
