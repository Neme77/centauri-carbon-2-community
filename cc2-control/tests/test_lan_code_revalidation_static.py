#!/usr/bin/env python3
"""Verify the explicit, restart-safe LAN access-code rotation flow."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src" / "main.c").read_text(encoding="utf-8")
ui = (root / "web" / "index.html").read_text(encoding="utf-8")

assert 'strcmp(path,"/api/setup/revalidate")==0' in main
assert 'setup_configure_response(fd,mqtt,body,body_len,1)' in main
assert 'if (!setup_mode && !allow_revalidation)' in main
assert 'fsync(fileno(file))' in main
assert 'rename(temporary, mqtt_config_path)' in main
assert 'first_run_restart_requested = 1' in main
assert "revalidate=Boolean(status.configured)" in ui
assert "'/api/setup/revalidate'" in ui
assert "Change / Revalidate" in ui
assert "Revalidation required" in ui
assert "verifyButton.disabled=false" in ui

print("PASS: LAN access-code rotation is explicit, atomic and restart-safe.")
