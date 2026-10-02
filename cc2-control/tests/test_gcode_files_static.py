#!/usr/bin/env python3
"""Static release guard for the G-code library and native start request."""

from pathlib import Path

from _websrc import read_src

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text(encoding="utf-8")
mqtt = (root / "src/mqtt.c").read_text(encoding="utf-8")
web = read_src()

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
    "send_local_gcode_script",
    "saved_plate_mesh_exists",
    'strcmp(leveling, "calibrate")',
    'BED_MESH_CALIBRATE_SET EXECUTE_CALIBRATE_FROM_SLICER=1',
    'BED_MESH_CALIBRATE PROFILE=%s BED_TEMP=60',
    'SDCARD_PRINT_FILE FILENAME=%s/\\"%s\\" SLICE_CFG_MODEL=1',
    'print_media = "local"',
    'GCODE_USB_IMPORT_PREFIX "CC2_USB_"',
    'bounded_number_after_marker(line, "nozzle_temperature", 500.0)',
    'bounded_number_after_marker(line, "bed_temperature", 200.0)',
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
    "['files', FilesIcon, 'common.files']",
    "const refresh = async",
    "export async function startFile(storage: string, path: string)",
    "'print.choose_print_spool'",
    "useCanvas",
    "/api/gcode-files/inspect",
    "`${tool}:${map[tool]}`",
    "print.importing_usb_g_code_to_internal",
    "plateSide",
    "profiles.default1",
    "meshAvailable",
    "calibrating ? 'calibrate' : 'saved'",
    "temp(meta.nozzle_temperature, 500)",
    "const metaCache = new Map",
    "metaCache.set(k, m)",
)

for marker in required_main:
    assert marker in main, f"missing backend guard: {marker}"
for marker in required_mqtt:
    assert marker in mqtt, f"missing native start field: {marker}"
for marker in required_web:
    assert marker in web, f"missing dashboard feature: {marker}"

print("PASS: G-code storage, Canvas popup, safety guards and native method 1020 markers are present.")
