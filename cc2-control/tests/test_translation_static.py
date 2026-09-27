#!/usr/bin/env python3
"""Release markers for bilingual static and runtime UI content."""

from pathlib import Path


html = (Path(__file__).resolve().parents[1] / "web" / "index.html").read_text(encoding="utf-8")
required = (
    "Interface preferences are stored on the printer and shared by every browser.",
    "Le preferenze dell’interfaccia sono salvate sulla stampante e condivise da tutti i browser.",
    "loadUiPreferences()",
    "request('/api/preferences')",
    "method:'PUT'",
    "setLanguage(preferenceSelects[0].value==='Italiano'?'it':'en',true)",
    "Choose print spool':'Scegli bobina di stampa",
    "Object Exclusion':'Esclusione oggetti",
    "Console is locked. Unlock Expert Mode to enable command input.",
    "La console è bloccata. Sblocca la modalità esperto per inserire comandi.",
)
for marker in required:
    assert marker in html, f"missing bilingual marker: {marker}"
print("PASS: bilingual UI and persistent-language markers")
