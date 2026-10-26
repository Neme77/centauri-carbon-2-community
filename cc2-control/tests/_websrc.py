"""Shared helper: the readable web UI source that the static tests scan.

web/index.html is a minified build artifact, so markers are matched against
web-src/ (the TypeScript/CSS sources) instead.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "web-src" / "src"
# Translation sources; the build copies them to web/locales, which the backend serves.
LOCALES = ROOT / "web-src" / "public" / "locales"


def read_src():
    return "\n".join(p.read_text(encoding="utf-8") for p in sorted(SRC.rglob("*")) if p.suffix in {".ts", ".tsx", ".css"})
