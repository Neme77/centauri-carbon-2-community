#!/usr/bin/env python3
"""Release markers for the JSON-backed translation and persistent-language wiring.

Objective: catch a regression in how index.html loads/applies translations
(the inline dictionary coming back, the /i18n/ fetch or LANGUAGE_NAMES map
disappearing) rather than in the translations themselves — see
test_locales_static.py for that. These are literal substring markers against
the live source, so expect to update them whenever the checked lines are
touched, even for an unrelated, correct change (e.g. the notify() toast
translation work: it currently doesn't test that path, but if it changes any
line already listed here, this file needs updating too).
"""

import json
from pathlib import Path

WEB = Path(__file__).resolve().parents[1] / "web"
html = (WEB / "index.html").read_text(encoding="utf-8")
required = (
    "Interface preferences are stored on the printer and shared by every browser.",
    "loadUiPreferences()",
    "request('/api/preferences')",
    "method:'PUT'",
    "Console is locked. Unlock Expert Mode to enable command input.",
    "LANGUAGE_NAMES",
    "languageCodeForLabel(preferenceSelects[0].value)",
    "fetch(`/i18n/${code}.json`",
    "italian=currentLanguage==='en'?{}:await loadLocale(currentLanguage)",
)
for marker in required:
    assert marker in html, f"missing translation-wiring marker: {marker}"

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
