#!/usr/bin/env python3
"""Every theme offered in Settings must define the whole palette in index.css (a missing token silently falls back to another theme)."""
import re

from _websrc import SRC

i18n = (SRC / "lib" / "i18n.ts").read_text(encoding="utf-8")
css = (SRC / "index.css").read_text(encoding="utf-8")

ids = re.findall(r"\{ id: '([a-z-]+)',", i18n)
assert {"dark", "light"} <= set(ids), ids
assert len(ids) == len(set(ids)), "duplicate theme id"

tokens = ("bg", "panel", "edge", "text", "muted", "field", "ink", "cyan", "green", "amber", "red", "blue", "well", "well-fg")
for theme in ids:
    selector = ":root {" if theme == "dark" else f':root[data-theme="{theme}"] {{'
    start = css.find(selector)
    assert start >= 0, f"index.css has no block for theme {theme!r}"
    block = css[start:css.index("}", start)]
    missing = [t for t in tokens if not re.search(rf"--{t}:\s*#", block)]
    assert not missing, f"theme {theme!r} lacks tokens: {missing}"
    assert "color-scheme:" in block, f"theme {theme!r} lacks color-scheme"

print(f"PASS: {len(ids)} themes define the full palette")
