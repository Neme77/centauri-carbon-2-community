#!/usr/bin/env python3
"""Release markers for the JSON-backed translation and persistent-language wiring.

Objective: catch a regression in how the web UI loads/applies translations
(the /i18n/ fetch or the LANGUAGE_NAMES map disappearing, untranslated
notify() toasts creeping back) rather than in the translations themselves —
see test_locales_static.py for that. These are literal substring markers
against the readable sources in web-src/, so expect to update them whenever
the checked lines are touched.
"""

import json
import re
from pathlib import Path

from _websrc import read_src

WEB = Path(__file__).resolve().parents[1] / "web"
src = read_src()
required = (
    "Interface preferences are stored on the printer and shared by every browser.",
    "loadUiPreferences",
    "'/api/preferences'",
    "method: 'PUT'",
    "Console is locked. Unlock Expert Mode to enable command input.",
    "LANGUAGE_NAMES",
    "setLanguage(e.currentTarget.value, true)",
    "fetch(`/i18n/${code}.json`",
    "const dict = lang === 'en' ? {} : await loadLocale(lang)",
)
for marker in required:
    assert marker in src, f"missing translation-wiring marker: {marker}"

# Every toast goes through t()/tpl(); `m` is an already-translated message and errText() is backend text.
for match in re.finditer(r"\bnotify\(([^)]*)", src):
    arg = match.group(1).strip()
    assert arg.startswith(("t(", "tpl(", "errText(")) or arg == "m", f"untranslated notify(): {match.group(0)}"

for locale, spot_checks in {
    "it": {
        "Object Exclusion": "Esclusione oggetti",
        "Console is locked. Unlock Expert Mode to enable command input.":
            "La console è bloccata. Sblocca la modalità esperto per inserire comandi.",
    },
    "fr": {
        "Object Exclusion": "Exclusion d’objet",
        "Console is locked. Unlock Expert Mode to enable command input.":
            "La console est verrouillée. Déverrouillez le mode Expert pour saisir des commandes.",
    },
}.items():
    data = json.loads((WEB / "locales" / f"{locale}.json").read_text(encoding="utf-8"))
    for key, expected in spot_checks.items():
        assert data.get(key) == expected, f"{locale}.json[{key!r}]: expected {expected!r}, got {data.get(key)!r}"

print("PASS: translation loader wiring and Italian/French spot-check markers")
