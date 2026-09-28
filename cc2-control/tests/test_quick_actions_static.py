#!/usr/bin/env python3
from pathlib import Path

html = (Path(__file__).parents[1] / "web" / "index.html").read_text(encoding="utf-8")
for marker in (
    "data-quick-edit",
    "quickActionChoices",
    "renderQuickActions()",
    "persistUiPreferences()",
    "light:toggle",
    "page:files",
    "page:bed",
    "page:canvas",
):
    assert marker in html, marker
print("PASS: configurable persistent Quick Actions UI markers")
