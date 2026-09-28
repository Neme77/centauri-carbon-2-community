#!/usr/bin/env python3
"""Runtime test for printer-persistent UI language preferences."""

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
with tempfile.TemporaryDirectory(prefix="cc2-preferences-") as temporary:
    root = Path(temporary)
    preferences = root / "ui-preferences.json"
    port = free_port()
    process = subprocess.Popen([
        str(binary), "--port", str(port), "--panda-port", "0",
        "--web-root", str(root), "--config", str(root / "missing.conf"),
        "--presets", str(root / "presets.json"),
        "--preferences", str(preferences),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        endpoint = f"http://127.0.0.1:{port}/api/preferences"
        for _ in range(30):
            try:
                with urllib.request.urlopen(endpoint, timeout=1) as response:
                    assert json.load(response) == {"language": "en", "theme": "dark", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"]}
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("preferences endpoint did not start")

        request = urllib.request.Request(
            endpoint, data=b'{"language":"it"}', method="PUT",
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        assert preferences.read_text(encoding="ascii") == '{"language":"it","theme":"dark","quick1":"home:ALL","quick2":"system:heaters_off","quick3":"system:fans_off","quick4":"system:motors_off"}\n'
        assert preferences.stat().st_mode & 0o777 == 0o600
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "it", "theme": "dark", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"]}

        request = urllib.request.Request(
            endpoint, data=b'{"language":"it","theme":"light"}', method="PUT",
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        assert preferences.read_text(encoding="ascii") == '{"language":"it","theme":"light","quick1":"home:ALL","quick2":"system:heaters_off","quick3":"system:fans_off","quick4":"system:motors_off"}\n'
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "it", "theme": "light", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"]}

        request = urllib.request.Request(
            endpoint, data=b'{"quick1":"page:files","quick2":"light:toggle","quick3":"home:Z","quick4":"page:canvas"}', method="PUT",
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response)["quick_actions"] == ["page:files", "light:toggle", "home:Z", "page:canvas"]

        invalid_quick = urllib.request.Request(
            endpoint, data=b'{"quick1":"console:arbitrary"}', method="PUT",
            headers={"Content-Type": "application/json"})
        try:
            urllib.request.urlopen(invalid_quick, timeout=1)
            raise AssertionError("unsupported quick action accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 400

        # Any locale code shaped like web/locales/<code>.json is accepted:
        # the backend only stores it, the frontend falls back to English for
        # one it has no translation file for. "fr" is a real, shipped locale.
        request = urllib.request.Request(
            endpoint, data=b'{"language":"fr"}', method="PUT",
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "fr", "theme": "light", "quick_actions": ["page:files", "light:toggle", "home:Z", "page:canvas"]}

        for malformed in (b'{"language":"../x"}', b'{"language":""}',
                          b'{"language":"toolongcode"}', b'{"language":"e1"}'):
            invalid = urllib.request.Request(
                endpoint, data=malformed, method="PUT",
                headers={"Content-Type": "application/json"})
            try:
                urllib.request.urlopen(invalid, timeout=1)
                raise AssertionError(f"malformed language accepted: {malformed!r}")
            except urllib.error.HTTPError as error:
                assert error.code == 400
        print("PASS: persistent UI language and theme API")
    finally:
        process.terminate()
        process.wait(timeout=3)
