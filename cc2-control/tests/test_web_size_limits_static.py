#!/usr/bin/env python3
"""The served web files stay clear of the sizes the backend and the page refuse.

The backend reads web/index.html and web/locales/<code>.json whole into memory and sends nothing above a fixed size
(serve_index: 1 MiB, serve_locale: 256 KiB, cc2-control/src/main.c); the page rejects a locale above 262144 characters
(web-src/src/lib/i18n.ts, boundedJson). A file that grows past its limit makes the start page unreachable or drops a
language back to English. This test fails while there is still room, so the fix (smaller file, compressed delivery or a
raised limit in all three places) can be made before a release is affected:

  - every web/locales/*.json at most 240 KiB (limit 256 KiB, alarm at 93.7 %)
  - web/index.html at most 940 KiB (limit 1024 KiB, alarm at 92 %)
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = root / "web"
KIB = 1024

# The appearance studio added a bounded page and 200+ translated labels. The
# alarms stay below the limits the backend and the page really enforce, with at
# least 5 % headroom so the next change fails before the limit is reached.
INDEX_LIMIT, INDEX_ALARM = 1024 * KIB, 940 * KIB
LOCALE_LIMIT, LOCALE_ALARM = 256 * KIB, 240 * KIB
HINT = ("serve_index/serve_locale in cc2-control/src/main.c answer a file above the limit with HTTP 500, and "
        "boundedJson in cc2-control/web-src/src/lib/i18n.ts drops a locale above 262144 characters; "
        "shrink the file, deliver it compressed, or raise the limit in all three places and this test together")

# The thresholds above only mean something while they sit below the limits the code really has.
main_c = (root / "src" / "main.c").read_text(encoding="utf-8")
i18n_ts = (root / "web-src" / "src" / "lib" / "i18n.ts").read_text(encoding="utf-8")
def define_value(name):
    """Value of `#define NAME <n>` or `#define NAME (<n> * <m>)` in main.c, or None."""
    found = re.search(rf"#define\s+{name}\s+\(?\s*(\d+)(?:\s*\*\s*(\d+))?\s*\)?\s*$", main_c, re.M)
    return None if not found else int(found.group(1)) * int(found.group(2) or 1)


index_max, locale_max = define_value("WEB_INDEX_MAX_BYTES"), define_value("WEB_LOCALE_MAX_BYTES")
assert index_max and locale_max, "src/main.c no longer defines WEB_INDEX_MAX_BYTES and WEB_LOCALE_MAX_BYTES"
assert index_max == INDEX_LIMIT, f"WEB_INDEX_MAX_BYTES changed: update INDEX_LIMIT and INDEX_ALARM in this test. {HINT}"
assert locale_max == LOCALE_LIMIT, f"WEB_LOCALE_MAX_BYTES changed: update LOCALE_LIMIT and LOCALE_ALARM in this test. {HINT}"
assert f"text.length > {LOCALE_LIMIT}" in i18n_ts, f"i18n.ts no longer limits a locale to {LOCALE_LIMIT} characters: update this test. {HINT}"

checks = [(web / "index.html", INDEX_ALARM, INDEX_LIMIT)]
checks += [(p, LOCALE_ALARM, LOCALE_LIMIT) for p in sorted((web / "locales").glob("*.json"))]
assert len(checks) > 1, "no locale files in web/locales"

failures = []
for path, alarm, limit in checks:
    size = path.stat().st_size
    if size > alarm:
        failures.append(f"{path.relative_to(root.parent)} is {size} bytes ({size / limit:.1%} of the {limit}-byte limit); "
                        f"the test allows {alarm} bytes. {HINT}")
assert not failures, "\n".join(failures)

biggest_index = checks[0][0].stat().st_size
biggest_locale = max(p.stat().st_size for p, _, _ in checks[1:])
print(f"PASS: index.html {biggest_index / INDEX_LIMIT:.1%} of {INDEX_LIMIT}, largest locale {biggest_locale / LOCALE_LIMIT:.1%} of {LOCALE_LIMIT} ({len(checks) - 1} locales)")
