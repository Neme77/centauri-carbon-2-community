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
                    assert json.load(response) == {"language": "", "theme": "dark", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"], "camera_persistent": False}
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise AssertionError("preferences endpoint did not start")

        # Saving only a theme must not invent a language choice.
        request = urllib.request.Request(
            endpoint, data=b'{"theme":"dark"}', method="PUT",
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1):
            pass
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response)["language"] == ""

        request = urllib.request.Request(
            endpoint, data=b'{"language":"it"}', method="PUT",
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        assert preferences.read_text(encoding="ascii") == '{"language":"it","theme":"dark","quick1":"home:ALL","quick2":"system:heaters_off","quick3":"system:fans_off","quick4":"system:motors_off","camera_persistent":false}\n'
        assert preferences.stat().st_mode & 0o777 == 0o600
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "it", "theme": "dark", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"], "camera_persistent": False}

        request = urllib.request.Request(
            endpoint, data=b'{"language":"it","theme":"light"}', method="PUT",
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        assert preferences.read_text(encoding="ascii") == '{"language":"it","theme":"light","quick1":"home:ALL","quick2":"system:heaters_off","quick3":"system:fans_off","quick4":"system:motors_off","camera_persistent":false}\n'
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "it", "theme": "light", "quick_actions": ["home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"], "camera_persistent": False}

        request = urllib.request.Request(
            endpoint, data=b'{"quick1":"page:files","quick2":"light:toggle","quick3":"home:Z","quick4":"page:canvas"}', method="PUT",
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response)["quick_actions"] == ["page:files", "light:toggle", "home:Z", "page:canvas"]

        invalid_quick = urllib.request.Request(
            endpoint, data=b'{"quick1":"console:arbitrary"}', method="PUT",
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
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
            headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response) == {"language": "fr", "theme": "light", "quick_actions": ["page:files", "light:toggle", "home:Z", "page:canvas"], "camera_persistent": False}

        # Themes are identifiers too (the frontend owns the palettes): lowercase words joined by hyphens.
        for theme in ("dracula", "solarized-light"):
            request = urllib.request.Request(
                endpoint, data=json.dumps({"theme": theme}).encode(), method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=1) as response:
                assert json.load(response) == {"saved": True}
            with urllib.request.urlopen(endpoint, timeout=1) as response:
                stored = json.load(response)
            assert stored["theme"] == theme and stored["language"] == "fr", stored
        for malformed_theme in (b'{"theme":"Dracula"}', b'{"theme":"../x"}', b'{"theme":""}', b'{"theme":"a"}',
                                b'{"theme":"-nord"}', b'{"theme":"nord-"}', b'{"theme":"a--b"}',
                                b'{"theme":"abcdefghijklmnopqrstuvwxyz"}', b'{"theme":"nord2"}'):
            invalid = urllib.request.Request(
                endpoint, data=malformed_theme, method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            try:
                urllib.request.urlopen(invalid, timeout=1)
                raise AssertionError(f"malformed theme accepted: {malformed_theme!r}")
            except urllib.error.HTTPError as error:
                assert error.code == 400
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response)["theme"] == "solarized-light"  # rejected values change nothing

        for malformed in (b'{"language":"../x"}', b'{"language":""}',
                          b'{"language":"toolongcode"}', b'{"language":"e1"}'):
            invalid = urllib.request.Request(
                endpoint, data=malformed, method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            try:
                urllib.request.urlopen(invalid, timeout=1)
                raise AssertionError(f"malformed language accepted: {malformed!r}")
            except urllib.error.HTTPError as error:
                assert error.code == 400
        shortcuts = ["calibration:shaper", "calibration:hotend", "calibration:bed", "page:control"]
        update = urllib.request.Request(endpoint, data=json.dumps({f"quick{i+1}": action for i, action in enumerate(shortcuts)}).encode(), method="PUT", headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        with urllib.request.urlopen(update, timeout=1) as response:
            assert json.load(response) == {"saved": True}
        with urllib.request.urlopen(endpoint, timeout=1) as response:
            assert json.load(response)["quick_actions"] == shortcuts
        assert all(action in preferences.read_text() for action in shortcuts)
        # The new shipped languages persist on the printer like existing ones.
        for language in ("pl", "cs"):
            update = urllib.request.Request(endpoint, data=json.dumps({"language": language}).encode(), method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            with urllib.request.urlopen(update, timeout=1) as response:
                assert json.load(response) == {"saved": True}
            with urllib.request.urlopen(endpoint, timeout=1) as response:
                assert json.load(response)["language"] == language
            assert json.loads(preferences.read_text(encoding="ascii"))["language"] == language
        # Persist the camera option while preserving other preferences.
        before = json.loads(preferences.read_text())
        for enabled in (True, False, True):
            update = urllib.request.Request(endpoint, data=json.dumps({"camera_persistent": enabled}).encode(), method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            with urllib.request.urlopen(update, timeout=1): pass
            with urllib.request.urlopen(endpoint, timeout=1) as response:
                assert json.load(response)["camera_persistent"] is enabled
            saved = json.loads(preferences.read_text())
            assert all(saved[k] == v for k, v in before.items() if k != "camera_persistent")
        for invalid in ('"true"', '1', 'null', 'truex'):
            update = urllib.request.Request(endpoint, data=('{"camera_persistent":' + invalid + '}').encode(), method="PUT",
                headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
            try:
                urllib.request.urlopen(update, timeout=1)
                raise AssertionError("invalid camera option accepted")
            except urllib.error.HTTPError as error:
                assert error.code == 400
        process.terminate(); process.wait(timeout=3)
        process = subprocess.Popen([str(binary), "--port", str(port), "--panda-port", "0",
            "--web-root", str(root), "--config", str(root / "missing.conf"), "--preferences", str(preferences)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(30):
            try:
                with urllib.request.urlopen(endpoint, timeout=1) as response:
                    assert json.load(response)["camera_persistent"] is True
                break
            except OSError: time.sleep(0.1)
        else: raise AssertionError("camera preference not restored after restart")
        print("PASS: persistent UI language, theme and camera API")
    finally:
        process.terminate()
        process.wait(timeout=3)
