from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "web" / "index.html").read_text(encoding="utf-8")
control = (root / "src" / "control.c").read_text(encoding="utf-8")
main = (root / "src" / "main.c").read_text(encoding="utf-8")
mqtt = (root / "src" / "mqtt.c").read_text(encoding="utf-8")

markers = {
    "single Bed Levelling navigation": 'data-page="bed"',
    "global emergency stop": 'class="global-estop"',
    "one-second emergency hold": "setTimeout(async()=>",
    "quick machine actions": 'data-quick-action="system:heaters_off"',
    "settings panel routing": "mapping=[0,3,2,1,4]",
    "compact settings layout": "Compact, tabbed Settings layout",
    "persistent light theme": "setTheme(preferences.theme,false)",
    "light theme stylesheet": 'html[data-theme="light"]',
    "light navigation sidebar": 'background:linear-gradient(180deg,#fff,#f1f3f4)',
    "accessible light warning palette": 'background:#fff4ce',
    "micron screw display": "value*1000",
    "optimized reference adjustment": "useOptimized",
    "demo job neutralization": "neutralizeJobDemo()",
    "language-independent telemetry readings": "const aliases={Nozzle:['Nozzle','Ugello']",
    "live job layer summary": "jobSummary[0].textContent=",
    "live job elapsed time": "elapsedText=activePrint?showDuration",
    "estimated completion time": "showFinishTime(remainingSeconds)",
    "neutral idle job times": "stats.forEach(node=>node.textContent='—')",
    "real G-code metadata": "/api/gcode-files/metadata",
    "real current print object": "model.current_object",
    "live UI demo neutralization": "neutralizeStaticDemo()",
    "live thermal renderer": "window.cc2DrawLiveThermal=drawLiveThermal",
}
for label, marker in markers.items():
    assert marker in ui, f"missing {label}: {marker}"

assert 'data-screws' not in ui.split('</nav>', 1)[0], "duplicate screw navigation remains"
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
assert 'for (let count = 0; count < 60; count++)' in ui
assert "for(const [temp,color]of [[28" not in ui
assert "lines[index].textContent = 'Read during inspection'" not in ui
print("PASS: CC2 Control 1.1.31 stable live-job and safety markers")
