#!/usr/bin/env python3
"""Runtime test for native-display capability detection and asset serving.

The apply path itself (tar validation, baseline, restore, recovery) is covered
by tests/test_display_theme.c; this checks the HTTP surface and fail-closed
behaviour of /api/display.
"""
import hashlib
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def png(width, height, rgba=(10, 20, 30, 255)):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    stride = width * 4
    raw = b"".join(b"\0" + bytes(rgba) * width for _ in range(height))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cc2-display-") as temporary:
    root = Path(temporary)
    theme = root / "theme"
    theme.mkdir()
    gui = root / "gui"
    gui.write_bytes(b"synthetic-native-gui")
    image = png(1, 1)
    (theme / "boot.png").write_bytes(image)
    gui_hash = hashlib.sha256(gui.read_bytes()).hexdigest()
    image_hash = hashlib.sha256(image).hexdigest()
    manifest = {
        "schema": 1, "firmwareBase": "test-base", "guiSha256": gui_hash,
        "assets": [{"name": "boot.png", "role": "boot", "width": 1, "height": 1, "sha256": image_hash}],
    }
    (theme / "cc2-display-manifest.json").write_text(json.dumps(manifest))
    port = free_port()
    process = subprocess.Popen([
        str(binary), "--port", str(port), "--panda-port", "0",
        "--web-root", str(root), "--config", str(root / "missing.conf"),
        "--presets", str(root / "presets.json"), "--preferences", str(root / "ui-preferences.json"),
        "--display-dir", str(theme), "--display-gui", str(gui),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    try:
        for _ in range(40):
            try:
                with urllib.request.urlopen(base + "/api/display", timeout=2) as response:
                    status = json.load(response)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("display endpoint did not start")
        assert status["supported"] is True, status
        assert status["firmwareBase"] == "test-base"
        assert status["fileCount"] == 1
        assert status["assets"] == [{"name": "boot.png", "role": "boot", "width": 1, "height": 1}]
        assert status["backupAvailable"] is False
        assert status["recoveryRequired"] is False
        assert status["error"] == ""

        with urllib.request.urlopen(base + "/api/display/asset?name=boot.png", timeout=2) as response:
            assert response.headers.get_content_type() == "image/png"
            assert response.read() == image
        for query in ("name=../ui-preferences.json", "name=missing.png", "name=", ""):
            try:
                urllib.request.urlopen(base + "/api/display/asset?" + query, timeout=2)
                raise AssertionError(f"asset query accepted: {query}")
            except urllib.error.HTTPError as error:
                assert error.code in (400, 404), (query, error.code)

        # Without a connected idle printer a write must be refused before any
        # file is touched or an upload slot is taken.
        request = urllib.request.Request(base + "/api/display/apply", data=b"x" * 512, method="POST",
                                         headers={"X-CC2-Request": "1", "Content-Type": "application/x-tar"})
        try:
            urllib.request.urlopen(request, timeout=2)
            raise AssertionError("display apply accepted while the printer is not idle")
        except urllib.error.HTTPError as error:
            assert error.code == 409, error.code
        request = urllib.request.Request(base + "/api/display/restore", data=b"", method="POST",
                                         headers={"X-CC2-Request": "1"})
        try:
            urllib.request.urlopen(request, timeout=2)
            raise AssertionError("display restore accepted while the printer is not idle")
        except urllib.error.HTTPError as error:
            assert error.code == 409, error.code

        # A GUI hash mismatch is a hard refusal, not a guess.
        process.terminate(); process.wait(timeout=3)
        gui.write_bytes(b"changed-native-gui")
        process = subprocess.Popen([
            str(binary), "--port", str(port), "--panda-port", "0",
            "--web-root", str(root), "--config", str(root / "missing.conf"),
            "--presets", str(root / "presets.json"), "--preferences", str(root / "ui-preferences.json"),
            "--display-dir", str(theme), "--display-gui", str(gui),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(40):
            try:
                with urllib.request.urlopen(base + "/api/display", timeout=2) as response:
                    status = json.load(response)
                break
            except OSError:
                time.sleep(0.1)
        assert status["supported"] is False
        assert status["error"] in ("display_gui_mismatch", "display_gui_unreadable"), status
        print("PASS: display capability, asset serving, traversal refusal, idle gate and GUI mismatch")
    finally:
        process.terminate()
        process.wait(timeout=3)
