#!/usr/bin/env python3
"""Install runtime-gated Falcon hooks after regeneration.

The adaptive renderer uses apply_renderer_hooks.py separately. Legacy
widescreen patches are no longer installed. --check verifies required seams.
"""
import argparse
import os
import re
import sys

MARKER = "/*WS-OVERRIDE*/"
HOOK_MARKER = "/*SMW-HOOK*/"

BLOCK_PATCHES = [
    # FALCON-AFTER-PHYSICS: ordinary PlayerState00 reaches $00:CD36 by
    # fall-through from the live $00:CD24 routine.  The separately emitted
    # CD36 entry is only an external dispatch target, so its @hook does not
    # publish native wall/floor results to Falcon during normal gameplay.
    # Patch the trace-labelled inline block in its exact M1X1 owner.
    {
        "marker": "/*FALCON-AFTER-PHYSICS*/",
        "check_exactly_once": True,
        "func_match": "PlayerState00_00CD24_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x00CD36)",
        "snippet": (
            " /*FALCON-AFTER-PHYSICS*/ {"
            " extern void SmwFalconAfterPhysics(CpuState *cpu);"
            " SmwFalconAfterPhysics(cpu); }"
        ),
    },
    # FALCON-BLOCK-SWEEP: destructible block handling can return before the
    # post-physics CD36 seam, and levels without normal sprites do not provide
    # a useful 80D2 fallback.  Patch the live RunPlayerBlockCode entry so an
    # active Falcon Punch/Kick applies its authored block-only volume before
    # native single-contact block handling breaks just the underfoot tile.
    {
        "marker": "/*FALCON-BLOCK-SWEEP*/",
        "check_exactly_once": True,
        "func_match": "RunPlayerBlockCode_00EE3A_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x00EE3A)",
        "snippet": (
            " /*FALCON-BLOCK-SWEEP*/ {"
            " extern void SmwFalconOnPlayerBlockCode(CpuState *cpu);"
            " SmwFalconOnPlayerBlockCode(cpu); }"
        ),
    },
    # FALCON-STEP-CRUSH: native co-op boundaries split the $00:E92B body.
    # Cover every emitted owner of $00:E9FB, including both E9C8 widths and
    # the E9A1 continuation reached by ordinary player physics. Each owner
    # observes the seam before reading $77 and branching to $00:EA08.
    {
        "marker": "/*FALCON-STEP-CRUSH*/",
        "check_exactly_once": True,
        "func_match": ("CoopNative_00E9A1_M1X1",
                       "HandlePlayerLevelColl_00E9C8_M1X1",
                       "HandlePlayerLevelColl_00E9C8_M0X1",
                       "HandlePlayerLevelColl_00E9FB_M1X1"),
        "expected_matches": 4,
        "anchor": "cpu_trace_block(cpu, 0x00E9FB)",
        "snippet": (
            " /*FALCON-STEP-CRUSH*/ {"
            " extern void SmwFalconBeforeCrushCheck(CpuState *cpu);"
            " SmwFalconBeforeCrushCheck(cpu); }"
        ),
    },
    # FALCON-STOMP: BoostMarioSpeed ($01:AA33) is reached only after SMW's
    # normal-sprite interaction chose the stomp path. At its $01:AA41 return,
    # native code has already written the exact D0/A8 player bounce speed.
    # Observe it there so the controller adopts the host impulse without
    # changing contact eligibility, enemy state, score, or SFX.
    {
        "marker": "/*FALCON-STOMP-BOUNCE*/",
        "func_match": "BoostMarioSpeed",
        "anchor": "cpu_trace_block(cpu, 0x01AA41)",
        "snippet": (
            " /*FALCON-STOMP-BOUNCE*/ {"
            " extern void SmwFalconOnNativeStompBounce(CpuState *cpu);"
            " SmwFalconOnNativeStompBounce(cpu); }"
        ),
    },
    # FALCON-YOSHI: Spr035_Yoshi arrives at $01:ED38 only after its ordinary
    # off-Yoshi movement, clipping, and CheckForContact have completed. The
    # following blocks are the sole fresh-mount path: on an eligible contact
    # they write C2=1, then PlayerDraw turns that into rider/carry-over/
    # colour/facing and emits the associated sound/bounce/position effects.
    # A function-entry hook clears a restored C2=1 before the earlier mounted
    # fast-path; this narrow successful-contact jump then bypasses only the
    # fresh-mount portion to the native $01:ED70 return. Unlike the former
    # temporary player-Y-speed guard, no player state is visible to later
    # normal-sprite slots.
    {
        "marker": "/*FALCON-YOSHI-MOUNT*/",
        # This code is emitted in the generated $01:EC61 helper called by
        # Spr035_Yoshi, rather than in Spr035_Yoshi itself.
        "func_match": "auto_01EC61",
        "anchor": "cpu_trace_block(cpu, 0x01ED38)",
        "snippet": (
            " /*FALCON-YOSHI-MOUNT*/ {"
            " extern int SmwFalconSkipYoshiMount(CpuState *cpu);"
            " if (SmwFalconSkipYoshiMount(cpu)) goto L_ED70_M1X1; }"
        ),
    },

]

# Every marker any injection mode can leave behind (prologues + block patches).
ALL_MARKERS = (MARKER, HOOK_MARKER, "/*FALCON-STOMP-BOUNCE*/", "/*FALCON-YOSHI-MOUNT*/", "/*WS-FLAG*/", "/*WS-DESPAWN*/", "/*WS-SPAWN*/",
               "/*WS-CHAIN*/", "/*WS-SLOT*/", "/*WS-RELOC*/", "/*WS-WING*/",
               "/*WS-COOP-TILE*/", "/*WS-COOP-ROW*/")


def strip_injections(text):
    """Remove every injected ` /*WS-...*/ { ... }` snippet from one file's
    text. Exact inverse of injection: each snippet is a single ` MARKER {`
    block appended to a generated line, with no string literals containing
    braces, so scanning from the marker's opening brace to balance removes
    precisely what was added (including the leading space the injector
    prepends). Returns (text, n_removed)."""
    n = 0
    for mk in ALL_MARKERS:
        while True:
            i = text.find(mk)
            if i < 0:
                break
            start = i - 1 if i > 0 and text[i - 1] == " " else i
            j = text.index("{", i + len(mk))
            depth = 0
            k = j
            while True:
                c = text[k]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            text = text[:start] + text[k + 1:]
            n += 1
    return text, n


# Recognize a generated function definition header to scope block patches.
_FUNC_HDR = re.compile(r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*CpuState")


def block_owner_matches(name, patch):
    owners = patch["func_match"]
    if isinstance(owners, tuple):
        return name in owners
    return owners in name


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
                    and cur_func and block_owner_matches(cur_func, p)):
                line = line.rstrip("\n") + p["snippet"] + "\n"
                n += 1
            out.append(line)
        text = "".join(out)
    return text, n


def count_block_marker_in_function(text, patch):
    """Count one block marker only in its declared generated function."""
    count = 0
    cur_func = None
    for line in text.splitlines():
        mh = _FUNC_HDR.match(line)
        if mh:
            cur_func = mh.group(1)
        if cur_func and block_owner_matches(cur_func, patch):
            count += line.count(patch["marker"])
    return count

# Matches a generated function DEFINITION (opening brace), not a forward
# declaration (which ends in ';'). Captures the base name and the _M?X? suffix.
#   RecompReturn  SomeName_M1X1 ( CpuState *cpu ) {
DEF_RE = re.compile(
    r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*?)(_M[01]X[01])\s*"
    r"\(\s*CpuState\s*\*\s*cpu\s*\)\s*\{",
    re.MULTILINE,
)


def parse_manifest(path):
    """Return full-replacement rules and side-effect hook rules.

    A normal `Base -> Override` entry returns from the generated function.
    `@hook Base -> Hook` injects `Hook(cpu)` at entry then continues into the
    original body, for narrow host seams that must retain native behaviour.
    """
    rules, hooks = [], []
    if not os.path.isfile(path):
        return rules, hooks
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
            if base.startswith("@hook "):
                base = base[len("@hook "):].strip()
                if not base:
                    sys.exit(f"apply_overrides: malformed hook line: {raw!r}")
                hooks.append((base, override, variant))
            else:
                rules.append((base, override, variant))
    return rules, hooks


def prologue(override_symbol):
    return (
        f" {MARKER} {{ extern bool g_ws_active;"
        f" extern RecompReturn {override_symbol}(CpuState *cpu);"
        f" if (g_ws_active) return {override_symbol}(cpu); }}"
    )


def hook_prologue(hook_symbol):
    return (
        f" {HOOK_MARKER} {{ extern void {hook_symbol}(CpuState *cpu);"
        f" {hook_symbol}(cpu); }}"
    )


def apply_to_text(text, rules, hooks):
    """Return (new_text, n_injected). Idempotent."""
    by_base = {}
    for base, override, variant in rules:
        by_base.setdefault(base, []).append((override, variant))
    hook_by_base = {}
    for base, hook, variant in hooks:
        hook_by_base.setdefault(base, []).append((hook, variant))

    injected = 0

    def repl(m):
        nonlocal injected
        whole = m.group(0)
        base, suffix = m.group(1), m.group(2)
        cands = by_base.get(base)
        hook_cands = hook_by_base.get(base)
        if not cands and not hook_cands:
            return whole
        # Pick a rule whose variant matches this definition (or is unscoped).
        chosen = None
        for override, variant in cands or ():
            if variant is None or variant == suffix[1:]:  # suffix like '_M1X1'
                chosen = override
                break
        hook = None
        for candidate, variant in hook_cands or ():
            if variant is None or variant == suffix[1:]:
                hook = candidate
                break
        if chosen is None and hook is None:
            return whole
        # The prologue is appended immediately after the matched opening brace,
        # so it is not part of `whole`. Check the original text at the match
        # boundary; testing `MARKER in whole` made repeated CMake invocations
        # stack duplicate fireball prologues despite this tool's idempotence
        # contract.
        after = text[m.end():m.end() + len(MARKER) + len(HOOK_MARKER) + 4]
        injected = whole
        if chosen is not None and not after.lstrip().startswith(MARKER):
            injected += prologue(chosen)
        if hook is not None and HOOK_MARKER not in after:
            injected += hook_prologue(hook)
        return injected

    new_text = DEF_RE.sub(repl, text)
    # Count injections by counting freshly added markers vs pre-existing.
    return new_text, ((new_text.count(MARKER) - text.count(MARKER)) +
                      (new_text.count(HOOK_MARKER) - text.count(HOOK_MARKER)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen-dir", default="src/gen")
    ap.add_argument(
        "--manifest", default="overrides/falcon/overrides.manifest"
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="verify every manifest base matched at least one definition",
    )
    ap.add_argument(
        "--restore",
        action="store_true",
        help="remove every injected snippet, restoring pristine gen output",
    )
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.restore:
        if not os.path.isdir(args.gen_dir):
            sys.exit(f"apply_overrides: gen dir not found: {args.gen_dir}")
        total = 0
        for name in sorted(os.listdir(args.gen_dir)):
            if not name.endswith(".c"):
                continue
            path = os.path.join(args.gen_dir, name)
            with open(path, "r", encoding="utf-8", newline="") as f:
                text = f.read()
            new_text, n = strip_injections(text)
            if n:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(new_text)
                total += n
                if args.verbose:
                    print(f"apply_overrides: {name}: removed {n} injection(s)")
        print(f"apply_overrides: restored pristine gen ({total} injection(s) removed)")
        return 0

    rules, hooks = parse_manifest(args.manifest)
    if not rules and not hooks and not BLOCK_PATCHES:
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
        # newline="" both ways: never translate the generator's LF line
        # endings (text-mode writes used to silently CRLF-convert every
        # patched bank, breaking byte-exact --restore round-trips).
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        # Track which bases exist in this file before substitution.
        for m in DEF_RE.finditer(text):
            if m.group(1) in {b for b, _, _ in rules} | {b for b, _, _ in hooks}:
                matched_bases.add(m.group(1))
        new_text, n = apply_to_text(text, rules, hooks)
        new_text, nb = apply_block_patches(new_text)
        if n or nb:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new_text)
            total += n + nb
            if args.verbose:
                print(f"apply_overrides: {name}: injected {n} prologue(s), {nb} block patch(es)")

    if args.check:
        missing = ({b for b, _, _ in rules} | {b for b, _, _ in hooks}) - matched_bases
        if missing:
            sys.exit(
                "apply_overrides: manifest bases never matched a definition: "
                + ", ".join(sorted(missing))
            )
        # Block patches do not have a manifest symbol to validate.  Assert
        # their unique marker instead, so an inlined native seam cannot
        # silently regress into a no-op hook on a dead standalone entry.
        checked_patches = [p for p in BLOCK_PATCHES
                           if p.get("check_exactly_once")]
        marker_counts = {p["marker"]: 0 for p in checked_patches}
        scoped_marker_counts = {p["marker"]: 0 for p in checked_patches}
        for name in os.listdir(args.gen_dir):
            if not name.endswith(".c"):
                continue
            with open(os.path.join(args.gen_dir, name), "r", encoding="utf-8",
                      newline="") as f:
                generated = f.read()
            for marker in marker_counts:
                marker_counts[marker] += generated.count(marker)
            for p in checked_patches:
                scoped_marker_counts[p["marker"]] += \
                    count_block_marker_in_function(generated, p)
        expected = {p["marker"]: p.get("expected_matches", 1)
                    for p in checked_patches}
        bad_patches = [marker for marker, count in marker_counts.items()
                       if count != expected[marker]
                       or scoped_marker_counts[marker] != expected[marker]]
        if bad_patches:
            details = ", ".join(
                f"{marker}={marker_counts[marker]}"
                f" (scoped={scoped_marker_counts[marker]})"
                for marker in bad_patches
            )
            sys.exit(
                "apply_overrides: block patches did not match exactly once: "
                + details
            )

    print(f"apply_overrides: injected {total} dispatch prologue(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
