#!/usr/bin/env python3
from _websrc import read_src

src = read_src()

required = (
    'class="block h-32 w-full"',
    "Math.min(devicePixelRatio || 1, 2)",
    "canvas.width = Math.round(w * dpr)",
    "canvas.height = Math.round(h * dpr)",
    "ctx.setTransform(dpr, 0, 0, dpr, 0, 0)",
    "times[0] < Date.now() - 300_000",
)

for marker in required:
    assert marker in src, f"missing thermal-chart layout marker: {marker}"

print("PASS: fixed thermal-chart layout and high-DPI rendering markers are present.")
