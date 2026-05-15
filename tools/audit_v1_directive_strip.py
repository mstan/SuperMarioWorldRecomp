"""Strip remaining v1 ABI-fiction directives from recomp/*.cfg.

snesrecomp v2 cfg_loader.py:8 explicitly ignores `ret_y`, `carry_ret`,
`y_after:`, `x_after:`, `init_y:`, `init_carry:`, `restores_x:`, and
the bank-scope `default_init_y` line. They were v1 calling-convention
hints; v2 carries register state in CpuState and doesn't need them.

Tokens stripped (on `func` and `name` lines only):
  - init_y:<value>
  - init_carry:<value>
  - restores_x:<value>
  - y_after:<value>
  - x_after:<value>
  - carry_ret  (flag)
  - ret_y      (flag)

Whole-line deletes:
  - `default_init_y = <value>` (bank-scope, top-of-file)

Commented-out occurrences (line starts with `#`) are preserved as
historical documentation.

Default mode prints a per-bank report. --apply rewrites in byte mode
(CRLF preserving). Companion to audit_func_sig_strip.py.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass

_VALUE_TOKENS = ('init_y', 'init_carry', 'restores_x', 'y_after', 'x_after')
_FLAG_TOKENS = ('carry_ret', 'ret_y')

# Token strip regexes (byte-mode). Leading whitespace included so the
# token + its preceding gap go in one bite.
_VALUE_RES = {
    name: re.compile(rb"\s+" + name.encode("ascii") + rb":\S+")
    for name in _VALUE_TOKENS
}
_FLAG_RES = {
    name: re.compile(rb"\s+" + name.encode("ascii") + rb"\b")
    for name in _FLAG_TOKENS
}

# Whole-line: `default_init_y = <token>` (no leading `#`).
_DEFAULT_INIT_Y_LINE_RE = re.compile(
    rb"^\s*default_init_y\s*=\s*\S+\s*$"
)

_BANK_RE = re.compile(r"bank([0-9a-fA-F]+)\.cfg$")


@dataclass
class BankCounts:
    tokens_stripped: dict
    lines_deleted: int = 0
    lines_mutated: int = 0


def _process_bank(path: pathlib.Path, apply: bool) -> BankCounts:
    counts = BankCounts(tokens_stripped={
        name: 0 for name in _VALUE_TOKENS + _FLAG_TOKENS
    })
    raw = path.read_bytes()
    out_chunks = []
    for line in raw.splitlines(keepends=True):
        # Preserve commented-out lines untouched.
        stripped_lead = line.lstrip()
        if stripped_lead.startswith(b"#"):
            out_chunks.append(line)
            continue

        # Whole-line `default_init_y = ...` delete.
        if _DEFAULT_INIT_Y_LINE_RE.match(line.rstrip(b"\r\n")):
            counts.lines_deleted += 1
            counts.lines_mutated += 1
            continue

        # Detect line ending so we can strip body cleanly.
        eol = b""
        body = line
        if body.endswith(b"\r\n"):
            eol = b"\r\n"
            body = body[:-2]
        elif body.endswith(b"\n"):
            eol = b"\n"
            body = body[:-1]

        # Only mutate func/name lines.
        if not (stripped_lead.startswith(b"func ")
                or stripped_lead.startswith(b"name ")):
            out_chunks.append(line)
            continue

        # Some tokens may sit inside the trailing `# ...` comment as
        # documentation prose. Split body at first `#` and only mutate
        # the code portion.
        hash_idx = body.find(b"#")
        if hash_idx >= 0:
            code = body[:hash_idx]
            comment_tail = body[hash_idx:]
        else:
            code = body
            comment_tail = b""

        mutated = False
        for name, rx in _VALUE_RES.items():
            code_new, n = rx.subn(b"", code)
            if n:
                counts.tokens_stripped[name] += n
                code = code_new
                mutated = True
        for name, rx in _FLAG_RES.items():
            code_new, n = rx.subn(b"", code)
            if n:
                counts.tokens_stripped[name] += n
                code = code_new
                mutated = True

        if mutated:
            counts.lines_mutated += 1
            code = code.rstrip(b" \t")
            if comment_tail:
                out_line = code + b"  " + comment_tail + eol
            else:
                out_line = code + eol
            out_chunks.append(out_line)
        else:
            out_chunks.append(line)

    if apply and (counts.lines_mutated or counts.lines_deleted):
        path.write_bytes(b"".join(out_chunks))
    return counts


def _format_report(per_bank: dict) -> str:
    lines = ["# audit_v1_directive_strip — report", ""]
    lines.append("v2 cfg_loader ignores all of these directives.")
    lines.append("Tokens stripped from `func`/`name` lines; whole-line")
    lines.append("`default_init_y = ...` deletes counted separately.")
    lines.append("")
    header = "| Bank | Lines mutated | default_init_y delete | "
    for name in _VALUE_TOKENS + _FLAG_TOKENS:
        header += f"{name} | "
    lines.append(header.rstrip())
    sep = "|" + "---|" * (3 + len(_VALUE_TOKENS) + len(_FLAG_TOKENS))
    lines.append(sep)

    total_mut = 0
    total_del = 0
    totals = {name: 0 for name in _VALUE_TOKENS + _FLAG_TOKENS}
    for bank in sorted(per_bank):
        c = per_bank[bank]
        row = f"| {bank} | {c.lines_mutated} | {c.lines_deleted} | "
        for name in _VALUE_TOKENS + _FLAG_TOKENS:
            row += f"{c.tokens_stripped[name]} | "
            totals[name] += c.tokens_stripped[name]
        lines.append(row.rstrip())
        total_mut += c.lines_mutated
        total_del += c.lines_deleted
    row = f"| **TOTAL** | **{total_mut}** | **{total_del}** | "
    for name in _VALUE_TOKENS + _FLAG_TOKENS:
        row += f"**{totals[name]}** | "
    lines.append(row.rstrip())
    return "\n".join(lines) + "\n"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cfg-dir", default="recomp")
    p.add_argument(
        "--out",
        default="tools/audit_v1_directive_strip_report.md",
    )
    p.add_argument("--apply", action="store_true",
                   help="Rewrite cfg files (default = report only)")
    args = p.parse_args()

    cfg_dir = pathlib.Path(args.cfg_dir)
    per_bank: dict = {}
    for cfg_path in sorted(cfg_dir.glob("bank*.cfg")):
        m = _BANK_RE.search(cfg_path.name)
        if not m:
            continue
        per_bank[cfg_path.name] = _process_bank(cfg_path, apply=args.apply)

    report = _format_report(per_bank)
    out_path = pathlib.Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(report, encoding="utf-8")
    print(f"Wrote {out_path}")
    print()
    print(report)
    if not args.apply:
        print("Report-only. Re-run with --apply to rewrite cfg files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
