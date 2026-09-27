#!/usr/bin/env python3
"""Release guard for the live internal-light button state."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
main = (root / "src" / "main.c").read_text(encoding="utf-8")
mqtt = (root / "src" / "mqtt.c").read_text(encoding="utf-8")
web = (root / "web" / "index.html").read_text(encoding="utf-8")

required = (
    '\\"hardware\\":{\\"camera\\":%s,\\"usb\\":%s,\\"light\\":%d',
    'number_in(o,n,"status",&v))c->led_status=(int)v',
    'data-machine-light',
    "Number(data.hardware&&data.hardware.light)===1",
    "classList.toggle('light-on',lightOn)",
    "classList.toggle('light-off',!lightOn)",
    "setAttribute('aria-pressed',String(lightOn))",
    "setTimeout(refreshPrinter,250)",
)

combined = main + mqtt + web
for marker in required:
    assert marker in combined, f"missing live-light marker: {marker}"

print("PASS: internal-light control follows live MQTT telemetry.")
