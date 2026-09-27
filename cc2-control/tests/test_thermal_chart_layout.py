#!/usr/bin/env python3
from pathlib import Path

html = (Path(__file__).resolve().parents[1] / "web/index.html").read_text(encoding="utf-8")

required = (
    ".thermal-body canvas{width:100%;height:130px;display:block}",
    "Math.min(devicePixelRatio || 1,2)",
    "canvas.width=Math.round(width*dpr)",
    "canvas.height=Math.round(height*dpr)",
    "ctx.setTransform(dpr,0,0,dpr,0,0)",
    "thermalHistory[key].length > 300",
)

for marker in required:
    assert marker in html, f"missing thermal-chart layout marker: {marker}"

assert "#dashboardTab>.overview .thermal-panel canvas{flex:1" not in html

print("PASS: fixed thermal-chart layout and high-DPI rendering markers are present.")
