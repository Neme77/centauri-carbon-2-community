#!/usr/bin/env python3
"""Every realpath() destination must satisfy the libc buffer contract.

glibc's fortified realpath() accepts either NULL (POSIX.1-2008: libc allocates
the canonical name) or a buffer of at least PATH_MAX bytes, even when the
resulting name is short. cc2-control keeps its own paths in PATH_MAX_LOCAL
(512) arrays, so passing one of those as the destination makes a fortified
-Werror build fail with "second argument of realpath must be either NULL or at
least PATH_MAX bytes long buffer".

The CI hosts enable _FORTIFY_SOURCE by default, a developer machine may not, so
this check is what keeps the mistake from returning unnoticed. The NULL form is
the one the project uses: it cannot overflow, whatever the canonical name length
is, and the printer's libc (glibc 2.23 via the documented GCC 6.5.0 toolchain,
musl or uClibc-ng) implements POSIX.1-2008.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = sorted((ROOT / "src").rglob("*.h")) + sorted((ROOT / "src").rglob("*.c"))


def code_only(text):
    """Drop comments and string literals so prose cannot look like a call."""
    out, index, length = [], 0, len(text)
    while index < length:
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            index = length if end < 0 else end + 2
            out.append(" ")
        elif text.startswith("//", index):
            end = text.find("\n", index + 2)
            index = length if end < 0 else end + 1
            out.append(" ")
        elif text[index] in "\"'":
            quote = text[index]
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            out.append('""')
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def split_arguments(text, start):
    """Split the arguments of the call whose '(' is at `start` (exclusive)."""
    parts, current = [], ""
    depth, quoted, escaped = 0, None, False
    for index in range(start, len(text)):
        char = text[index]
        if quoted:
            current += char
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quoted:
                quoted = None
            continue
        if char in "\"'":
            quoted = char
            current += char
        elif char in "([{":
            depth += 1
            current += char
        elif char in ")]}":
            if depth == 0:
                parts.append(current.strip())
                return parts
            depth -= 1
            current += char
        elif char == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += char
    return None


def body_of(text, name):
    """The brace-balanced body of the function `name`."""
    start = text.index(name)
    start = text.index("{", start)
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    return ""


calls = 0
for source in SOURCES:
    text = code_only(source.read_text(encoding="utf-8"))
    for match in re.finditer(r"\brealpath\s*\(", text):
        arguments = split_arguments(text, match.end())
        assert arguments and len(arguments) == 2, (source.name, match.group(0), arguments)
        calls += 1
        assert arguments[1] == "NULL", (
            f"{source.relative_to(ROOT)}: realpath({arguments[0]}, {arguments[1]}) passes a "
            "destination buffer; use realpath(path, NULL) and free the result, or a "
            "buffer of at least PATH_MAX bytes"
        )

assert calls >= 2, f"expected the realpath() call sites in src/, found {calls}"

# The allocated canonical names must be released on every return path, including
# the failure one: the function compares them, frees both, then reports.
theme = (ROOT / "src" / "display_theme.h").read_text(encoding="utf-8")
check = body_of(theme, "static int disp_active_theme_ok(void)")
assert "realpath(target, NULL)" in check, "the active-theme link is resolved by libc"
assert "realpath(disp_theme_dir, NULL)" in check
for variable in ("resolved", "here"):
    assert f"free({variable})" in check, f"{variable} must be freed before the function returns"

print(f"PASS: {calls} realpath() call(s) let libc allocate the destination; "
      "the active-theme check frees both canonical names")
