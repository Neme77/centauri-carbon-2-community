#!/usr/bin/env python3
"""Identifiers used by the UI and the entries of en.json must match both ways.

A t('group.name') without its en.json entry fails the TypeScript build already;
this also catches an entry nobody uses any more and a backend machine state
(main.c machine_status_name) with no state.* text to translate.
"""
import json
import re
from pathlib import Path

from _websrc import LOCALES, read_src

en = json.loads((LOCALES / "en.json").read_text(encoding="utf-8"))
src = read_src()

# Ids used in the sources (any quoted <group>.<name>), plus the backend machine-state names shown through tState().
used = set(re.findall(r"""['"`]([a-z0-9]+\.[a-z0-9_]+)['"`]""", src)) & set(en)
literals = {m for m in re.findall(r"""\b(?:t|tpl)\(\s*['"]([^'"]+)['"]""", src)}
missing = sorted(k for k in literals if k not in en)
assert not missing, f"{len(missing)} t()/tpl() id(s) missing from en.json: {missing[:8]}"

names = re.search(r'names\[\]=\{([^}]*)\}', (Path(__file__).resolve().parents[1] / "src" / "main.c").read_text(encoding="utf-8")).group(1)
state_texts = set(re.findall(r'"([^"]+)"', names)) | {"Offline", "Unknown"}
missing_states = sorted(s for s in state_texts if s not in {v for k, v in en.items() if k.startswith("state.")})
assert not missing_states, f"backend machine states without a state.* entry: {missing_states}"

unused = sorted(k for k in en if k not in used and not k.startswith("state."))
assert not unused, f"{len(unused)} en.json id(s) not used by any source: {unused[:8]}"
print(f"PASS: {len(literals)} t()/tpl() ids exist in en.json, no unused ids, all machine states covered")
