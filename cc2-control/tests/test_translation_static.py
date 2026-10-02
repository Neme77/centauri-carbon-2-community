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

from _websrc import LOCALES, read_src

src = read_src()
required = (
    "settings.interface_preferences_are_stored",
    "loadUiPreferences",
    "'/api/preferences'",
    "method: 'PUT'",
    "console.unlock_console_to_send_g_code",
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
    assert arg.startswith(("t(", "tpl(", "errText(")) or re.fullmatch(r"m(, 'error')?", arg), f"untranslated notify(): {match.group(0)}"

for locale, spot_checks in {
    "it": {
        "job.object_exclusion": "Esclusione oggetti",
        "console.unlock_console_to_send_g_code":
            "Sblocca la console per inviare G-code…",
    },
    "fr": {
        "job.object_exclusion": "Exclusion d’objet",
        "console.unlock_console_to_send_g_code":
            "Déverrouillez la console pour envoyer du G-code…",
    },
    "zh": {
        "job.object_exclusion": "对象排除",
        "console.unlock_console_to_send_g_code":
            "解锁控制台以发送 G-code…",
    },
}.items():
    data = json.loads((LOCALES / f"{locale}.json").read_text(encoding="utf-8"))
    for key, expected in spot_checks.items():
        assert data.get(key) == expected, f"{locale}.json[{key!r}]: expected {expected!r}, got {data.get(key)!r}"

assert "navigator.languages" in src and "p.language || detectLanguage()" in src, "browser language fallback missing"

print("PASS: translation loader wiring and spot-check markers")
