#!/usr/bin/env python3
"""One theme-id contract and one palette list on both sides.

A custom theme id is written by /api/appearance and by the legacy
/api/preferences field; the studio, the import path and both backends must
agree or a theme can be accepted by one store and refused by the other. The
shipped palette ids must also match between the C validator, the CSS theme
blocks and the frontend list.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src" / "appearance.h").read_text(encoding="utf-8")
main = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
ts = (ROOT / "web-src" / "src" / "lib" / "appearance.ts").read_text(encoding="utf-8")
i18n = (ROOT / "web-src" / "src" / "lib" / "i18n.ts").read_text(encoding="utf-8")
css = (ROOT / "web-src" / "src" / "index.css").read_text(encoding="utf-8")

# --- id length ---------------------------------------------------------------
server_max = int(re.search(r"#define AP_THEME_ID_MAX (\d+)", header).group(1))
client_max = int(re.search(r"export const THEME_ID_MAX = (\d+)", ts).group(1))
legacy_function = main[main.index("static int theme_valid"):]
legacy_function = legacy_function[:legacy_function.index("}")]
legacy_max = int(re.search(r"if \(length < 2 \|\| length > (\d+)\) return 0;", legacy_function).group(1))
assert server_max == client_max == legacy_max == 24, (server_max, client_max, legacy_max)
assert "if (strlen(id) > AP_THEME_ID_MAX) return 0;" in header

# --- character rule ----------------------------------------------------------
# The server accepts "custom-" plus lowercase letters/hyphens; the clients must
# generate and normalise to the same shape.
assert "export const themeIdOk" in ts
assert "/^custom-[a-z][a-z-]*[a-z]$/" in ts
assert "!id.includes('--')" in ts
assert "!mode.includes('--')" in i18n
assert ".slice(0, 17)" in ts, "slug must leave room for 'custom-' and a suffix inside 24 characters"
assert "THEME_ID_MAX" in ts and "limit - tail.length" in ts, "uniqueThemeId must cap at the contract"
assert "normalizeTokens(tokens)" in ts, "createTheme must validate imported tokens"
assert "normalizeToken" in ts and "export const normalizeThemes" in ts
assert "appearance/themes/delete" in ts
assert "request('/api/appearance/themes'" in ts, "theme writes must be per item"
assert "themes: doc.themes" not in ts, "the document PUT must not replace the theme library"

# --- one shipped palette list ------------------------------------------------
server_list = re.search(r"static const char \*const builtin\[\] = \{([^}]*)\}", header, re.S).group(1)
server_themes = set(re.findall(r'"([a-z-]+)"', server_list))
server_themes.discard("auto")
client_themes = set(re.findall(r"\{ id: '([a-z-]+)'", i18n[i18n.index("export const THEMES"):i18n.index("const customThemeId")]))
client_themes.discard("auto")
assert server_themes == client_themes, (sorted(server_themes), sorted(client_themes))
css_themes = set(re.findall(r':root\[data-theme="([a-z-]+)"\]', css))
css_themes.discard("custom")
# The default palette (dark) is the base :root block, not a data-theme block.
if re.search(r"\n:root \{", css) and "--bg:" in css[css.index("\n:root {"):]:
    css_themes.add("dark")
assert client_themes <= css_themes, (sorted(client_themes - css_themes))
print(f"PASS: theme ids agree on 24 characters and characters; {len(client_themes)} shipped palettes match "
      "the C validator, the CSS blocks and the frontend list; themes are written per item")
