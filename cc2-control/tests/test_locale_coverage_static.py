#!/usr/bin/env python3
"""Every literal UI string passed to t()/tpl() must exist in the source locale.

Catches a string added to web-src/ without its en.json (and so it.json/fr.json)
entry, which would silently stay English. Dynamic keys (t(variable)) and the
strings handed to components as props are out of reach of a regex; the goal is
the common case: a new t('…') or tpl('…', …) call.
"""
import json
import re
from pathlib import Path

from _websrc import read_src

en = json.loads((Path(__file__).resolve().parents[1] / "web" / "locales" / "en.json").read_text(encoding="utf-8"))
src = read_src()

used = {m.group(2) for m in re.finditer(r"""\b(?:t|tpl)\(\s*(['"])((?:\\.|(?!\1).)*)\1""", src)}
# t(cond ? 'a' : 'b')
for m in re.finditer(r"""\bt\(([^()]*\?[^()]*)\)""", src):
    branches = re.sub(r"""===?\s*'(?:\\.|[^'])*'""", "", m.group(1))  # drop comparisons like v === '3d'
    used.update(re.findall(r"""'((?:\\.|[^'])*)'""", branches))

# Machine state names come from the backend (main.c machine_status_name) and are shown through t().
names = re.search(r'names\[\]=\{([^}]*)\}', (Path(__file__).resolve().parents[1] / "src" / "main.c").read_text(encoding="utf-8")).group(1)
used.update(re.findall(r'"([^"]+)"', names))
used.update({"Offline", "Unknown", "Protected console ready.", "Protected console ready. Waiting for live printer output."})

missing = sorted(k for k in used if k not in en)
assert not missing, f"{len(missing)} UI string(s) missing from en.json: {missing[:8]}"
print(f"PASS: {len(used)} literal t()/tpl() keys all exist in en.json")
