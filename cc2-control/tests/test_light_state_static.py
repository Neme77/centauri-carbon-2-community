#!/usr/bin/env python3
"""Release guard for the live internal-light button state."""

from pathlib import Path

from _websrc import read_src

root = Path(__file__).resolve().parents[1]
main = (root / "src" / "main.c").read_text(encoding="utf-8")
mqtt = (root / "src" / "mqtt.c").read_text(encoding="utf-8")
web = read_src()

required = (
    '\\"hardware\\":{\\"camera\\":%s,\\"usb\\":%s,\\"light\\":%d',
    'number_in(o,n,"status",&v))c->led_status=(int)v',
    "lightOn: Number(d?.hardware?.light) === 1",
    "aria-pressed={v.lightOn}",
    "control(v.lightOn ? 'light:off' : 'light:on')",
    "setTimeout(refreshPrinter, 250)",
)

combined = main + mqtt + web
for marker in required:
    assert marker in combined, f"missing live-light marker: {marker}"

print("PASS: internal-light control follows live MQTT telemetry.")
