#!/usr/bin/env python3
"""The committed firmware overlay must carry exactly the web/ files (index.html and locales).

prepare_firmware_overlay.sh copies them on every firmware build, but the overlay
copies are committed too; this catches one being updated without the other.
web/index.html itself is a build artifact of web-src/ (CI rebuilds it and fails on a diff).
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
overlay = root / "firmware-integration/overlay/opt/inst/cc2-control/web"

pairs = [(web / "index.html", overlay / "index.html")]
names = sorted(p.name for p in (web / "locales").glob("*.json"))
assert names, "no locale files in web/locales"
assert names == sorted(p.name for p in (overlay / "locales").glob("*.json")), "overlay locale files differ from web/locales"
pairs += [(web / "locales" / n, overlay / "locales" / n) for n in names]

for source, copy in pairs:
    assert source.read_bytes() == copy.read_bytes(), f"{copy.relative_to(root)} differs from {source.relative_to(root)}"

print(f"PASS: firmware overlay matches web/ ({len(pairs)} files)")
