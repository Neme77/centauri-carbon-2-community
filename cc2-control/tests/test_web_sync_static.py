#!/usr/bin/env python3
"""web/locales must carry exactly what web-src/public/locales holds.

web/index.html and web/locales are build artifacts of web-src/ (CI rebuilds them and fails on a diff);
this catches a locale added or removed in web-src/public/locales without a build.
The firmware builder takes web/ from `make dist`, so there is no second copy to keep in sync.
"""
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
    assert source.read_bytes() == copy.read_bytes(), f"{copy.relative_to(root)} differs from {source.relative_to(root)}"

print(f"PASS: web/locales matches web-src/public/locales ({len(pairs)} files)")
