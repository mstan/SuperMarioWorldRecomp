#!/usr/bin/env python3
"""Apply the optional override layer to freshly generated banks.

Runs at build time, AFTER snesrecomp emits src/gen/ and BEFORE compilation.
For each rule in a manifest it injects an idempotent, runtime-gated dispatch
prologue into the matching generated function(s), so the override layer
survives regeneration without anyone hand-editing src/gen/.

See overrides/README.md for the full design and the contract override bodies
must follow.

Usage:
    python tools/apply_overrides.py [--gen-dir src/gen]
        [--manifest overrides/widescreen/overrides.manifest] [--check] [-v]

With no manifest rules active this is a no-op (authentic build). Safe to run
on every build: injection is marked and skipped if already present.
"""
import argparse
import os
import re
import sys

MARKER = "/*WS-OVERRIDE*/"

# --- Block-level widescreen patches (runtime-gated, default-off) ---------
# Some widescreen behaviour can't be a whole-function override (the recompiled
# functions carry CpuState/NLR plumbing). Instead we inject a small gated
# snippet right after a specific recompiled basic-block anchor. Each patch is
# anchored on a unique generated line so it targets exactly one site per
# (m,x) variant, and is idempotent via its marker.
#
# WS-FLAG: widen GetDrawInfo's draw/cull window ONLY. Vanilla GetDrawInfo
# (banks 01/02/03) culls a sprite when screen-x is outside [-64, 320)
# (tx = sprX + 0x40 - camX; cull when tx >= 0x180): it stores the cull bool to
# spr_table15c4 ($15C4) and double-returns so the GFX routine never draws.
# For widescreen we widen that window to [-(64+g_ws_extra), 256+g_ws_extra):
# 64 = vanilla's pad for the widest sprites straddling the left edge, mirrored
# at the widescreen edge; the right bound needs no pad (tiles extend rightward
# from the origin and the presentation crops them).
#
# CRITICAL — do NOT touch spr_xoffscreen ($15A0). It is set earlier in
# GetDrawInfo to "screen-x not in [0,256)" and it IS the OAM x-high (9th
# position bit) source: generic draw routines emit
# `sprites_oamtile_size_buffer[slot+64] = $15A0|2` and FinishOAMWrite
# recomputes the same predicate from world coords. With vanilla $15A0, margin
# sprites get x-high=1 and the PPU's widened sprite-x wrap threshold
# (256+g_ws_extra) presents 9-bit x >= threshold as negative (left margin).
# A previous revision of this patch forced $15A0=0 inside the widescreen
# window; that zeroed the x-high bit and made every left-margin sprite
# (incl. Yoshi) render 256px to the RIGHT — the classic 9-bit wrap teleport.
#
# 9-bit representability bounds the widening: left tiles live at 512-(64+extra)
# .. 511 and must stay >= the wrap threshold 256+extra, so extra < 96. The
# snippet clamps. No-op when widescreen is off.
#
# WS-DESPAWN: widen SubOffscreen's horizontal erase bounds by g_ws_extra.
# Each bank (01/02/03) carries its own copy of SubOffscreen with its own
# bounds tables (contents differ per bank!). The horizontal decision joins
# at one block per bank: `LDA $00 : BPL keep / fall into EraseSprite`,
# where $00 = hibyte(bound[r1] + camera - sprX), sign-flipped for odd r1
# (left bounds), and Y still holds r1. The snippet re-derives the full
# 16-bit comparison from the bank's own table (read via DB like the
# original) and rewrites $00 with the widened verdict (0x80 = erase,
# 0x00 = keep) just before the vanilla load tests it. Even r1 = right
# bound (erase if sprX > cam+bound+extra), odd r1 = left bound (erase if
# sprX < cam+bound-extra). extra=0 reduces exactly to vanilla. The
# offscreen GATE ($15A0|$186C) stays vanilla — it is also the x-high bit
# (see WS-FLAG) and only opens the despawn check, never forces it.
#
# Left bounds get a cushion, not just a shift: vanilla left bounds encode
# each class's width slack against a screen edge at 0 (Entry1 -0x40,
# Entry3 only -0x10, bank03 even +0x40). A uniform -extra shift keeps
# that slack, but the widescreen edge is at -extra and a 16-32px sprite
# of a shallow-bound class still blinks out visibly inside the margin
# (user-observed: left-side despawn premature, right side perfect). So
# every left bound is clamped to at least -(64+extra) — the same
# widest-sprite cushion as the WS-FLAG draw window — while deeper vanilla
# bounds (e.g. -0x70) keep their extra slack. Right bounds are origin-
# based with the body extending away from view; the plain shift is
# already invisible there. Hysteresis: spawn column -0x30-extra stays
# inside the shallowest left despawn -(64+extra).
#
# WS-SPAWN: shift ParseLevelSpriteList's spawn-trigger column outward by
# g_ws_extra so sprites spawn beyond the visible widescreen margin
# instead of materializing inside it. Vanilla parses the level sprite
# list at column camera-0x30 when scrolling left / camera+0x120 when
# scrolling right ($55 = 0/2; $5B bit0 = vertical level, left untouched
# — widescreen is horizontal-only). At the join block $02A828 the column
# low byte (&0xF0) is in $00 and the high byte is in A; the snippet
# recomputes both with the widened offset. Hysteresis vs WS-DESPAWN is
# preserved: both sides move by the same extra (vanilla gap: spawn
# +0x120 < erase +0x130; spawn -0x30 > erase -0x40). If the widened
# column would precede the level start (<0) the vanilla column is kept.


def _ws_despawn_patch(anchor_pc, tbl_lo):
    """Per-bank WS-DESPAWN entry. anchor_pc = the `LDA $00` join block;
    tbl_lo = that bank's 8-entry bounds table (hi bytes at tbl_lo+8)."""
    return {
        "marker": "/*WS-DESPAWN*/",
        # Anchor on the unique trace line of the join block; every
        # emitted copy of that block (entry funcs x (m,x) variants) is
        # the despawn body, so no func name scoping is needed.
        "func_match": "",
        "anchor": "cpu_trace_block(cpu, 0x%06X)" % anchor_pc,
        "snippet": (
            " /*WS-DESPAWN*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active) {"
            " unsigned int _wk = cpu->X & 0xffffu; unsigned int _wy = cpu->Y & 7u;"
            " int _wb = (int)(short)(unsigned short)("
            "cpu_read8(cpu,cpu->DB,(unsigned short)(0x%04Xu+_wy))"
            " | (cpu_read8(cpu,cpu->DB,(unsigned short)(0x%04Xu+_wy))<<8));"
            " int _wv = (int)(short)(unsigned short)((unsigned int)(_wb"
            " + (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8))"
            " - (cpu_read8(cpu,0x7E,(unsigned short)(0x00E4+_wk))"
            " | (cpu_read8(cpu,0x7E,(unsigned short)(0x14E0+_wk))<<8))) & 0xFFFFu);"
            " int _wthr = (_wb <= -64) ? g_ws_extra : (_wb + 64 + g_ws_extra);"
            " int _werase = (_wy & 1) ? (_wv >= _wthr) : (_wv < -g_ws_extra);"
            " cpu_write8(cpu,0x7E,(unsigned short)(cpu->D + 0x0000),"
            " (unsigned char)(_werase ? 0x80 : 0x00)); } }"
        ) % (tbl_lo, tbl_lo + 8),
    }


BLOCK_PATCHES = [
    {
        "marker": "/*WS-FLAG*/",
        # Only inside GetDrawInfo* (banks 01/02/03; normal sprites use the
        # bank-01 copy). Anchor on the spr_table15c4 ($15C4) write — the
        # draw-cull store, present once per variant, immediately before the
        # branch `if (_flag_Z == 0) goto <off-screen double-return>`.
        "func_match": "GetDrawInfo",
        "anchor": "0x15c4 + (uint32)cpu->X",
        # Recompute the draw decision against the widened window and override
        # the branch flags (_flag_Z: Z=1 means draw, mirroring vanilla's
        # `AND #1` result of 0) plus the just-stored $15C4 cull bool. A is dead
        # past this point on both paths (reloaded immediately); $15A0 is left
        # at its vanilla value on purpose (see header comment).
        "snippet": (
            " /*WS-FLAG*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active) {"
            " unsigned int _wk = cpu->X & 0xffffu;"
            " int _we = g_ws_extra > 95 ? 95 : g_ws_extra;"
            " int _wsx = (int)(short)("
            "(cpu_read8(cpu,0x7E,(unsigned short)(0x00E4+_wk))"
            " | (cpu_read8(cpu,0x7E,(unsigned short)(0x14E0+_wk))<<8))"
            " - (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8)) );"
            " int _wdraw = (_wsx >= -(64 + _we) && _wsx < 256 + _we);"
            " cpu->_flag_Z = _wdraw ? 1 : 0; cpu->_flag_C = _wdraw ? 0 : 1;"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x15C4+_wk), _wdraw ? 0 : 1); } }"
        ),
    },
    # WS-DESPAWN (see header comment): one entry per bank copy.
    _ws_despawn_patch(0x01AC7C, 0xAC11),  # SubOffscreen_Bank01_* join + tables
    _ws_despawn_patch(0x02D076, 0xD007),  # SubOffscreen_Bank02_* join + tables
    _ws_despawn_patch(0x03B8A8, 0xB83F),  # SubOffscreen_Bank03_* join + tables
    # WS-SPAWN (see header comment): bank-02 ParseLevelSpriteList only.
    {
        "marker": "/*WS-SPAWN*/",
        "func_match": "ParseLevelSpriteList",
        "anchor": "cpu_trace_block(cpu, 0x02A828)",
        "snippet": (
            " /*WS-SPAWN*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && !(cpu_read8(cpu,0x7E,0x005B) & 1)) {"
            " unsigned int _wd = cpu_read8(cpu,0x7E,0x0055);"
            " if (_wd == 0 || _wd == 2) {"
            " int _wcol = (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8))"
            " + ((_wd == 0) ? -(0x30 + g_ws_extra) : (0x120 + g_ws_extra));"
            " if (_wcol >= 0) {"
            " cpu_write8(cpu,0x7E,(unsigned short)(cpu->D + 0x0000),"
            " (unsigned char)(_wcol & 0xF0));"
            " cpu_write_a_m(cpu, (uint16)((_wcol >> 8) & 0xFF)); } } } }"
        ),
    },
]

# Recognize a generated function definition header to scope block patches.
_FUNC_HDR = re.compile(r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*CpuState")


def apply_block_patches(text):
    """Apply BLOCK_PATCHES to one file's text, function-scoped. Returns (text, n)."""
    n = 0
    for p in BLOCK_PATCHES:
        if p["anchor"] not in text:
            continue
        out = []
        cur_func = None
        for line in text.splitlines(keepends=True):
            mh = _FUNC_HDR.match(line)
            if mh:
                cur_func = mh.group(1)
            if (p["anchor"] in line and p["marker"] not in line
                    and cur_func and p["func_match"] in cur_func):
                line = line.rstrip("\n") + p["snippet"] + "\n"
                n += 1
            out.append(line)
        text = "".join(out)
    return text, n

# Matches a generated function DEFINITION (opening brace), not a forward
# declaration (which ends in ';'). Captures the base name and the _M?X? suffix.
#   RecompReturn  SomeName_M1X1 ( CpuState *cpu ) {
DEF_RE = re.compile(
    r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*?)(_M[01]X[01])\s*"
    r"\(\s*CpuState\s*\*\s*cpu\s*\)\s*\{",
    re.MULTILINE,
)


def parse_manifest(path):
    """Return list of (base_name, override_symbol, variant_or_None)."""
    rules = []
    if not os.path.isfile(path):
        return rules
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if "->" not in line:
                sys.exit(f"apply_overrides: malformed manifest line: {raw!r}")
            lhs, rhs = line.split("->", 1)
            base = lhs.strip()
            parts = rhs.split()
            override = parts[0].strip()
            variant = parts[1].strip() if len(parts) > 1 else None
            rules.append((base, override, variant))
    return rules


def prologue(override_symbol):
    return (
        f" {MARKER} {{ extern bool g_ws_active;"
        f" extern RecompReturn {override_symbol}(CpuState *cpu);"
        f" if (g_ws_active) return {override_symbol}(cpu); }}"
    )


def apply_to_text(text, rules):
    """Return (new_text, n_injected). Idempotent."""
    by_base = {}
    for base, override, variant in rules:
        by_base.setdefault(base, []).append((override, variant))

    injected = 0

    def repl(m):
        nonlocal injected
        whole = m.group(0)
        base, suffix = m.group(1), m.group(2)
        cands = by_base.get(base)
        if not cands:
            return whole
        # Pick a rule whose variant matches this definition (or is unscoped).
        chosen = None
        for override, variant in cands:
            if variant is None or variant == suffix[1:]:  # suffix like '_M1X1'
                chosen = override
                break
        if chosen is None:
            return whole
        if MARKER in whole:  # already injected on a previous build
            return whole
        return whole + prologue(chosen)

    new_text = DEF_RE.sub(repl, text)
    # Count injections by counting freshly added markers vs pre-existing.
    return new_text, new_text.count(MARKER) - text.count(MARKER)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen-dir", default="src/gen")
    ap.add_argument(
        "--manifest", default="overrides/widescreen/overrides.manifest"
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="verify every manifest base matched at least one definition",
    )
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    rules = parse_manifest(args.manifest)
    if not rules and not BLOCK_PATCHES:
        if args.verbose:
            print("apply_overrides: no active rules — authentic build, no-op")
        return 0

    if not os.path.isdir(args.gen_dir):
        sys.exit(f"apply_overrides: gen dir not found: {args.gen_dir}")

    matched_bases = set()
    total = 0
    for name in sorted(os.listdir(args.gen_dir)):
        if not name.endswith(".c"):
            continue
        path = os.path.join(args.gen_dir, name)
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        # Track which bases exist in this file before substitution.
        for m in DEF_RE.finditer(text):
            if m.group(1) in {b for b, _, _ in rules}:
                matched_bases.add(m.group(1))
        new_text, n = apply_to_text(text, rules)
        new_text, nb = apply_block_patches(new_text)
        if n or nb:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new_text)
            total += n + nb
            if args.verbose:
                print(f"apply_overrides: {name}: injected {n} prologue(s), {nb} block patch(es)")

    if args.check:
        missing = {b for b, _, _ in rules} - matched_bases
        if missing:
            sys.exit(
                "apply_overrides: manifest bases never matched a definition: "
                + ", ".join(sorted(missing))
            )

    print(f"apply_overrides: injected {total} dispatch prologue(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
