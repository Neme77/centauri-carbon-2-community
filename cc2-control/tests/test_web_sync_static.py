#!/usr/bin/env python3
"""web/locales must carry exactly what web-src/public/locales holds.

web/index.html and web/locales are build artifacts of web-src/ (CI rebuilds them and fails on a diff);
this catches a locale added or removed in web-src/public/locales without a build.
The firmware builder takes web/ from `make dist`, so there is no second copy to keep in sync.

The shipped copy is compact JSON (vite.config.ts compactLocales) because the
printer loads each catalogue whole into memory; the source files stay indented
for review. The comparison is therefore by parsed content, and the shipped
copy must not be larger than its source.
"""
import json
from pathlib import Path

from _websrc import LOCALES

root = Path(__file__).resolve().parents[1]
web = root / "web"

pairs = []
names = sorted(p.name for p in LOCALES.glob("*.json"))
assert names, "no locale files in web-src/public/locales"
assert names == sorted(p.name for p in (web / "locales").glob("*.json")), "web/locales differs from web-src/public/locales: run npm run build"
pairs += [(LOCALES / n, web / "locales" / n) for n in names]

for source, copy in pairs:
    text = copy.read_text(encoding="utf-8")
    assert json.loads(source.read_text(encoding="utf-8")) == json.loads(text), (
        f"{copy.relative_to(root)} differs from {source.relative_to(root)}: run npm run build")
    assert len(text) <= source.stat().st_size, f"{copy.relative_to(root)} is larger than its source"
    assert "\n" not in text.rstrip("\n"), f"{copy.relative_to(root)} is not compact JSON"

print(f"PASS: web/locales matches web-src/public/locales ({len(pairs)} files, compact)")
