#!/usr/bin/env python3
"""Real display manifest and state robustness.

The firmware builder generates a 154-asset manifest. This test feeds that exact
manifest to the production parser and proves that:
  - capability is detected (the defect this test exists for reported
    display_manifest_invalid for the shipped manifest);
  - every asset is listed;
  - a missing, unreadable, corrupt or oversized state file never crashes the
    daemon and is reported through stateValid instead.
"""
import hashlib
import importlib.util
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
binary = Path(sys.argv[1]).resolve()


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def load_builder():
    spec = importlib.util.spec_from_file_location(
        "native_display_component", ROOT / "builder/current/core/native_display_component.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Server:
    def __init__(self, root, theme, gui):
        self.port = free_port()
        self.process = subprocess.Popen([
            str(binary), "--port", str(self.port), "--panda-port", "0",
            "--web-root", str(root), "--config", str(root / "missing.conf"),
            "--presets", str(root / "presets.json"), "--preferences", str(root / "ui-preferences.json"),
            "--display-dir", str(theme), "--display-gui", str(gui),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @property
    def endpoint(self):
        return f"http://127.0.0.1:{self.port}/api/display"

    def wait(self):
        for _ in range(80):
            if self.process.poll() is not None:
                raise AssertionError(f"server exited with {self.process.returncode}")
            try:
                code, body = get(self.endpoint)
                if code == 200 and isinstance(body, dict):
                    return body
            except OSError:
                pass
            time.sleep(0.1)
        raise AssertionError("display endpoint did not start")

    def stop(self):
        self.process.terminate()
        self.process.wait(timeout=5)


builder = load_builder()
with tempfile.TemporaryDirectory(prefix="cc2-display-capacity-") as temporary:
    root = Path(temporary)
    theme = root / "theme"
    theme.mkdir()
    gui = root / "gui"
    gui.write_bytes(b"synthetic-native-gui")
    gui_hash = hashlib.sha256(gui.read_bytes()).hexdigest()
    manifest = builder.display_manifest(gui_sha256=gui_hash)
    assert len(manifest["assets"]) == 154, len(manifest["assets"])
    (theme / "cc2-display-manifest.json").write_text(
        json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n")
    for asset in manifest["assets"]:
        (theme / asset["name"]).write_bytes(b"placeholder")
    state_dir = root / "display-theme"
    state_dir.mkdir()

    server = Server(root, theme, gui)
    try:
        status = server.wait()
        assert status["supported"] is True, status["error"]
        assert status["error"] == "", status["error"]
        assert status["fileCount"] == 154, status["fileCount"]
        assert len(status["assets"]) == 154
        assert {asset["role"] for asset in status["assets"]} == {"boot", "background", "accent"}
        boot = [asset for asset in status["assets"] if asset["role"] == "boot"]
        assert len(boot) == 100
        assert boot[0]["width"] == 532 and boot[0]["height"] == 300
        assert status["stateValid"] is True
        assert status["restoreAvailable"] is False  # no baseline yet

        # A missing state file: defaults, no crash.
        assert get(server.endpoint)[0] == 200
    finally:
        server.stop()

    # Each bad state file is presented to a *fresh* process, as after a reboot.
    def status_with_state(label, setup):
        if state_file.exists():
            os.chmod(state_file, 0o600)
            state_file.unlink()
        setup()
        fresh = Server(root, theme, gui)
        try:
            body = fresh.wait()
            assert fresh.process.poll() is None, f"daemon died: {label}"
            return body
        finally:
            fresh.stop()

    state_file = state_dir / "state.json"
    body = status_with_state("unreadable", lambda: state_file.write_text('{"phase":"applied","files":[]}'))
    if os.geteuid() != 0:
        body = status_with_state("unreadable", lambda: (state_file.write_text('{"phase":"applied","files":[]}'),
                                                        os.chmod(state_file, 0)))
        assert body["stateValid"] is False, body
    body = status_with_state("corrupt", lambda: state_file.write_text("{not json"))
    assert body["stateValid"] is False, body
    body = status_with_state(
        "oversized",
        lambda: state_file.write_text(json.dumps({"phase": "applied", "files": [], "pad": "x" * (200 * 1024)})))
    assert body["stateValid"] is False, body

    # A valid state with 154 applied entries is read in full.
    files = [{"name": f"asset{i:03d}.png", "sha256": hashlib.sha256(str(i).encode()).hexdigest()}
             for i in range(154)]
    body = status_with_state(
        "154 entries",
        lambda: state_file.write_text(json.dumps({"version": 1, "phase": "applied", "bootId": "b",
                                                  "guiMarker": "m", "appliedAt": 1, "files": files})))
    assert body["stateValid"] is True
    assert body["phase"] == 1
    assert body["customizedCount"] == 0  # none of those names is an applied asset
    os.chmod(state_file, 0o600)

    print(f"PASS: real {len(manifest['assets'])}-asset manifest is supported; "
          "missing, unreadable, corrupt, oversized and 154-entry state files are handled without a crash")
