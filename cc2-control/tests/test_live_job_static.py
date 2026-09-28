from pathlib import Path

from _websrc import read_src

root = Path(__file__).resolve().parents[1]
ui = read_src()
control = (root / "src" / "control.c").read_text(encoding="utf-8")
main = (root / "src" / "main.c").read_text(encoding="utf-8")
mqtt = (root / "src" / "mqtt.c").read_text(encoding="utf-8")

markers = {
    "single Bed Levelling navigation": "['bed', Grid3x3, 'Bed Levelling']",
    "global emergency stop": "control('system:emergency_stop')",
    "one-second emergency hold": "}, 1000)",
    "quick machine actions": "'system:heaters_off'",
    "settings panel routing": "[Link, 'Connection'], [Plug, 'Integrations'], [Palette, 'Appearance'], [Info, 'About']",
    "persistent light theme": "setTheme(p.theme)",
    "light theme stylesheet": ':root[data-theme="light"]',
    "micron display": "Math.round(v * 1000)",
    "optimized reference adjustment": "useOptimized",
    "live job layer stats": "[v.active ? v.layer || '—' : '—', t('Current layer')]",
    "live job elapsed time": "elapsedText: active ? duration(elapsed)",
    "estimated completion time": "finishTime(remaining)",
    "real G-code metadata": "/api/gcode-files/metadata",
    "real current print object": "m?.current_object",
    "live thermal history": "a.length > 300",
}
for label, marker in markers.items():
    assert marker in ui, f"missing {label}: {marker}"

assert "data-screws" not in ui, "duplicate screw navigation remains"
assert 'strcmp(action,"system:heaters_off")' in control
assert 'strcmp(action,"system:fans_off")' in control
assert 'strcmp(action,"system:emergency_stop")' in control
assert 'active_gcode_total_layers' in main
assert '\\\"total_layers\\\":%d' in main
assert 'number_in(o,n,"total_layer_count"' in mqtt
assert 'number_in(o,n,"elapsed_time"' in mqtt
assert 'number_in(o,n,"remaining_time"' in mqtt
assert 'gcode_metadata_response' in main
assert '"filament used [g]"' in main
assert 'first_run_restart_requested = 1' in main
assert '\\"restarting\\":true' in main
assert 'Restarting CC2 Control and synchronizing Canvas' in ui
assert 'n < 60' in ui
assert "for(const [temp,color]of [[28" not in ui
assert "Read during inspection" not in ui
print("PASS: CC2 Control 1.1.31 stable live-job and safety markers")
