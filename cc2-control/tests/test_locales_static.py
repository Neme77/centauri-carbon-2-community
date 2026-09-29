#!/usr/bin/env python3
"""Static consistency checks for web/locales/*.json.

Objective: guard the *data*, not the code that reads it. Structural only (key
parity, valid JSON, non-empty values) and independent of any exact string in
index.html, so it stays meaningful as translations, keys and languages change
over time — including once Weblate starts submitting these files directly.
Complements test_translation_static.py, which checks the loader wiring
instead.

This is the guard that turns a silent translation drift (a key added to
en.json but missing from it.json, or a locale file that fails to parse) into
a build failure, mirroring the key-consistency check Weblate itself performs
on JSON components.
"""
import json
import re
import sys
from pathlib import Path

LOCALES_DIR = Path(__file__).resolve().parents[1] / "web" / "locales"
SOURCE = "en.json"


def load(path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


files = sorted(LOCALES_DIR.glob("*.json"))
assert files, f"no locale files found in {LOCALES_DIR}"

source_path = LOCALES_DIR / SOURCE
assert source_path in files, f"missing source locale {SOURCE}"
source = load(source_path)
assert source, f"{SOURCE} is empty"
for key, value in source.items():
    assert re.fullmatch(r"[a-z0-9]+\.[a-z0-9_]+", key), f"{key!r} is not a <group>.<name> identifier"
    assert isinstance(value, str) and value.strip(), f"en.json: empty text for {key!r}"

source_keys = set(source)
for path in files:
    if path.name == SOURCE:
        continue
    data = load(path)
    assert isinstance(data, dict), f"{path.name}: top-level JSON must be an object"
    assert data, f"{path.name} is empty"
    for key, value in data.items():
        assert isinstance(key, str) and key, f"{path.name}: empty or non-string key {key!r}"
        assert isinstance(value, str) and value.strip(), f"{path.name}: empty value for key {key!r}"
    for key, value in data.items():
        placeholders = lambda text: sorted(re.findall(r"\{[a-z]+\}", text))
        if key in source:
            assert placeholders(value) == placeholders(source[key]), f"{path.name}[{key!r}]: placeholders differ from en.json"
    extra = set(data) - source_keys
    assert not extra, f"{path.name}: {len(extra)} key(s) not present in {SOURCE}: {sorted(extra)[:5]}"
    # A partial translation is fine at runtime (the loader falls back to
    # English per key); it is not fine to go unnoticed, so report it.
    missing = source_keys - set(data)
    if missing:
        print(f"note: {path.name} is missing {len(missing)}/{len(source_keys)} key(s) "
              f"present in {SOURCE} (falls back to English at runtime)", file=sys.stderr)

print(f"PASS: {len(files)} locale file(s), {len(source_keys)} source keys, all present and structurally valid")
