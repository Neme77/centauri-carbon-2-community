#!/usr/bin/env python3
from _websrc import read_src

src = read_src()
for marker in (
    "QUICK_CHOICES",
    "Configure Quick Actions",
    "setQuickFromServer(p.quick_actions)",
    "`quick${i + 1}`",
    "quickAlwaysAvailable",
    "'light:toggle'",
    "'page:files'",
    "'page:bed'",
    "'page:canvas'",
):
    assert marker in src, marker
print("PASS: configurable persistent Quick Actions UI markers")
