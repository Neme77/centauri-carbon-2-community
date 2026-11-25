#!/usr/bin/env python3
"""Reject async predicates that Playwright waitForFunction accepts before they become true."""
import re
from pathlib import Path

ASYNC_WAIT = re.compile(r"\.waitForFunction\s*\(\s*async\b")

# Playwright 1.62.1 tests the Promise for truthiness and resolves with its value,
# even when that value is false. Evaluate async work first, then poll synchronously,
# or poll the API from Node with an explicit assertion.
assert ASYNC_WAIT.search("page.waitForFunction(async () => false)")
assert ASYNC_WAIT.search("page.waitForFunction(\n async function () { return false })")
assert not ASYNC_WAIT.search("page.waitForFunction(() => window.ready)")
assert not ASYNC_WAIT.search("page.evaluate(async () => false)")

preview = Path(__file__).resolve().parents[1] / "web-src" / "preview"
for script in sorted(preview.glob("*.mjs")):
    source = script.read_text(encoding="utf-8")
    match = ASYNC_WAIT.search(source)
    assert match is None, f"{script.name}: async waitForFunction predicate can pass with false"

print("PASS: browser waitForFunction predicates are synchronous")
