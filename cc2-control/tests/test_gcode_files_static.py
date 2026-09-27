#!/usr/bin/env python3
"""Static release guard for the G-code library and native start request."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text(encoding="utf-8")
mqtt = (root / "src/mqtt.c").read_text(encoding="utf-8")
web = (root / "web/index.html").read_text(encoding="utf-8")

required_main = (
    'GCODE_INTERNAL_ROOT "/opt/usr/gcode/local"',
    'GCODE_USB_ROOT "/mnt/exUDISK"',
    'strcmp(path,"/api/gcode-files")',
    'strcmp(path,"/api/gcode-files/inspect")',
    'strcmp(path,"/api/gcode-files/print")',
    "mqtt->machine_status != 1",
    "S_ISLNK",
    "GCODE_FILES_MAX 128",
    "gcode_detect_tools",
    "gcode_has_adaptive_mesh",
    "parse_slot_map",
    "gcode_import_usb",
    'print_media = "local"',
    'GCODE_USB_IMPORT_PREFIX "CC2_USB_"',
)
required_mqtt = (
    '"method\\\":1020',
    'strcmp(storage_media, "local")',
    'strcmp(storage_media, "u-disk")',
    '"printer_check\\\":false',
    'bedlevel_force ? "true" : "false"',
    "print_layout != 'A' && print_layout != 'B'",
    '"canvas_id\\\":0',
    '"tray_id\\\":%d',
    'tools[index], trays[index]',
)
required_web = (
    'data-page="files"',
    'async function refreshFiles()',
    'async function startFile(storage, path)',
    "overlay.id='cc2PrintOverlay'",
    'name="useCanvas"',
    'Choose print spool',
    '/api/gcode-files/inspect',
    'select.dataset.tool',
    'Importing USB G-code to internal storage',
    'name="plateSide"',
    "form.elements.plateSide.value",
    "profiles.default1",
    "meshAvailable",
    'id="meshProfile"',
    'name="leveling"',
    'inspection.adaptive_mesh',
)

for marker in required_main:
    assert marker in main, f"missing backend guard: {marker}"
for marker in required_mqtt:
    assert marker in mqtt, f"missing native start field: {marker}"
for marker in required_web:
    assert marker in web, f"missing dashboard feature: {marker}"

print("PASS: G-code storage, Canvas popup, safety guards and native method 1020 markers are present.")
