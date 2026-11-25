#!/usr/bin/env python3
"""Import Super Mario World structure from the byte-exact SMWDisX disassembly.

IsoFrieze's SMWDisX assembles, under the pinned asar, to a ROM that is
byte-identical to the clean U 1.0 dump this project targets. That makes it an
*authority*, not a hint: for every byte in the ROM it says which source
statement produced it, and therefore whether that byte is 65816 code, SPC-700
code, or data.

Both halves of that claim are pinned as submodules, so it is reproducible from
a clean checkout with nothing else installed:

    third_party/SMWDisX   github.com/IsoFrieze/SMWDisX
    third_party/asar      github.com/RPGHacker/asar (v1.91)

Three facts are imported, all of them from asar's own verified output rather
than from the human `;$xxxxxx` comments (which can drift):

  labels        `[labels]` in the WLA symbol file: bank:addr -> name.
  code vs data  `[addr-to-line mapping]` resolves every emitted address to the
                exact source line that produced it; the statement at that line
                classifies the address.
  architecture  `arch 65816 / spc700` is assembler state that flows across
                `incsrc` in smw.asm's order. Reconstructing that timeline is
                what separates "bank $0E is code" from "bank $0E is code the
                65816 decoder must never walk into".

What is emitted:

  data_region   every span that is NOT 65816 code — data statements, and the
                SPC-700 sound driver inside bank $0E. The decoder stops there.
  symbol        every label, at every address. Naming only: `symbol` feeds the
                emitter's name resolver without promoting anything.
  name          the subset of labels that are hand-named parent labels standing
                at 65816 code. These DO promote to AOT entries, so they are
                opt-in here (--promote-names): unlike Yoshi's Island, this
                project's cfgs already carry 1,800+ hand-tuned `func` entries
                with explicit bounds, and promotion interacts with those.

Macro expansion is the one place where SMWDisX differs structurally from a
flat disassembly, and it is handled explicitly rather than guessed at.
asar's line attribution is exact for top-level statements — a probe over all
108,036 mapped addresses found zero addresses credited to a non-emitting
top-level statement — but inside a macro body it can be off by one (line 70,
`elseif`, is credited with insert_empty's 27,676 fill bytes, whose `rep` is on
line 71). That ambiguity is neutralised rather than corrected: every macro
that receives an address has a uniformly code-or-data body, so an address
landing anywhere inside a macro is classified by THE MACRO, never by the line.
Each macro's kind is derived from its own body — data directives, or a
mnemonic-position `<param>` placeholder resolved against the arguments every
call site passes — and a macro whose body mixes the two is a hard failure,
not a default.

Nothing ROM-derived is written. The output is names, addresses and structural
metadata; no instruction bytes and no disassembly text are copied.

Usage:
    python tools/ingest_smwdisx.py --report                  # counts only
    python tools/ingest_smwdisx.py                           # write cfgs
    python tools/ingest_smwdisx.py --promote-names           # + AOT roots
    python tools/ingest_smwdisx.py --sym _smw_build/smw.sym  # skip assembly
"""
from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "snesrecomp"))

from recompiler import snes65816  # noqa: E402

BEGIN_MARK = "# >>> BEGIN disassembly overlay (generated — do not edit)"
END_MARK = "# <<< END disassembly overlay"

# SMWDisX assembles five console/arcade revisions from one source. This
# project targets the U dump, which is `!_VER = !__VER_U` (constants.asm).
VERSION_U = 1

# ── asar symbol-file grammar ────────────────────────────────────────────────
SECTION_RE = re.compile(r"^\[(?P<name>[^\]]+)\]\s*$")
LABEL_RE = re.compile(r"^([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)\s*$")
SOURCE_FILE_RE = re.compile(r"^([0-9A-Fa-f]{4})\s+[0-9A-Fa-f]{8}\s+(.+?)\s*$")
ADDR_LINE_RE = re.compile(
    r"^([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+"
    r"([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s*$")

# ── source grammar ──────────────────────────────────────────────────────────
PARENT_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
# `?name:` is a macro-local label; `.name` / `..name` are sublabels.
MACRO_LOCAL_LABEL_RE = re.compile(r"^\?([A-Za-z_][A-Za-z0-9_]*)\s*:")
SUBLABEL_RE = re.compile(r"^(\.{1,2})([A-Za-z_][A-Za-z0-9_]*)")
# asar anonymous labels: `+`, `++`, `-`, `--` … standing on the same line as
# the statement they label. Missing these reads `+ LDA.B #$03` as data.
ANON_LABEL_RE = re.compile(r"^(\++|-+)(?=\s|$)")
ARCH_RE = re.compile(r"^arch\s+(\S+)", re.IGNORECASE)
INCSRC_RE = re.compile(r"^\s*incsrc\s+\"?([^\"\s]+)\"?", re.IGNORECASE)
MACRO_START_RE = re.compile(
    r"^\s*macro\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((?P<params>[^)]*)\)",
    re.IGNORECASE)
MACRO_END_RE = re.compile(r"^\s*endmacro\b", re.IGNORECASE)
MACRO_CALL_RE = re.compile(r"%([A-Za-z_][A-Za-z0-9_]*)\s*\(")
# `rep <n> : <statement>` emits <n> copies of the statement; classify the
# statement, not the `rep`.
REP_PREFIX_RE = re.compile(r"^rep\b[^:]*:\s*(.+)$", re.IGNORECASE)
PLACEHOLDER_RE = re.compile(r"^<([A-Za-z_][A-Za-z0-9_]*)>")

# Parent labels the disassembler generated rather than a human naming a
# routine. These stand at unattributed branch targets and inside tables, and
# promoting them would shatter real functions into hundreds of fragments.
GENERATED_LABEL_RE = re.compile(
    r"^(CODE|DATA|EDATA|ADDR|PTR|SUB|UNK|Return)_[0-9A-Fa-f]{4,6}$")

DATA_DIRECTIVE_RE = re.compile(
    r"^(db|dw|dl|dd|byte|word|long|dword|incbin|incsrc_bin|fill|fillbyte|"
    r"skip|pad|padbyte)\b", re.IGNORECASE)
# Statements that emit nothing and therefore cannot classify an address.
NEUTRAL_RE = re.compile(
    r"^(org|base|arch|lorom|hirom|exlorom|exhirom|sa1rom|fullsa1rom|asar|"
    r"namespace|pushpc|pullpc|pushbase|pullbase|align|warnpc|assert|print|"
    r"if|elseif|else|endif|while|endwhile|for|endfor|macro|endmacro|struct|"
    r"endstruct|function|freespace|freecode|freedata|includefrom|incsrc|"
    r"check|optimize|bankcross|math|spcblock|endspcblock|undef|global|"
    r"pushtable|pulltable|table|cleartable|error|warn|reset)\b",
    re.IGNORECASE)
# `!define = value`, `!define #= value`.
DEFINE_RE = re.compile(r"^!\S+\s*[#:]?=")

CPU_MNEMONICS = frozenset("""
ADC AND ASL BCC BCS BEQ BIT BMI BNE BPL BRA BRK BRL BVC BVS CLC CLD CLI CLV
CMP COP CPX CPY DEC DEX DEY EOR INC INX INY JMP JML JSR JSL LDA LDX LDY LSR
MVN MVP NOP ORA PEA PEI PER PHA PHB PHD PHK PHP PHX PHY PLA PLB PLD PLP PLX
PLY REP ROL ROR RTI RTL RTS SBC SEC SED SEI SEP STA STP STX STY STZ TAX TAY
TCD TCS TDC TRB TSB TSC TSX TXA TXS TXY TYA TYX WAI WDM XBA XCE
""".split())

SPC_MNEMONICS = frozenset("""
MOV MOVW ADC SBC CMP AND OR EOR INC DEC ASL LSR ROL ROR XCN MUL DIV DAA DAS
BRA BEQ BNE BCS BCC BVS BVC BMI BPL BBS BBC CBNE DBNZ JMP CALL PCALL TCALL
BRK RET RETI PUSH POP SET1 CLR1 TSET1 TCLR1 AND1 OR1 EOR1 NOT1 MOV1 CLRC
SETC NOTC CLRV CLRP SETP EI DI NOP SLEEP STOP ADDW SUBW CMPW INCW DECW
""".split())

# asar spells the SPC-700's bit-indexed instructions with the index baked
# into the mnemonic (SET4, CLR7, BBS7, BBC0, TCALL11), so the plain table
# above does not cover them.
SPC_MNEMONICS = SPC_MNEMONICS | frozenset(
    [f"{stem}{bit}" for stem in ("SET", "CLR", "BBS", "BBC")
     for bit in range(8)]
    + [f"TCALL{slot}" for slot in range(16)])

ARCH_MNEMONICS = {"65816": CPU_MNEMONICS, "spc700": SPC_MNEMONICS}


@dataclass(frozen=True)
class Label:
    pc24: int
    name: str
    parent: bool          # a column-0 `name:` label in the source
    generated: bool       # CODE_xxxxxx and friends


@dataclass(frozen=True)
class MacroDef:
    name: str
    file_id: str
    first: int            # 1-based line of the `macro` statement
    last: int             # 1-based line of the matching `endmacro`
    params: tuple


def strip_comment(line: str) -> str:
    """Drop an asar line comment. SMWDisX has no semicolon inside a string
    literal in an emitting statement, and asar has no escape, so a plain
    split is exact here."""
    return line.split(";", 1)[0].rstrip()


def strip_label(text: str) -> str:
    """Remove a leading label so `Label: LDA $00` classifies as code.

    SMWDisX uses four label shapes on the same line as a statement: named
    (`Label:`), macro-local (`?start:`), sublabel (`.sub` / `..sub`) and
    asar's anonymous `+` / `-` (with `++`, `--` … for nesting depth).
    """
    text = text.strip()
    match = ANON_LABEL_RE.match(text)
    if match:
        text = text[match.end():].strip()
    for pattern in (MACRO_LOCAL_LABEL_RE, PARENT_LABEL_RE):
        match = pattern.match(text)
        if match:
            return text[match.end():].strip()
    match = SUBLABEL_RE.match(text)
    if match:
        rest = text[match.end():].lstrip()
        return rest[1:].strip() if rest.startswith(":") else rest
    return text


def head_word(text: str) -> str:
    return re.split(r"[\s.:]", text, 1)[0].upper()


# ── source walking ──────────────────────────────────────────────────────────
def walk_assembly_order(entry: Path, visit) -> None:
    """Walk sources in the exact order asar evaluates them.

    `incsrc` splices a file in at its own position, so the sequence is a
    depth-first walk of smw.asm, not a flat list of its includes. Getting this
    wrong is not cosmetic: `arch` is global assembler state that survives the
    return from an include.

    `visit(path, line_number, stripped_source)` is called for every line.
    """
    stack: list[Path] = []

    def walk(path: Path) -> None:
        path = path.resolve()
        if path in stack:
            raise SystemExit(f"incsrc cycle through {path.name}")
        stack.append(path)
        for number, raw in enumerate(
                path.read_text(encoding="utf-8",
                               errors="replace").splitlines(), 1):
            source = strip_comment(raw)
            visit(path, number, source)
            match = INCSRC_RE.match(source)
            if match:
                child = (path.parent / match.group(1)).resolve()
                if child.exists():
                    walk(child)
        stack.pop()

    walk(entry)


def parse_symbols(sym_path: Path):
    """Split an asar WLA symbol file into its three useful sections."""
    labels: list[tuple[int, str]] = []
    files: dict[str, str] = {}
    addr_line: dict[int, tuple[str, int]] = {}
    section = None
    for raw in sym_path.read_text(encoding="utf-8",
                                  errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        match = SECTION_RE.match(line)
        if match:
            section = match.group("name").lower()
            continue
        if section == "labels":
            match = LABEL_RE.match(line)
            if match:
                pc24 = (int(match.group(1), 16) << 16) | int(match.group(2), 16)
                labels.append((pc24, match.group(3)))
        elif section == "source files":
            match = SOURCE_FILE_RE.match(line)
            if match:
                files[match.group(1).upper()] = match.group(2)
        elif section == "addr-to-line mapping":
            match = ADDR_LINE_RE.match(line)
            if match:
                pc24 = (int(match.group(1), 16) << 16) | int(match.group(2), 16)
                addr_line[pc24] = (match.group(3).upper(),
                                   int(match.group(4), 16))
    return labels, files, addr_line


def resolve_source_files(disasm_dir: Path, files: dict[str, str]):
    """asar spells source paths in the case it was handed them; resolve each
    back to this checkout and read it once."""
    by_name: dict[str, Path] = {}
    for path in disasm_dir.rglob("*.asm"):
        by_name.setdefault(path.name.lower(), path.resolve())
    id_to_path: dict[str, Path] = {}
    for file_id, spelled in files.items():
        name = spelled.replace("\\", "/").rsplit("/", 1)[-1].lower()
        if name in by_name:
            id_to_path[file_id] = by_name[name]
    lines_by_id = {
        file_id: path.read_text(encoding="utf-8", errors="replace").splitlines()
        for file_id, path in id_to_path.items()}
    return id_to_path, lines_by_id


# ── macros ──────────────────────────────────────────────────────────────────
def find_macros(lines_by_id: dict[str, list[str]]):
    """Locate every `macro NAME(params) ... endmacro` block.

    Returns (macros, owner) where owner maps (file_id, line) -> macro name for
    every line of every body, including the `macro` and `endmacro` lines
    themselves. An address credited to any of those lines belongs to the macro.
    """
    macros: dict[str, MacroDef] = {}
    owner: dict[tuple[str, int], str] = {}
    for file_id, lines in lines_by_id.items():
        current = None
        start = 0
        params: tuple = ()
        for number, raw in enumerate(lines, 1):
            text = strip_comment(raw)
            match = MACRO_START_RE.match(text)
            if match:
                if current is not None:
                    raise SystemExit(
                        f"nested `macro {match.group(1)}` inside "
                        f"`macro {current}` — unsupported")
                current = match.group(1)
                start = number
                params = tuple(
                    p.strip() for p in match.group("params").split(",")
                    if p.strip())
                owner[(file_id, number)] = current
                continue
            if MACRO_END_RE.match(text):
                if current is not None:
                    owner[(file_id, number)] = current
                    macros[current] = MacroDef(current, file_id, start,
                                               number, params)
                    current = None
                continue
            if current is not None:
                owner[(file_id, number)] = current
        if current is not None:
            raise SystemExit(f"`macro {current}` has no endmacro")
    return macros, owner


def collect_macro_call_args(lines_by_id: dict[str, list[str]],
                            macros: dict[str, MacroDef]):
    """For every macro, the set of argument spellings seen at each parameter
    position across every call site in the disassembly."""
    args_by_macro: dict[str, dict[int, set]] = defaultdict(
        lambda: defaultdict(set))
    for lines in lines_by_id.values():
        for raw in lines:
            text = strip_comment(raw)
            match = MACRO_CALL_RE.search(text)
            if not match or match.group(1) not in macros:
                continue
            depth = 0
            args: list[str] = []
            current: list[str] = []
            for char in text[match.end():]:
                if char in "([":
                    depth += 1
                elif char == ")" and depth == 0:
                    break
                elif char in ")]":
                    depth -= 1
                elif char == "," and depth == 0:
                    args.append("".join(current))
                    current = []
                    continue
                current.append(char)
            args.append("".join(current))
            for index, value in enumerate(args):
                args_by_macro[match.group(1)][index].add(value.strip())
    return args_by_macro


def macro_kind(name: str, macros: dict, lines_by_id: dict[str, list[str]],
               args_by_macro, arch: str, cache: dict,
               resolving: tuple = ()) -> str:
    """'code' | 'data' for a macro whose body emits anything at all.

    Derived from the body, never assumed: a data directive is data, a literal
    mnemonic is code, a nested `%Other(...)` call is whatever that macro is,
    and a mnemonic-position `<param>` placeholder is code only if every
    argument every call site passes in that position is a mnemonic. A body
    that emits both, or a statement this cannot classify at all, is a hard
    failure — silently defaulting is how a decoder ends up walking a fill
    pattern.
    """
    if name in cache:
        return cache[name]
    if name in resolving:
        raise SystemExit(
            "macro expansion cycle: " + " -> ".join(resolving + (name,)))
    macro = macros[name]
    lines = lines_by_id[macro.file_id]
    kinds: set[str] = set()
    unknown: list[str] = []
    for number in range(macro.first, macro.last + 1):
        text = strip_label(strip_comment(lines[number - 1]))
        match = REP_PREFIX_RE.match(text)
        if match:
            text = strip_label(match.group(1))
        if not text or NEUTRAL_RE.match(text) or DEFINE_RE.match(text):
            continue
        if DATA_DIRECTIVE_RE.match(text):
            kinds.add("data")
            continue
        nested = MACRO_CALL_RE.match(text)
        if nested and nested.group(1) in macros:
            kinds.add(macro_kind(nested.group(1), macros, lines_by_id,
                                 args_by_macro, arch, cache,
                                 resolving + (name,)))
            continue
        placeholder = PLACEHOLDER_RE.match(text)
        if placeholder:
            param = placeholder.group(1)
            if param not in macro.params:
                unknown.append(text)
                continue
            index = macro.params.index(param)
            seen = args_by_macro.get(name, {}).get(index, set())
            if seen and all(head_word(value) in CPU_MNEMONICS
                            for value in seen):
                kinds.add("code")
            else:
                unknown.append(
                    f"{text}   (<{param}> resolves to {sorted(seen)})")
            continue
        if head_word(text) in ARCH_MNEMONICS.get(arch, frozenset()):
            kinds.add("code")
            continue
        unknown.append(text)
    if unknown:
        raise SystemExit(
            f"macro `{name}` has statements this importer cannot "
            "classify; refusing to guess:\n  " + "\n  ".join(unknown))
    if kinds == {"code"}:
        kind = "code"
    elif kinds == {"data"} or not kinds:
        kind = "data"
    else:
        raise SystemExit(
            f"macro `{name}` emits both code and data; an address landing in "
            "its body cannot be classified by the macro alone")
    cache[name] = kind
    return kind


# ── classification ──────────────────────────────────────────────────────────
def statement_kind(source: str, arch: str, macros: dict,
                   macro_kinds: dict) -> str:
    """'code' | 'data' | 'neutral' | 'unknown' for one comment-stripped line.

    There is deliberately no "assume data" fallback. Defaulting is how
    `+ LDA.B #$03` — an asar anonymous label sharing a line with its
    instruction — gets silently written into the cfg as a data_region that
    hides a real instruction from the decoder. An unrecognised statement is
    reported as `unknown` and aborts the import instead.
    """
    text = strip_label(source)
    match = REP_PREFIX_RE.match(text)
    if match:
        text = strip_label(match.group(1))
    if not text:
        return "neutral"
    if DATA_DIRECTIVE_RE.match(text):
        return "data"
    if NEUTRAL_RE.match(text) or DEFINE_RE.match(text):
        return "neutral"
    call = MACRO_CALL_RE.match(text)
    if call and call.group(1) in macros:
        return macro_kinds.get(call.group(1), "unknown")
    if head_word(text) in ARCH_MNEMONICS.get(arch, frozenset()):
        return "code"
    return "unknown"


def build_maps(disasm_dir: Path, files, addr_line, lines_by_id, id_to_path):
    """Classify every emitted address.

    Returns (kind_by_pc24, arch_by_pc24, parent_names, macros, macro_owner,
    macro_kinds).
    """
    path_to_id = {path: file_id for file_id, path in id_to_path.items()}
    arch_at: dict[tuple[str, int], str] = {}
    parent_names: set[str] = set()
    state = {"arch": "65816"}

    def visit(path: Path, number: int, source: str) -> None:
        stripped = source.strip()
        match = ARCH_RE.match(stripped)
        if match:
            state["arch"] = match.group(1).lower()
        label = PARENT_LABEL_RE.match(stripped)
        if label:
            parent_names.add(label.group(1))
        file_id = path_to_id.get(path)
        if file_id is not None:
            arch_at[(file_id, number)] = state["arch"]

    walk_assembly_order(disasm_dir / "smw.asm", visit)

    macros, macro_owner = find_macros(lines_by_id)
    args_by_macro = collect_macro_call_args(lines_by_id, macros)
    # Only macros that actually receive an emitted address need classifying;
    # a macro this build never expanded cannot misclassify anything. Nested
    # calls pull their callees in transitively.
    used = {macro_owner[key] for key in
            (addr_line[pc24] for pc24 in addr_line) if key in macro_owner}
    # A macro is expanded at its call site, so its body carries the arch in
    # force there. Every call site in this disassembly is 65816.
    cache: dict[str, str] = {}
    macro_kinds = {name: macro_kind(name, macros, lines_by_id, args_by_macro,
                                    "65816", cache)
                   for name in sorted(used)}

    kind_by_pc24: dict[int, str] = {}
    arch_by_pc24: dict[int, str] = {}
    missing_arch: set[str] = set()
    unknown: dict[tuple[str, int], str] = {}
    for pc24, (file_id, number) in addr_line.items():
        lines = lines_by_id.get(file_id)
        if lines is None or not (1 <= number <= len(lines)):
            continue
        arch_here = arch_at.get((file_id, number))
        if arch_here is None:
            missing_arch.add(file_id)
            continue
        macro = macro_owner.get((file_id, number))
        if macro is not None:
            # Inside a macro body asar's line attribution can be off by one,
            # so classify by the macro rather than by the line. Every macro is
            # proved uniform above, which is what makes that sound.
            kind_by_pc24[pc24] = macro_kinds[macro]
            arch_by_pc24[pc24] = "65816"
            continue
        kind = statement_kind(strip_comment(lines[number - 1]), arch_here,
                              macros, macro_kinds)
        if kind == "unknown":
            unknown[(file_id, number)] = lines[number - 1].strip()
            continue
        kind_by_pc24[pc24] = kind
        arch_by_pc24[pc24] = arch_here
    if missing_arch:
        raise SystemExit(
            "no arch state for source file id(s) "
            + ", ".join(sorted(f"{i} ({files.get(i, '?')})"
                               for i in missing_arch))
            + " — the include order in smw.asm did not cover them")
    if unknown:
        listing = "\n  ".join(
            f"{files.get(file_id, file_id)}:{number}: {text[:90]}"
            for (file_id, number), text in sorted(unknown.items())[:25])
        raise SystemExit(
            f"{len(unknown)} source statements emit bytes this importer "
            "cannot classify as code or data; refusing to guess:\n  "
            + listing)
    return (kind_by_pc24, arch_by_pc24, parent_names, macros, macro_owner,
            macro_kinds)


def cross_check_against_rom(rom: bytes, addr_line, lines_by_id, macro_owner,
                            kind_by_pc24, arch_by_pc24):
    """Prove the line attribution against the ROM itself.

    Every address classified as 65816 code whose source line names a literal
    mnemonic must sit on a ROM opcode for that mnemonic. A mismatch means the
    addr-to-line map and the bytes disagree, and nothing downstream of that is
    trustworthy.
    """
    opcodes = snes65816.opcode_table()
    checked = 0
    mismatches: list[str] = []
    for pc24, (file_id, number) in addr_line.items():
        if kind_by_pc24.get(pc24) != "code":
            continue
        if arch_by_pc24.get(pc24) != "65816":
            continue
        if (file_id, number) in macro_owner:
            continue  # placeholder mnemonic; nothing literal to compare
        lines = lines_by_id.get(file_id)
        if lines is None or not (1 <= number <= len(lines)):
            continue
        want = head_word(strip_label(strip_comment(lines[number - 1])))
        if want not in CPU_MNEMONICS:
            continue
        bank, pc16 = (pc24 >> 16) & 0xFF, pc24 & 0xFFFF
        if pc16 < 0x8000:
            continue
        offset = snes65816.lorom_offset(bank, pc16)
        if offset >= len(rom):
            continue
        entry = opcodes.get(rom[offset])
        got = str(entry[0]).upper() if entry else "??"
        checked += 1
        # asar spells some opcodes by their short form; the decoder names the
        # long one. Fold the pairs that are the same byte-level instruction.
        equivalent = {("JMP", "JML"), ("JML", "JMP"), ("JSR", "JSL"),
                      ("JSL", "JSR"), ("BRA", "BRL"), ("BRL", "BRA"),
                      ("RTS", "RTL"), ("RTL", "RTS")}
        if got != want and (want, got) not in equivalent:
            mismatches.append(f"${pc24:06X} source says {want}, ROM has {got}")
    return checked, mismatches


IDENTIFIER_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# asar built-ins that can appear in a data operand and are not labels.
ASAR_FUNCTIONS = frozenset("""
snestopc pctosnes readfile1 readfile2 readfile3 readfile4 filesize canread
stringsequal stringequal equal notequal less greater lessequal greaterequal
and or nand nor xor not min max clamp safediv select sqrt sin cos tan log
log2 log10 bank realbase con defined sizeof objectsize bitrev
""".split())


def pointer_table_targets(addr_line, lines_by_id, macro_owner, kind_by_pc24,
                          arch_by_pc24, label_addr):
    """Every 65816 entry point named by a pointer table, from the authority.

    This is the fact a `data_region` otherwise destroys. The framework's
    dispatch recovery finds a jump table by scanning ROM from the dispatching
    instruction, and it abandons the scan when the table base falls inside a
    `data_region` (decoder.py `_autorecover_*`). That gate exists because,
    with no authority, walking arbitrary data invents targets. The moment the
    data_regions become ACCURATE, the gate starts firing on exactly the
    tables it most wants to read: SMW's sprite-main `dw` tables are data, so
    marking them data hid all of their targets.

    The disassembly does not have to guess. `dw BanzaiBnCGrayPlat` inside a
    data statement names its target outright, and the symbol file says what
    address that label stands at. A target whose address is 65816 code is an
    entry point, and it is promoted on that evidence — not on the shape of
    its name.
    """
    statements: dict[tuple[str, int], None] = {}
    for pc24, key in addr_line.items():
        if kind_by_pc24.get(pc24) != "data":
            continue
        if key in macro_owner:
            continue  # the operand is a macro parameter, not a label
        statements[key] = None

    targets: dict[int, str] = {}
    for file_id, number in statements:
        lines = lines_by_id.get(file_id)
        if lines is None or not (1 <= number <= len(lines)):
            continue
        text = strip_label(strip_comment(lines[number - 1]))
        match = DATA_DIRECTIVE_RE.match(text)
        if not match:
            continue
        for word in IDENTIFIER_RE.findall(text[match.end():]):
            if word.lower() in ASAR_FUNCTIONS:
                continue
            pc24 = label_addr.get(word)
            if pc24 is None:
                continue
            if (kind_by_pc24.get(pc24) == "code"
                    and arch_by_pc24.get(pc24) == "65816"):
                targets[pc24] = word
    return targets


CFG_BANK_RE = re.compile(r"^bank\s*=\s*(?:0x)?([0-9A-Fa-f]+)", re.IGNORECASE)
CFG_FUNC_RE = re.compile(r"^func\s+(\S+)\s+([0-9A-Fa-f]{1,6})\b")
CFG_NAME_RE = re.compile(r"^name\s+([0-9A-Fa-f]{1,6})\s+(\S+)")


def declared_entry_addresses(output_dir: Path) -> dict:
    """Addresses the project already declares by hand, outside the markers.

    The overlay adds entry points this project does not have; it never
    renames one it does. Two names for one address make the emitter publish
    an alias against a variant set the owning `func` never produced, and the
    link closure rejects the build — correctly, since the hand-written entry
    is the one carrying bounds and width annotations.
    """
    declared: dict[int, str] = {}
    for path in sorted(output_dir.glob("bank??.cfg")):
        bank = int(path.stem[4:], 16)
        inside = False
        for raw in path.read_text(encoding="utf-8").splitlines():
            text = raw.strip()
            if text.startswith(BEGIN_MARK[:30]):
                inside = True
                continue
            if text.startswith(END_MARK[:20]):
                inside = False
                continue
            if inside or not text or text.startswith("#"):
                continue
            match = CFG_BANK_RE.match(text)
            if match:
                bank = int(match.group(1), 16)
                continue
            match = CFG_FUNC_RE.match(text)
            if match:
                declared[(bank << 16) | int(match.group(2), 16)] = \
                    match.group(1)
                continue
            match = CFG_NAME_RE.match(text)
            if match:
                declared[int(match.group(1), 16)] = match.group(2)
    return declared


def data_width_boundaries(addr_line, lines_by_id, macro_owner, kind_by_pc24,
                          arch_by_pc24) -> set:
    """Keep a word/long pointer run distinct from following byte constants.

    Both are data, but merging them gives inline dispatch recovery a false
    table extent. Use the assembler's actual source attribution, not ROM
    values that merely resemble pointers.
    """
    boundaries, previous = set(), {}
    widths = {'dw': 2, 'word': 2, 'dl': 3, 'long': 3,
              'db': 1, 'byte': 1, 'dd': 4, 'dword': 4}
    for pc24, key in sorted(addr_line.items()):
        bank = pc24 >> 16
        if (kind_by_pc24.get(pc24) != 'data' or
                arch_by_pc24.get(pc24) != '65816' or key in macro_owner):
            previous.pop(bank, None)
            continue
        file_id, number = key
        text = strip_label(strip_comment(lines_by_id[file_id][number-1]))
        width = widths.get(head_word(text).lower())
        old = previous.get(bank)
        if old in (2, 3) and width is not None and width != old:
            boundaries.add(pc24)
        previous[bank] = width
    return boundaries


def non_code_regions(kind_by_pc24, arch_by_pc24, boundaries=()) -> list:
    """Merge every span that is not 65816 code into (bank, start, end).

    Each emitted address owns the bytes up to the next emitted address in the
    same bank, which is exactly the statement's assembled width. The last
    statement in a bank runs to $10000 — SMWDisX fills every bank to its end,
    so no gap is invented.
    """
    by_bank: dict[int, list[int]] = defaultdict(list)
    for pc24 in kind_by_pc24:
        by_bank[(pc24 >> 16) & 0xFF].append(pc24 & 0xFFFF)

    spans: list[tuple[int, int, int]] = []
    for bank, addresses in by_bank.items():
        addresses.sort()
        for index, pc16 in enumerate(addresses):
            pc24 = (bank << 16) | pc16
            if (kind_by_pc24[pc24] == "code"
                    and arch_by_pc24.get(pc24) == "65816"):
                continue
            end = addresses[index + 1] if index + 1 < len(addresses) else 0x10000
            if end > pc16:
                spans.append((bank, pc16, end))

    merged: list[tuple[int, int, int]] = []
    for bank, start, end in sorted(spans):
        if (merged and merged[-1][0] == bank and start <= merged[-1][2]
                and ((bank << 16) | start) not in boundaries):
            old_bank, old_start, old_end = merged[-1]
            merged[-1] = (old_bank, old_start, max(old_end, end))
        else:
            merged.append((bank, start, end))
    return merged


def unmapped_bank_regions(kind_by_pc24, rom_size: int) -> list:
    """Banks the disassembly emits nothing for at all. They are data end to
    end. (SMWDisX covers every bank of the U ROM, so this is normally empty;
    it stays as a guard against a future ROM this importer is pointed at.)"""
    banks_with_lines = {(pc24 >> 16) & 0xFF for pc24 in kind_by_pc24}
    last_bank = (rom_size // 0x8000) - 1
    return [(bank, 0x8000, 0x10000)
            for bank in range(0, last_bank + 1)
            if bank not in banks_with_lines]


def classify_labels(labels, parent_names, rom_size: int) -> list:
    """Attach source shape to each ROM symbol, keeping one name per address.

    Labels below $8000 are rammap.asm's RAM names, and banks $7E/$7F are WRAM
    (this game runs a routine out of $7F8000); neither is a ROM address, so
    neither has a place in a bank cfg.
    """
    last_bank = (rom_size // 0x8000) - 1
    chosen: dict[int, Label] = {}
    for pc24, name in labels:
        if name.startswith(":"):
            continue  # asar internals: :macro_N_start, :neg_1_N
        bank, pc16 = (pc24 >> 16) & 0xFF, pc24 & 0xFFFF
        if pc16 < 0x8000 or (bank & 0x7F) > last_bank or bank in (0x7E, 0x7F):
            continue
        candidate = Label(pc24, name, name in parent_names,
                          bool(GENERATED_LABEL_RE.match(name)))
        current = chosen.get(pc24)
        if current is None:
            chosen[pc24] = candidate
            continue

        # Prefer a hand-written parent name over a generated one, then the
        # shorter spelling, so the emitted symbol is the readable alias.
        def rank(item: Label):
            return (not item.parent, item.generated, len(item.name), item.name)

        if rank(candidate) < rank(current):
            chosen[pc24] = candidate
    return [chosen[pc24] for pc24 in sorted(chosen)]


# ── assembly ────────────────────────────────────────────────────────────────
def build_asar(asar_src: Path, build_dir: Path) -> Path:
    """Build the pinned asar if it is not built yet, and return the binary."""
    exe = build_dir / "asar" / "bin" / ("asar.exe" if sys.platform == "win32"
                                       else "asar")
    if exe.exists():
        return exe
    if not (asar_src / "src" / "CMakeLists.txt").exists():
        raise SystemExit(
            f"{asar_src} is empty — run `git submodule update --init "
            "third_party/asar`")
    print(f"building pinned asar into {build_dir} …")
    configure = subprocess.run(
        ["cmake", "-S", str(asar_src / "src"), "-B", str(build_dir),
         "-DCMAKE_BUILD_TYPE=Release", "-DASAR_GEN_EXE=ON",
         "-DASAR_GEN_DLL=OFF", "-DASAR_GEN_EXE_TEST=OFF",
         "-DASAR_GEN_DLL_TEST=OFF"],
        capture_output=True, text=True)
    if configure.returncode != 0:
        sys.stderr.write(configure.stdout + configure.stderr)
        raise SystemExit("cmake could not configure the pinned asar")
    build = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "asar-standalone",
         "--config", "Release"],
        capture_output=True, text=True)
    if build.returncode != 0:
        sys.stderr.write(build.stdout + build.stderr)
        raise SystemExit("cmake could not build the pinned asar")
    if not exe.exists():
        matches = list(build_dir.rglob("asar.exe")) + list(
            build_dir.rglob("asar"))
        matches = [m for m in matches if m.is_file()]
        if not matches:
            raise SystemExit(f"asar built but no binary under {build_dir}")
        exe = matches[0]
    return exe


def assemble(asar: Path, disasm_dir: Path, out_rom: Path, out_sym: Path,
             version: int) -> bytes:
    out_rom.parent.mkdir(parents=True, exist_ok=True)
    out_rom.write_bytes(b"")
    result = subprocess.run(
        [str(asar), "-wno1009", "-wno1018", "--fix-checksum=off",
         f"--define", f"_VER={version}",
         "--symbols=wla", f"--symbols-path={out_sym}",
         "smw.asm", str(out_rom)],
        cwd=str(disasm_dir), capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit("asar failed to assemble SMWDisX")
    built = out_rom.read_bytes()
    # asar writes a 512-byte copier header when it is handed an empty output
    # file. The project ROM is headerless.
    if len(built) % 0x8000 == 0x200:
        built = built[0x200:]
    return built


# ── emission ────────────────────────────────────────────────────────────────
def emit_cfg(output_dir: Path, labels, regions, kind_by_pc24, arch_by_pc24,
             dispatch_targets, promote_names: bool) -> dict:
    """Write the overlay block into recomp/bankNN.cfg.

    Everything this importer produces goes between explicit markers, so the
    hand-written directives this project's cfgs are full of — func bounds,
    exclude_range, hle_*, indirect_dispatch — survive a re-run untouched.
    """
    # Which banks this project compiles is a project decision — a bank cfg is
    # a translation unit — so the overlay fills in the cfgs that exist and
    # never invents one. Structure for a bank with no cfg is not lost: a
    # `data_region` is bank-explicit and aggregated across every cfg, so it
    # rides along in the lowest one.
    banks = sorted(int(path.stem[4:], 16)
                   for path in output_dir.glob("bank??.cfg"))
    if not banks:
        raise SystemExit(f"no bank??.cfg under {output_dir} to overlay onto")

    regions_by_bank: dict[int, list[tuple[int, int]]] = defaultdict(list)
    for bank, start, end in regions:
        regions_by_bank[bank].append((start, end))
    labels_by_bank: dict[int, list[Label]] = defaultdict(list)
    for label in labels:
        labels_by_bank[(label.pc24 >> 16) & 0xFF].append(label)

    def is_65816_code(pc24: int) -> bool:
        return (kind_by_pc24.get(pc24) == "code"
                and arch_by_pc24.get(pc24) == "65816")

    # Regions in banks with no cfg of their own have nowhere to live;
    # data_region is bank-explicit and aggregated across every cfg, so the
    # lowest bank cfg carries them under its own heading.
    host_bank = banks[0]
    orphan_regions = [(bank, start, end) for bank, start, end in regions
                      if bank not in banks]

    dispatch_by_bank: dict[int, list[tuple[int, str]]] = defaultdict(list)
    for pc24, name in sorted(dispatch_targets.items()):
        dispatch_by_bank[(pc24 >> 16) & 0xFF].append((pc24, name))

    stats = {"banks": len(banks), "names": 0, "symbols": 0,
             "dispatch": 0, "regions": len(regions)}
    output_dir.mkdir(parents=True, exist_ok=True)

    for bank in banks:
        path = output_dir / f"bank{bank:02x}.cfg"
        body = [
            BEGIN_MARK,
            "# Generated by tools/ingest_smwdisx.py from the pinned SMWDisX",
            "# disassembly (IsoFrieze), which assembles byte-identical to",
            "# this project's ROM. Do not hand-edit between the markers.",
            "",
            "# Every span that is not 65816 code: data statements, and the",
            "# SPC-700 sound driver, which is not this decoder's ISA.",
        ]
        for start, end in sorted(regions_by_bank.get(bank, ())):
            body.append(f"data_region {bank:02X} {start:04X} {end:04X}")
        if bank == host_bank and orphan_regions:
            body.append("")
            body.append("# Banks with no cfg of their own. data_region is")
            body.append("# bank-explicit and aggregated across every cfg, so")
            body.append("# they live here.")
            for obank, start, end in sorted(orphan_regions):
                body.append(f"data_region {obank:02X} {start:04X} {end:04X}")

        body.append("")
        body.append("# Entry points named by a pointer table. A data_region")
        body.append("# stops the framework's jump-table scan (decoder.py")
        body.append("# _autorecover_*), so the tables above would otherwise")
        body.append("# hide their own targets. The disassembly names them")
        body.append("# outright, so they are declared instead of scanned for.")
        dispatched = 0
        for pc24, name in dispatch_by_bank.get(bank, ()):
            body.append(f"name {pc24:06X} {name}")
            dispatched += 1
        stats["dispatch"] += dispatched

        promoted = 0
        if promote_names:
            body.append("")
            body.append("# Hand-named parent labels standing at 65816 code.")
            body.append("# These promote to AOT entries.")
            for label in labels_by_bank.get(bank, ()):
                if not label.parent or label.generated:
                    continue
                if not is_65816_code(label.pc24):
                    continue
                body.append(f"name {label.pc24:06X} {label.name}")
                promoted += 1
        stats["names"] += promoted

        body.append("")
        body.append("# Every remaining label, naming only. `symbol` feeds the")
        body.append("# emitter's name resolver without promoting anything, so")
        body.append("# a discovered function still comes out carrying its")
        body.append("# name.")
        named = 0
        for label in labels_by_bank.get(bank, ()):
            if label.pc24 in dispatch_targets:
                continue  # already declared above, as a promoting `name`
            if (promote_names and label.parent and not label.generated
                    and is_65816_code(label.pc24)):
                continue
            body.append(f"symbol {label.pc24:06X} {label.name}")
            named += 1
        stats["symbols"] += named
        body.append(END_MARK)

        generated = "\n".join(body) + "\n"
        if path.exists():
            text = path.read_text(encoding="utf-8")
            if BEGIN_MARK in text and END_MARK in text:
                head = text.split(BEGIN_MARK)[0]
                tail = text.split(END_MARK, 1)[1]
                # The generated block already ends in a newline, and the tail
                # opens with the one that followed the end marker. Keeping
                # both grows the file by a blank line on every re-run, which
                # makes the importer non-idempotent and every re-import a
                # diff.
                if tail.startswith("\n"):
                    tail = tail[1:]
                text = head + generated + tail
            else:
                text = text.rstrip("\n") + "\n\n" + generated
        else:
            raise SystemExit(f"{path} vanished mid-write")
        with path.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disasm", type=Path,
                        default=REPO / "third_party" / "SMWDisX")
    parser.add_argument("--asar-src", type=Path,
                        default=REPO / "third_party" / "asar")
    parser.add_argument("--asar", type=Path,
                        help="use this asar binary instead of building the "
                             "pinned one")
    parser.add_argument("--rom", type=Path, default=REPO / "smw.sfc")
    parser.add_argument("--output", type=Path, default=REPO / "recomp")
    parser.add_argument("--build-dir", type=Path, default=REPO / "_smw_build")
    parser.add_argument("--version", type=int, default=VERSION_U,
                        help="SMWDisX !_VER (default 1 = U)")
    parser.add_argument("--sym", type=Path,
                        help="use an existing WLA sym instead of assembling; "
                             "skips the byte-exactness proof")
    parser.add_argument("--promote-names", action="store_true",
                        help="also emit `name` for hand-named parent labels, "
                             "which promotes them to AOT roots")
    parser.add_argument("--report", action="store_true",
                        help="classify and print counts, write nothing")
    args = parser.parse_args()

    rom = args.rom.read_bytes()
    snes65816.set_rom_mapping("lorom")

    sym = args.sym
    if sym is None:
        asar = args.asar or build_asar(args.asar_src,
                                       args.build_dir / "asar")
        sym = args.build_dir / "smw.sym"
        built = assemble(asar, args.disasm, args.build_dir / "smw.smc", sym,
                         args.version)
        if built != rom:
            print(f"assembled {hashlib.sha256(built).hexdigest()}")
            print(f"project   {hashlib.sha256(rom).hexdigest()}")
            raise SystemExit(
                "SMWDisX does not assemble to this project's ROM — refusing "
                "to import structure that describes a different image")
        print(f"SMWDisX assembles byte-identical to {args.rom.name} "
              f"({len(built)} bytes)")

    labels, files, addr_line = parse_symbols(sym)
    id_to_path, lines_by_id = resolve_source_files(args.disasm, files)
    (kind_by_pc24, arch_by_pc24, parent_names, macros, macro_owner,
     macro_kinds) = build_maps(args.disasm, files, addr_line, lines_by_id,
                               id_to_path)

    checked, mismatches = cross_check_against_rom(
        rom, addr_line, lines_by_id, macro_owner, kind_by_pc24, arch_by_pc24)
    if mismatches:
        for line in mismatches[:20]:
            print(f"  {line}")
        raise SystemExit(
            f"{len(mismatches)} of {checked} literal-mnemonic code addresses "
            "disagree with the ROM byte at that address — the addr-to-line "
            "map and the image do not describe the same program")

    boundaries = data_width_boundaries(addr_line, lines_by_id, macro_owner,
                                       kind_by_pc24, arch_by_pc24)
    regions = non_code_regions(kind_by_pc24, arch_by_pc24, boundaries)
    regions += unmapped_bank_regions(kind_by_pc24, len(rom))
    regions.sort()
    classified = classify_labels(labels, parent_names, len(rom))

    label_addr = {name: pc24 for pc24, name in labels
                  if not name.startswith(":")}
    dispatch_targets = pointer_table_targets(
        addr_line, lines_by_id, macro_owner, kind_by_pc24, arch_by_pc24,
        label_addr)
    on_cfg_bank = {(label.pc24 >> 16) & 0xFF for label in classified}
    declared = declared_entry_addresses(args.output)
    already = sum(1 for pc24 in dispatch_targets if pc24 in declared)
    dispatch_targets = {pc24: name for pc24, name in dispatch_targets.items()
                        if ((pc24 >> 16) & 0xFF) in on_cfg_bank
                        and pc24 not in declared}

    kinds: dict[tuple[str, str], int] = defaultdict(int)
    for pc24, kind in kind_by_pc24.items():
        kinds[(arch_by_pc24.get(pc24, "?"), kind)] += 1
    print(f"symbol labels        {len(labels)} "
          f"({len(classified)} ROM addresses)")
    print(f"mapped addresses     {len(kind_by_pc24)}")
    for key in sorted(kinds):
        print(f"  {key[0]:<8} {key[1]:<8} {kinds[key]}")
    print(f"macros classified    {len(macro_kinds)} "
          f"({', '.join(f'{n}={k}' for n, k in sorted(macro_kinds.items()))})")
    print(f"mnemonic cross-check {checked} literal code addresses, "
          f"0 disagreements with the ROM")
    print(f"non-65816 regions    {len(regions)}")
    print(f"pointer-table entries {len(dispatch_targets)} new 65816 entry "
          f"points named by data statements ({already} already declared by "
          "hand, left alone)")

    # Banks the authority says hold 65816 code but this project does not
    # compile. Reported, never papered over: a bank cfg is a translation unit,
    # and adding one is a project decision, not an import.
    code_banks = {(pc24 >> 16) & 0xFF for pc24, kind in kind_by_pc24.items()
                  if kind == "code" and arch_by_pc24.get(pc24) == "65816"}
    have_cfg = {int(path.stem[4:], 16)
                for path in args.output.glob("bank??.cfg")}
    uncompiled = sorted(code_banks - have_cfg)
    if uncompiled:
        counts = {bank: sum(1 for pc24, kind in kind_by_pc24.items()
                            if kind == "code"
                            and arch_by_pc24.get(pc24) == "65816"
                            and (pc24 >> 16) & 0xFF == bank)
                  for bank in uncompiled}
        print("banks with 65816 code and no cfg in this project: "
              + ", ".join(f"${b:02X} ({counts[b]} insns)"
                          for b in uncompiled))

    if args.report:
        return 0
    stats = emit_cfg(args.output, classified, regions, kind_by_pc24,
                     arch_by_pc24, dispatch_targets, args.promote_names)
    print(f"wrote {stats['banks']} bank cfgs to {args.output}: "
          f"{stats['dispatch']} pointer-table name, {stats['names']} label "
          f"name, {stats['symbols']} symbol, {stats['regions']} data_region")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
