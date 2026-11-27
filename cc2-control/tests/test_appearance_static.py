#!/usr/bin/env python3
"""The appearance contract must stay identical on both sides.

The dashboard sends widget identifiers and colour-token names to
src/appearance.h; a drift would be rejected at runtime (or, worse, silently
fall back). This static guard also checks that the customisation layer has no
HTML/CSS/JS injection escape hatch.
"""
import re
from pathlib import Path

from _websrc import SRC

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src" / "appearance.h").read_text(encoding="utf-8")
ts = (SRC / "lib" / "appearance.ts").read_text(encoding="utf-8")
page = (SRC / "pages" / "appearance.tsx").read_text(encoding="utf-8")
display = (SRC / "lib" / "display.ts").read_text(encoding="utf-8")

# Widgets
server_widgets = re.search(r"ap_widget_ids\[AP_MAX_WIDGETS\]\s*=\s*\{([^}]*)\}", header, re.S).group(1)
server_widgets = re.findall(r'"([a-zA-Z]+)"', server_widgets)
client_widgets = re.search(r"export const WIDGET_IDS: WidgetId\[\]\s*=\s*\[([^\]]*)\]", ts, re.S).group(1)
client_widgets = re.findall(r"'([a-zA-Z]+)'", client_widgets)
assert server_widgets == client_widgets, (server_widgets, client_widgets)

# Colour tokens
server_tokens = re.search(r"ap_token_names\[AP_TOKEN_COUNT\]\s*=\s*\{([^}]*)\}", header, re.S).group(1)
server_tokens = re.findall(r'"([a-zA-Z]+)"', server_tokens)
client_tokens = re.search(r"export const TOKEN_IDS: ThemeToken\[\]\s*=\s*\[([^\]]*)\]", ts, re.S).group(1)
client_tokens = re.findall(r"'([a-zA-Z]+)'", client_tokens)
assert server_tokens == client_tokens, (server_tokens, client_tokens)

# Schema version and bounds
assert re.search(r"#define AP_VERSION 1\b", header)
assert re.search(r"version: 1", ts)
for limit in ("AP_MAX_PROFILES", "AP_MAX_LAYOUTS", "AP_MAX_WIDGETS", "AP_MAX_THEMES"):
    assert re.search(rf"#define {limit} \d+", header), limit

# No injection escape hatch: the customisation sources must not build HTML,
# evaluate strings or accept a free-form CSS field.
for name, text in (("appearance.ts", ts), ("appearance.tsx", page), ("display.ts", display)):
    for forbidden in ("dangerouslySetInnerHTML", "innerHTML", "outerHTML", "eval(", "new Function(", "document.write"):
        assert forbidden not in text, f"{name} uses {forbidden}"
assert "customCss" not in ts and "customCss" not in page
assert "http://" not in ts.replace("http://127.0.0.1", "")  # no remote styles or fonts

# The server exposes exactly the documented routes.
for route in (
    '"/api/appearance"',
    '"/api/appearance/themes"',
    '"/api/appearance/themes/delete"',
    '"/api/appearance/profiles"',
    '"/api/appearance/profiles/apply"',
    '"/api/appearance/profiles/delete"',
    '"/api/appearance/reset"',
    '"/api/display"',
    '"/api/display/asset"',
    '"/api/display/apply"',
    '"/api/display/restore"',
):
    assert route in (ROOT / "src" / "main.c").read_text(encoding="utf-8"), route

# A profile and a layout named "classic"/"community" are reserved markers.
assert "'classic'" in ts and "'community'" in ts
print(f"PASS: {len(client_widgets)} widgets and {len(client_tokens)} tokens match; no injection path; routes present")
