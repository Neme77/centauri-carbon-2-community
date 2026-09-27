#!/usr/bin/env python3
"""One-time migration: pull the inline `const italian={...}` dictionary out
of web/index.html into external, Weblate-editable JSON locale files.

Usage:
    python3 tools/extract_locales.py [index.html]

Writes web/locales/en.json (identity map, English is the source language)
and web/locales/it.json (English -> Italian), preserving the exact runtime
behaviour of the old inline dictionary: when a key repeats, the last value
in source order wins (this matches how a JS object literal parses).

This script is a one-off migration aid, kept for reference and for any
future rebase against an upstream copy that still carries the inline
dictionary. It is not part of the build.
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_INDEX = HERE.parent / "web" / "index.html"
LOCALES_DIR = HERE.parent / "web" / "locales"

DICT_RE = re.compile(r"const italian=\{(.*?)\};", re.S)
# Matches JS single-quoted string literals, honouring backslash escapes,
# without assuming anything about the characters inside (English/Italian
# prose may contain colons, braces, etc.).
TOKEN_RE = re.compile(r"'((?:[^'\\]|\\.)*)'")


def unescape(token):
    # The source only ever uses \' and \\ (curly quotes are literal Unicode
    # apostrophes, not escaped). Keep this narrow rather than reaching for
    # a JS-string-literal decoder for two escape sequences.
    return token.replace("\\'", "'").replace("\\\\", "\\")


def extract_pairs(html):
    match = DICT_RE.search(html)
    if not match:
        raise SystemExit("const italian={...} block not found")
    tokens = [unescape(t) for t in TOKEN_RE.findall(match.group(1))]
    if len(tokens) % 2:
        raise SystemExit(f"odd token count ({len(tokens)}): unbalanced key/value pairs")
    return list(zip(tokens[0::2], tokens[1::2]))


def main():
    index_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_INDEX
    html = index_path.read_text(encoding="utf-8")
    pairs = extract_pairs(html)

    # Last-value-wins on duplicate keys, matching JS object-literal semantics.
    english = {}
    italian = {}
    for key, value in pairs:
        english[key] = key
        italian[key] = value

    LOCALES_DIR.mkdir(parents=True, exist_ok=True)
    for name, data in (("en.json", english), ("it.json", italian)):
        out = LOCALES_DIR / name
        out.write_text(
            json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"wrote {out} ({len(data)} keys)")

    dupes = [k for k in {k for k, _ in pairs} if [p for p in pairs if p[0] == k].__len__() > 1]
    if dupes:
        print(f"note: {len(dupes)} duplicate source keys collapsed to their last value: {sorted(dupes)}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
