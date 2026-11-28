#!/usr/bin/env python3
"""Every realpath() destination must meet the libc buffer contract.

glibc's fortified realpath() accepts either NULL (POSIX.1-2008: libc allocates
the canonical name) or a buffer of at least PATH_MAX bytes, even when the
resulting name is short. cc2-control keeps its own short paths in
PATH_MAX_LOCAL (512) arrays, which is smaller than PATH_MAX, so passing one of
those as the destination is undefined behaviour and makes a fortified -Werror
build fail:

    error: call to '__realpath_chk_warn' declared with attribute warning:
    second argument of realpath must be either NULL or at least PATH_MAX bytes
    long buffer

The CI hosts enable _FORTIFY_SOURCE by default and a developer machine may not,
so this check keeps the defect from returning unnoticed. The shipped display
code uses PATH_MAX-sized destinations (DISP_REALPATH_MAX, from <limits.h>); the
connection resolver uses the allocation form. Both are safe; a buffer of any
other size, or an unknown variable, is refused. A synthetic negative case at the
end proves the check rejects the original 512-byte form.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = [ROOT / "src", ROOT / "tests", ROOT / "experimental" / "network-connections"]
SOURCES = sorted(
    path
    for directory in DIRECTORIES
    if directory.is_dir()
    for pattern in ("*.h", "*.c")
    for path in directory.rglob(pattern)
)


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


def declared_size(text, identifier):
    """The declared array size of `identifier[…]`, or None.

    Several buffers may share one `char a[N], b[N];` statement, so the match
    starts at the name itself rather than at the `char` keyword."""
    found = re.search(rf"\b{re.escape(identifier)}\s*\[\s*([^\]]+?)\s*\]", text)
    return found.group(1) if found else None


def realpath_problems(text, raw, label):
    """Every contract violation in one translation unit (comment-free text)."""
    problems = []
    for match in re.finditer(r"\brealpath\s*\(", text):
        arguments = split_arguments(text, match.end())
        if not arguments or len(arguments) != 2:
            problems.append(f"{label}: cannot parse realpath() arguments")
            continue
        destination = arguments[1]
        if destination == "NULL":
            continue  # POSIX.1-2008 allocation form: libc owns the buffer
        if not re.fullmatch(r"[A-Za-z_]\w*", destination):
            problems.append(
                f"{label}: realpath({arguments[0]}, {destination}) is not a plain "
                "buffer name; use NULL or a PATH_MAX-sized array"
            )
            continue
        size = declared_size(text, destination)
        if size is None:
            problems.append(
                f"{label}: cannot find the declaration of the realpath() destination "
                f"'{destination}'"
            )
        elif size not in ("PATH_MAX", "DISP_REALPATH_MAX"):
            problems.append(
                f"{label}: realpath({arguments[0]}, {destination}) passes a buffer of "
                f"{size}; it must be NULL or PATH_MAX bytes (PATH_MAX or "
                "DISP_REALPATH_MAX from <limits.h>)"
            )
        elif size == "DISP_REALPATH_MAX":
            if "#define DISP_REALPATH_MAX PATH_MAX" not in raw:
                problems.append(f"{label}: DISP_REALPATH_MAX must come from <limits.h>")
            if "#include <limits.h>" not in raw:
                problems.append(f"{label}: <limits.h> must be included for PATH_MAX")
    return problems


calls = path_max_calls = 0
for source in SOURCES:
    raw = source.read_text(encoding="utf-8")
    text = code_only(raw)
    problems = realpath_problems(text, raw, str(source.relative_to(ROOT)))
    assert not problems, "\n".join(problems)
    calls += len(re.findall(r"\brealpath\s*\(", text))
    path_max_calls += sum(
        1
        for match in re.finditer(r"\brealpath\s*\(", text)
        if (split_arguments(text, match.end()) or ["", ""])[1] != "NULL"
    )

assert calls >= 4, f"expected the realpath() call sites, found {calls}"
assert path_max_calls >= 2, (
    "the display active-theme check must resolve both paths into PATH_MAX buffers"
)

theme = (ROOT / "src" / "display_theme.h").read_text(encoding="utf-8")
assert "realpath(target, resolved)" in theme and "realpath(disp_theme_dir, here)" in theme
assert re.search(r"\bresolved\s*\[\s*DISP_REALPATH_MAX\s*\]", theme)
assert re.search(r"\bhere\s*\[\s*DISP_REALPATH_MAX\s*\]", theme)

# Negative test: the implementation this guard was written for must be rejected.
old_form = """
static int old_theme_check(void) {
    char resolved[PATH_MAX_LOCAL], here[PATH_MAX_LOCAL];
    if (!realpath("/opt/usr/images/current_theme", resolved)) return 0;
    if (!realpath("/opt/usr/images/theme_default", here)) return 0;
    return !strcmp(here, resolved);
}
"""
assert realpath_problems(code_only(old_form), "", "negative") != [], (
    "the guard must reject the original PATH_MAX_LOCAL destination buffers"
)
safe_form = """
#define DISP_REALPATH_MAX PATH_MAX
#include <limits.h>
static int new_theme_check(void) {
    char resolved[DISP_REALPATH_MAX], here[DISP_REALPATH_MAX];
    return realpath("/a", resolved) && realpath("/b", here) && !strcmp(here, resolved);
}
"""
assert realpath_problems(code_only(safe_form), safe_form, "negative") == []

print(f"PASS: {calls} realpath() call(s) are safe ({path_max_calls} with PATH_MAX "
      "buffers, the rest allocation form); the old 512-byte form is rejected")
