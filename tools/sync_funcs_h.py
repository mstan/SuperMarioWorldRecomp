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
SRC_DIR = REPO / 'src'
# Framework runtime lives under snesrecomp/runner/src/. Hand bodies there
# are legitimate (RtlApuWrite, WatchdogCheck, debug-server helpers).
# Hand bodies in src/*.c are smw-rev scaffolding smells the rip plan is
# actively deleting — we still scan them so funcs.h stays link-consistent
# WHILE they exist, but their presence is not a green light to keep them.
RUNNER_SRC_DIR = REPO / 'snesrecomp' / 'runner' / 'src'
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

# Hand-body definition header: any `ret_type fname(params) {` at line start,
# used to find functions that have a C body outside of src/gen/ or inside
# a cfg verbatim_start/verbatim_end block. These bodies represent hand-
# written ABI that funcs.h must preserve over any gen-derived narrowing.
_HAND_BODY_RE = re.compile(
    r'^(?:static\s+)?(?P<ret>\w[\w\s\*]*?)\s+(?P<name>\w+)\s*\([^)]*\)\s*\{',
    re.MULTILINE,
)


def collect_hand_body_fnames() -> set:
    """Return {fname} for every function with a hand-written body.

    Sources scanned:
      * src/*.c (excluding src/gen/ which is recompiler output).
      * recomp/bank*.cfg, inside `verbatim_start ... verbatim_end` blocks
        (excluding bisect files — those are ad-hoc debug cfgs).

    A function that appears here is one funcs.h cannot safely update
    from gen alone: its hand body is the ABI oracle, and live-in
    analysis's known blind spots (mid-body PH/PL scribble-restore,
    DP-indirect reads) can make gen drop a param the hand body
    legitimately consumes. For these, _rom_authoritative_sig's union-
    against-funcs.h path preserves hand-caller-proven params.

    A function NOT in this set has no hand body — only cfg + gen are
    sources of truth. funcs.h must follow the cfg/gen sig unconditionally
    (no stale struct returns or widened params from deleted hand
    wrappers).
    """
    fnames: set = set()
    # Scan both src/*.c (per-game scaffolding + any tiny legit WRAM helpers)
    # and snesrecomp/runner/src (framework runtime). We count bodies from
    # both so funcs.h declares everything that currently has a definition
    # — otherwise cross-bank callers fail to link. Once the rip deletes a
    # src/*.c body, its decl becomes an orphan and this tool's orphan-
    # deletion pass removes it.
    scan_paths: list = []
    scan_paths.extend(sorted(SRC_DIR.glob('*.c')))
    if RUNNER_SRC_DIR.exists():
        scan_paths.extend(sorted(RUNNER_SRC_DIR.glob('*.c')))
        scan_paths.extend(sorted(RUNNER_SRC_DIR.glob('snes/*.c')))
    for p in scan_paths:
        try:
            text = p.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        for m in _HAND_BODY_RE.finditer(text):
            fname = m.group('name')
            # Skip common non-function matches (struct initializers labeled
            # by compound-literal pattern don't match our regex, but a
            # `static const` array sometimes does — the ret captures the
            # array's type. Filter obvious false positives.)
            ret = m.group('ret').strip()
            if ret in ('if', 'for', 'while', 'switch', 'do', 'return'):
                continue
            fnames.add(fname)
    # recomp/bank*.cfg verbatim blocks
    for cfg_path in sorted(RECOMP_DIR.glob('bank*.cfg')):
        if 'bisect' in cfg_path.name:
            continue
        try:
            text = cfg_path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        in_verbatim = False
        buf: list = []
        for line in text.splitlines():
            stripped = line.strip()
            if stripped == 'verbatim_start':
                in_verbatim = True
                buf = []
                continue
            if stripped == 'verbatim_end':
                in_verbatim = False
                block = '\n'.join(buf)
                for m in _HAND_BODY_RE.finditer(block):
                    ret = m.group('ret').strip()
                    if ret in ('if', 'for', 'while', 'switch', 'do', 'return'):
                        continue
                    fnames.add(m.group('name'))
                buf = []
                continue
            if in_verbatim:
                buf.append(line)
    return fnames


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
    # Split cfg sigs by whether the function has a hand-written body.
    # Hand bodies are the ABI oracle: live-in analysis has blind spots
    # (mid-body PH/PL scribble-restore, DP-indirect reads) that can make
    # gen drop a param the hand body really consumes, so funcs.h unions
    # against gen (existing behavior).
    #
    # For functions WITHOUT a hand body, funcs.h cannot justify keeping
    # a declaration that disagrees with cfg+gen. Stale struct returns and
    # widened params from deleted wrappers must be dropped — otherwise
    # un-skipping a function is impossible without hand-editing funcs.h
    # (a rule-7 violation). So cfg_sig_before_gen (from cfg.sigs directly)
    # is retained for those; after the gen overlay, the reconciliation
    # loop checks hand_body_fnames and picks the right source.
    cfg_sig_before_gen: dict = dict(cfg_sigs_by_name)
    hand_body_fnames = collect_hand_body_fnames()
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
    #
    # Gen body is authoritative only when the emitted sig fits the
    # dispatch-capped shape (`()` or `(uint8 k)` with any return). That
    # is the original purpose of gen-body-wins: `_augment_cfg_sigs_one_
    # pass` narrows dispatch targets to a FuncU8-compatible shape, and
    # funcs.h must follow. For every other gen body, we can't safely
    # treat gen as ground truth — hand-written src/smw_*.c callers are
    # the real oracle for ABI, and live-in analysis has known blind
    # spots (mid-body scribble-restore patterns, DP-indirect reads,
    # etc.) that can make gen drop a param the ROM actually consumes.
    # So fall back to a union against funcs.h, which preserves any
    # hand-caller-proven params.
    rewrites: dict = {}
    for fname, cfg_sig in cfg_sigs_by_name.items():
        fh_sig = funcs_h_sigs.get(fname)
        if (fname in gen_body_fnames and fname not in hand_body_fnames
                and not (fh_sig and recomp._ret_is_pointer(fh_sig))):
            # No hand body anywhere. funcs.h cannot justify a declaration
            # that disagrees with the recompiler's two information
            # sources (cfg and gen). Build the reconciled sig from those
            # two directly, ignoring funcs.h:
            #
            #   * return type: cfg wins. When the cfg author explicitly
            #     declares a return (typically void, verified against
            #     SMWDisX), that's ROM truth. A struct return in funcs.h
            #     is a stale artifact of a deleted hand wrapper; gen
            #     emitting the same struct is just funcs.h's specificity
            #     bias feeding back through _reconcile_sig. Without this
            #     we can't drop stale PointU8/RetAY/etc. once the hand
            #     wrapper is removed.
            #
            #   * parameter list: gen wins. Live-in analysis runs during
            #     the full regen pipeline; it adds (uint8 k), (uint8 j),
            #     etc. when the ROM body really consumes them. cfg's
            #     pre-augment param list can be stale (AUTO entries
            #     without explicit sig come out as void()).
            #
            # Pointer-return exception: funcs.h pointer returns are
            # preserved (handled by the `not _ret_is_pointer(fh_sig)`
            # guard above) even without a hand body. Mirrors
            # recomp._reconcile_sig's pointer-return carve-out: SNES
            # code communicates pointer returns via DP writes, so the
            # recompiler emits a void body while funcs.h advertises
            # the pointer for hand callers (LmHook_* being the common
            # case).
            cfg_pre = cfg_sig_before_gen.get(fname, cfg_sig)
            cfg_ret, cfg_params = recomp.parse_sig(cfg_pre)
            gen_ret, gen_params = recomp.parse_sig(cfg_sig)  # cfg_sig == gen sig here
            # Merge params: start with cfg's explicit params (includes
            # live-in augment results, which is ROM-truth), then add any
            # extra REGISTER params from gen that cfg doesn't have. Skip
            # non-register param types (pointers, structs) — those can
            # only come from a funcs.h hand declaration, not from
            # live-in analysis; since we're in the no-hand-body branch,
            # they're stale and must be dropped.
            cfg_param_names = {n for _t, n in cfg_params}
            REG_PARAM_NAMES = {'k', 'j', 'a', 'x', 'y'}
            merged_params = list(cfg_params)
            for t, n in gen_params:
                if n in cfg_param_names:
                    continue
                if n in REG_PARAM_NAMES:
                    merged_params.append((t, n))
            if not merged_params:
                reconciled = f'{cfg_ret}()'
            else:
                reconciled = (
                    f'{cfg_ret}('
                    + ','.join(f'{t}_{n}' for t, n in merged_params)
                    + ')'
                )
        elif fname in gen_body_fnames and recomp._sig_matches_dispatch_shape(cfg_sig):
            # Dispatch-capped narrowing (hand body exists — hand_body_fnames
            # catch above didn't fire). Gen body wins verbatim so the
            # FuncU8 cast type at the dispatch call site matches. Without
            # this, funcs.h might keep a widened sig that disagrees with
            # the FuncU8* cast, producing a C compile error.
            reconciled = cfg_sig
        else:
            reconciled = _rom_authoritative_sig(cfg_sig, fh_sig)
        if reconciled is None or reconciled == fh_sig:
            continue
        rewrites[fname] = reconciled

    # Rewrite funcs.h line by line. Match declarations via the same regex
    # parse_funcs_h uses and substitute when a rewrite exists.
    decl_re = re.compile(r'^(\s*)(\w[\w\s\*]*?)\s+(\w+)\s*\(([^)]*)\)\s*;(.*)$')
    seen_fnames: set = set()
    insert_block_begin = '// --- auto-inserted declarations (sync_funcs_h.py) ---'
    insert_block_end = '// --- end auto-inserted declarations ---'
    lines_out = []
    changes = 0
    # Strip any prior auto-inserted block so this run rebuilds it cleanly.
    in_auto_block = False
    with open(FUNCS_H) as fh:
        raw_lines = fh.readlines()
    cleaned: list = []
    for raw in raw_lines:
        if raw.strip() == insert_block_begin:
            in_auto_block = True
            continue
        if raw.strip() == insert_block_end:
            in_auto_block = False
            continue
        if in_auto_block:
            continue
        cleaned.append(raw)

    # Orphan-deletion criterion: a declaration is an orphan iff
    #   (a) no cfg/gen emits it (not in cfg_sigs_by_name), AND
    #   (b) no hand body anywhere in src/ or snesrecomp/runner/src/ defines
    #       it (not in hand_body_fnames).
    # Orphans accumulate when hand-written HLE functions get deleted (e.g.
    # Tier-1 scaffolding rip): their bodies go away but nothing in this
    # tool removed the declaration — callers would now fail to link.
    hand_body_fnames = collect_hand_body_fnames()
    # Dedup: keep only the first declaration per fname.
    for raw in cleaned:
        m = decl_re.match(raw)
        if m:
            leading, _ret_raw, fname, _params_raw, trailing = m.groups()
            is_orphan = (fname not in cfg_sigs_by_name
                         and fname not in hand_body_fnames)
            is_dup = fname in seen_fnames
            if is_orphan:
                print(f'  [delete] orphan decl (no cfg, no hand body): {fname}')
                changes += 1
                continue
            if is_dup:
                print(f'  [delete] duplicate decl: {fname}')
                changes += 1
                continue
            seen_fnames.add(fname)
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

    # Insert pass: any recompiler-emitted function (gen body or cfg func)
    # that has no declaration in funcs.h needs one. Without this step the
    # cross-bank C link works only for functions whose declarations were
    # hand-placed; new sub-entries and newly-promoted funcs stay invisible
    # to other banks' callers (and to the recompiler's reconciliation
    # seed). NEVER hand-edit funcs.h to fix this -- extend this tool.
    inserts: list = []
    for fname in sorted(cfg_sigs_by_name):
        if fname in seen_fnames:
            continue
        sig = cfg_sigs_by_name[fname]
        if sig is None:
            continue
        inserts.append(cfg_sig_to_c_decl(fname, sig))
    if inserts:
        # Place the auto-block immediately before the final `#endif` so
        # hand-curated sections earlier in the file stay untouched.
        endif_idx = None
        for i in range(len(lines_out) - 1, -1, -1):
            if lines_out[i].lstrip().startswith('#endif'):
                endif_idx = i
                break
        block = [
            '\n',
            insert_block_begin + '\n',
            '// Generated by tools/sync_funcs_h.py. Do NOT hand-edit.\n',
        ]
        for decl in inserts:
            block.append(decl + '\n')
            print(f'  [insert] {decl}')
        block.append(insert_block_end + '\n')
        if endif_idx is not None:
            lines_out = lines_out[:endif_idx] + block + lines_out[endif_idx:]
        else:
            lines_out.extend(block)
        changes += len(inserts)

    FUNCS_H.write_text(''.join(lines_out))
    print(f'\nRewrote {changes} declarations in {FUNCS_H}')
    # Scaffolding-smell metric: count hand bodies remaining in src/*.c
    # (excluding src/gen/ and the runner). Per the rip plan these should
    # trend toward a very small WRAM-helper-only residual.
    src_smells = 0
    for p in sorted(SRC_DIR.glob('*.c')):
        try:
            text = p.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        for m in _HAND_BODY_RE.finditer(text):
            ret = m.group('ret').strip()
            if ret in ('if', 'for', 'while', 'switch', 'do', 'return'):
                continue
            src_smells += 1
    print(f'src/*.c hand-body count: {src_smells} (scaffolding smell, should shrink)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
