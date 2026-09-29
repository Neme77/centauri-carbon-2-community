#!/usr/bin/env python3
"""web/ and the firmware overlay must carry exactly what the build produces (index.html and locales).

prepare_firmware_overlay.sh copies them on every firmware build, but the overlay
copies are committed too; this catches one being updated without the other.
web/index.html and web/locales are build artifacts of web-src/ (CI rebuilds them and fails on a diff);
this also catches a locale added or removed in web-src/public/locales without a build.
"""
from pathlib import Path

from _websrc import LOCALES

root = Path(__file__).resolve().parents[1]
web = root / "web"
overlay = root / "firmware-integration/overlay/opt/inst/cc2-control/web"

pairs = [(web / "index.html", overlay / "index.html")]
names = sorted(p.name for p in LOCALES.glob("*.json"))
assert names, "no locale files in web-src/public/locales"
assert names == sorted(p.name for p in (web / "locales").glob("*.json")), "web/locales differs from web-src/public/locales: run npm run build"
pairs += [(LOCALES / n, web / "locales" / n) for n in names]
assert names == sorted(p.name for p in (overlay / "locales").glob("*.json")), "overlay locale files differ from web/locales"
pairs += [(web / "locales" / n, overlay / "locales" / n) for n in names]

for source, copy in pairs:
    assert source.read_bytes() == copy.read_bytes(), f"{copy.relative_to(root)} differs from {source.relative_to(root)}"

print(f"PASS: firmware overlay matches web/ ({len(pairs)} files)")
