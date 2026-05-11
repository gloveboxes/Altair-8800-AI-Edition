#!/usr/bin/env python3
"""Lightweight BDS C source checker."""

import re
import sys
from pathlib import Path

KEYS = {
    "int", "char", "unsigned", "return", "if", "else", "for", "while",
    "break", "continue", "switch", "case", "default", "sizeof",
    "struct", "union", "register", "define", "include", "ifdef",
    "ifndef", "endif", "undef",
}

BAD = {
    "void", "typedef", "const", "enum", "signed", "volatile", "static",
    "inline", "extern", "auto",
}

TYPE_WORDS = {"int", "char", "unsigned", "struct", "union", "register"}


def strip_text(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\])*"', " ", text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", " ", text)
    return text


def line_clean(line):
    line = re.sub(r"/\*.*?\*/", " ", line)
    line = re.sub(r'"(?:\\.|[^"\\])*"', " ", line)
    line = re.sub(r"'(?:\\.|[^'\\])*'", " ", line)
    return line


def find_ids(text):
    return re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", text)


def is_decl(line):
    s = line.strip()
    if not s or s.startswith("#"):
        return False
    if not s.endswith(";"):
        return False
    first = s.split()[0]
    return first in TYPE_WORDS


def scan_order(lines):
    errs = []
    infn = False
    seen = False
    pend = False
    sig = False
    depth = 0

    for no, raw in enumerate(lines, 1):
        s = line_clean(raw).strip()
        if not s:
            continue

        if not infn and re.match(r"^[A-Za-z_][A-Za-z0-9_\s\*]*\([^;]*\)\s*$", s):
            sig = True
            continue

        if sig and s == "{":
            infn = True
            sig = False
            seen = False
            depth = 1
            continue

        if not infn:
            continue

        if "{" in s:
            depth += s.count("{")
        if "}" in s:
            depth -= s.count("}")
            if depth <= 0:
                infn = False
                continue

        if depth == 1:
            if is_decl(s):
                if seen:
                    errs.append((no, "declaration after statement", raw.rstrip()))
            elif not s.startswith("{") and not s.startswith("}"):
                seen = True

    return errs


def check(path):
    text = path.read_text(errors="replace")
    code = strip_text(text)
    errs = []

    if "//" in code:
        errs.append((0, "contains // comment or token", ""))

    ids = [i for i in find_ids(code) if i not in KEYS]

    for name in sorted(set(ids)):
        if len(name) > 7:
            errs.append((0, "identifier longer than 7 chars", name))
        if name in BAD:
            errs.append((0, "unsupported keyword", name))

    pref = {}
    for name in ids:
        key = name[:7]
        pref.setdefault(key, set()).add(name)
    for key, vals in sorted(pref.items()):
        vals = sorted(vals)
        if len(vals) > 1:
            errs.append((0, "first-7-character collision", "%s: %s" % (key, ", ".join(vals))))

    for no, raw in enumerate(text.splitlines(), 1):
        s = line_clean(raw)
        if re.search(r"\([ \t]*(int|char|unsigned|struct|union)[ \t\*]+[A-Za-z_\)]", s):
            errs.append((no, "possible cast", raw.rstrip()))

    errs.extend(scan_order(text.splitlines()))
    return errs


def main(argv):
    if len(argv) < 2:
        print("usage: check_bds_c.py FILE...", file=sys.stderr)
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
