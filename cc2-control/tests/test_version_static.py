#!/usr/bin/env python3
"""cc2-control/VERSION and FIRMWARE_VERSION are the only places the CC2 Control version and the community firmware release are written."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo = root.parent
version = (root / "VERSION").read_text().strip()
assert re.fullmatch(r"\d+\.\d+\.\d+", version), f"VERSION must be MAJOR.MINOR.PATCH, got {version!r}"
firmware = (root / "FIRMWARE_VERSION").read_text().strip()
assert re.fullmatch(r"\d+\.\d+", firmware), f"FIRMWARE_VERSION must be MAJOR.MINOR, got {firmware!r}"

# The built binary carries the version from VERSION (skipped when no binary path is given).
if len(sys.argv) > 1:
    binary = Path(sys.argv[1]).read_bytes()
    assert version.encode() in binary, "binary was not built from VERSION"
    assert firmware.encode() in binary, "binary was not built from FIRMWARE_VERSION"

# Code that ships or runs must not carry its own copy. Docs, changelog and tests may name a release.
code = [*root.glob("src/*"), *root.glob("installer/*"), *root.glob("scripts/*"), root / "Makefile",
        *root.glob("firmware-integration/**/*.sh"),
        *repo.glob("builder/current/*.py"), *repo.glob("builder/current/*.ps1"), *repo.glob("builder/current/core/*.py"),
        *repo.glob("builder/current/components/cc2-control/runtime/*")]
copies = [str(p.relative_to(repo)) for p in code if p.is_file() and version in p.read_text(encoding="utf-8", errors="ignore")]
assert not copies, f"version {version} is written outside VERSION in: {copies}"
# A release written as 4.2 or v4.2, but not as part of 1.4.2 or 4.2.1.
fw = re.compile(rf"(?<![\d.])v?{re.escape(firmware)}(?![\d.])", re.I)
copies = [str(p.relative_to(repo)) for p in code if p.is_file() and fw.search(p.read_text(encoding="utf-8", errors="ignore"))]
assert not copies, f"firmware release {firmware} is written outside FIRMWARE_VERSION in: {copies}"
print(f"PASS: version {version} and firmware release {firmware} are each defined once")
