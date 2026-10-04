#!/usr/bin/env python3
"""Polling must go through the single scheduler in lib/poll.ts (the CC2 is resource-constrained)."""
import re

from _websrc import SRC, read_src

src = read_src()
poll = (SRC / "lib" / "poll.ts").read_text(encoding="utf-8")

# No stray interval loops: every periodic request is registered with poll()/usePoll().
assert "setInterval" not in src, "use poll()/usePoll() from lib/poll.ts instead of setInterval"

# Scheduler guarantees: no overlapping runs of a source, sleep while the tab is hidden, wake up on return.
for marker in ("busy", "document.hidden", "visibilitychange", "Math.min(...t.intervals)"):
    assert marker in poll, f"lib/poll.ts lost a guarantee: {marker}"

# The OrcaSlicer pending-print check keeps running in a hidden WebView.
assert re.search(r"poll\(checkOrcaPendingPrint, 1500, \{ hidden: true \}\)", src), "Orca check must keep polling when hidden"

print("PASS: periodic requests use the shared non-overlapping, visibility-aware scheduler")
