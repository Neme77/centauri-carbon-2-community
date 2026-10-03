#!/usr/bin/env python3
"""Ensure both persistent-hotfix and firmware launchers expose Panda port 7125."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
hotfix = (root / "scripts" / "start.sh").read_text(encoding="utf-8")
firmware = (root / "scripts" / "start-firmware.sh").read_text(encoding="utf-8")

assert '--panda-port 7125' in hotfix, "persistent launcher does not expose Panda port 7125"
assert '--panda-port 7125' in firmware, "firmware launcher does not expose Panda port 7125"
assert '--config "$BASE/cc2-control.conf"' in hotfix
assert '--config "$PERSIST/cc2-control.conf"' in firmware
assert '--preferences "$BASE/ui-preferences.json"' in hotfix
assert '--preferences "$PERSIST/ui-preferences.json"' in firmware
mount_guard = "while ! grep -q ' /opt/usr ' /proc/mounts; do"
assert mount_guard in firmware, "firmware launcher does not wait for the persistent UDISK mount"
assert firmware.index(mount_guard) < firmware.index('mkdir -p "$PERSIST"'), (
    "persistent directory must be created only after /opt/usr is mounted"
)
assert 'chmod 755 "$PERSIST"' in firmware
assert 'chmod 644 "$PERSIST/material-presets.json"' in firmware

print("PASS: launchers preserve Panda compatibility and firmware waits for persistent storage.")

# Both deployed startup paths must wait after the vendor process appears.
repo = root.parent
for relative in (
    "cc2-control/scripts/launch.sh",
    "cc2-control/scripts/start-firmware.sh",
    "builder/current/components/cc2-control/runtime/start.sh",
    "builder/current/components/cc2-control/runtime/launch.sh",
):
    launcher = (repo / relative).read_text(encoding="utf-8")
    guard = "while ! pidof elegoo_printer >/dev/null 2>&1; do"
    assert guard in launcher, relative
    assert "sleep 60" in launcher, relative
    assert launcher.index(guard) < launcher.index("sleep 60") < launcher.index("exec "), relative
