#!/usr/bin/env python3
"""Runtime test for the single live-view viewer: POST /api/camera/claim and /api/printer camera_viewer."""

import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cc2-camera-viewer-") as temporary:
    root = Path(temporary)
    port = free_port()
    process = subprocess.Popen([
        str(binary), "--port", str(port), "--panda-port", "0",
        "--web-root", str(root), "--config", str(root / "missing.conf"),
        "--presets", str(root / "presets.json"),
        "--preferences", str(root / "ui-preferences.json"),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        base = f"http://127.0.0.1:{port}"

        def viewer():
            with urllib.request.urlopen(base + "/api/printer", timeout=1) as response:
                return json.load(response)["camera_viewer"]

        def claim(body, marker=True):
            headers = {"Content-Type": "text/plain;charset=UTF-8"}
            if marker:
                headers["X-CC2-Request"] = "1"
            request = urllib.request.Request(base + "/api/camera/claim", data=body, method="POST", headers=headers)
            try:
                with urllib.request.urlopen(request, timeout=1) as response:
                    return response.status, json.load(response)
            except urllib.error.HTTPError as error:
                return error.code, None

        for _ in range(30):
            try:
                assert viewer() is None, "no viewer before the first claim"
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("printer endpoint did not start")

        first = "0123456789abcdef0123456789abcdef"
        assert claim(first.encode()) == (200, {"viewer": first})
        assert viewer() == first

        # The newest claim wins; a trailing newline is tolerated.
        second = "fedcba98-7654-3210"
        assert claim(second.encode() + b"\n") == (200, {"viewer": second})
        assert viewer() == second

        # Identifiers are echoed into JSON unescaped, so only a safe, bounded charset is accepted.
        for invalid in (b"", b"short", b"UPPERCASE0123", b'quote"0123456', b"space 0123456", b"a" * 41,
                        "кириллица01".encode()):
            assert claim(invalid)[0] == 400, invalid
        assert viewer() == second, "a rejected claim must not change the viewer"

        # Like every other command, a claim needs the CC2 request marker.
        assert claim(b"0123456789abcdef", marker=False)[0] == 403
        assert viewer() == second

        print("PASS: camera viewer claim, newest-wins replacement, strict identifiers and request marker")
    finally:
        process.terminate()
        process.wait(timeout=3)
