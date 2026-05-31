#!/usr/bin/env python3
"""Lightweight MAC/ASM (8080 CP/M) source checker.

Flags the constraints that bite when assembling with MAC.COM (or ASM.COM):

  * two symbols that collide within their first 6 significant characters
  * a label/symbol that reuses a MAC directive name (MACRO, REPT, ...)
  * IN / OUT instructions whose port is written in hex instead of decimal

Usage: check_mac_asm.py FILE.ASM ...
Exit status is 1 when any issue is found, 0 when clean.
"""

import re
import sys
from pathlib import Path

# Pseudo-ops / directives that introduce or use a name but are not themselves
# user symbols. A label may not reuse one of these under MAC.
DIRECTIVES = {
    "MACRO", "ENDM", "REPT", "IRP", "IRPC", "EXITM", "LOCAL", "MACLIB",
    "EQU", "SET", "DB", "DW", "DS", "ORG", "END", "IF", "ELSE", "ENDIF",
    "TITLE", "PAGE", "INCLUDE",
}

SIGNIF = 6  # significant characters in a CP/M assembler symbol


def strip_comment(line):
    """Remove an assembler ';' comment, ignoring ';' inside quotes."""
    out = []
    inq = None
    for ch in line:
        if inq:
            out.append(ch)
            if ch == inq:
                inq = None
        elif ch in ("'", '"'):
            inq = ch
            out.append(ch)
        elif ch == ";":
            break
        else:
            out.append(ch)
    return "".join(out)


def find_symbols(lines):
    """Return list of (lineno, name) for each defined symbol.

    A symbol is defined when it appears in the label field: either ending in
    ':' or immediately followed by EQU / SET / MACRO on the same line.
    """
    syms = []
    for no, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        if not line.strip():
            continue

        # Labelled definition: NAME: ...  or  NAME EQU/SET/MACRO ...
        m = re.match(r"^\s*([A-Za-z@?][A-Za-z0-9@?$]*)\s*:", line)
        if m:
            syms.append((no, m.group(1)))
            continue

        m = re.match(
            r"^\s*([A-Za-z@?][A-Za-z0-9@?$]*)\s+(EQU|SET|MACRO)\b",
            line, re.IGNORECASE,
        )
        if m:
            syms.append((no, m.group(1)))
    return syms


def check(path):
    text = path.read_text(errors="replace")
    lines = text.splitlines()
    errs = []

    syms = find_symbols(lines)

    # Reserved-word check. (Symbol length itself is not an error: CP/M
    # assemblers accept longer names but only the first 6 characters are
    # significant, so the real hazard is a first-6-char collision, below.)
    for no, name in syms:
        if name.upper() in DIRECTIVES:
            errs.append((no, "symbol reuses a MAC directive name", name))

    # First-6-character collisions across all defined symbols. Two symbols
    # that share their first 6 characters are the same symbol to the
    # assembler, even though they look distinct in the source.
    pref = {}
    for no, name in syms:
        key = name[:SIGNIF].upper()
        pref.setdefault(key, []).append(name)
    for key, vals in sorted(pref.items()):
        uniq = sorted(set(vals))
        if len(uniq) > 1:
            errs.append((0, "first-6-character collision",
                         "%s: %s" % (key, ", ".join(uniq))))

    # IN / OUT ports should be decimal, not hex (NNH or 0xNN).
    for no, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        m = re.search(r"\b(IN|OUT)\s+([0-9A-Fa-fHhxX$]+)\b", line)
        if m:
            port = m.group(2)
            if re.match(r"^[0-9A-Fa-f]+[Hh]$", port) or port.lower().startswith("0x"):
                errs.append((no, "IN/OUT port is hex; use decimal",
                             "%s %s" % (m.group(1), port)))

    return errs


def main(argv):
    if len(argv) < 2:
        print("usage: check_mac_asm.py FILE...", file=sys.stderr)
        return 2

    bad = 0
    for arg in argv[1:]:
        path = Path(arg)
        errs = check(path)
        if errs:
            bad = 1
            print("%s:" % path)
            for no, msg, detail in errs:
                loc = ":%d" % no if no else ""
                print("  %s%s: %s" % (path.name, loc, msg))
                if detail:
                    print("    %s" % detail)
    return bad


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
