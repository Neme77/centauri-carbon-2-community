#!/usr/bin/env python3
"""Runtime test for the versioned appearance document (themes, layouts, profiles)."""
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


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def get(url):
    with urllib.request.urlopen(url, timeout=2) as response:
        return response.status, json.load(response)


def put(url, value):
    request = urllib.request.Request(
        url, data=json.dumps(value).encode(), method="PUT",
        headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=2) as response:
        return response.status, json.load(response)


def post(url, value):
    request = urllib.request.Request(
        url, data=json.dumps(value).encode(), method="POST",
        headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=2) as response:
        return response.status, json.load(response)


def status_of(url, value, method="PUT"):
    request = urllib.request.Request(
        url, data=json.dumps(value).encode(), method=method,
        headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=2) as response:
            return response.status
    except urllib.error.HTTPError as error:
        return error.code


binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cc2-appearance-") as temporary:
    root = Path(temporary)
    preferences = root / "ui-preferences.json"
    preferences.write_text('{"language":"it","theme":"light"}\n')
    endpoint = None
    port = free_port()

    def start():
        return subprocess.Popen([
            str(binary), "--port", str(port), "--panda-port", "0",
            "--web-root", str(root), "--config", str(root / "missing.conf"),
            "--presets", str(root / "presets.json"), "--preferences", str(preferences),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def wait():
        for _ in range(40):
            try:
                return get(endpoint)
            except OSError:
                time.sleep(0.1)
        raise AssertionError("appearance endpoint did not start")

    process = start()
    endpoint = f"http://127.0.0.1:{port}/api/appearance"
    try:
        # First run inherits the legacy theme; the editable shape is complete.
        code, doc = wait()
        assert code == 200 and doc["version"] == 1
        assert doc["dashboard"]["theme"] == "light", doc
        assert doc["dashboard"]["layout"] == "classic"
        assert {layout["id"] for layout in doc["dashboard"]["layouts"]} >= {"classic", "custom-grid"}
        assert doc["profiles"][0]["id"] == "community"
        assert doc["recovered"] is False

        custom = {
            "bg": "#101010", "panel": "#181818", "edge": "#303030", "text": "#ffffff",
            "muted": "#aaaaaa", "field": "#202020", "ink": "#000000", "cyan": "#ff8800",
            "green": "#00ff00", "amber": "#ffcc00", "red": "#ff0000", "blue": "#0088ff",
            "well": "#050505", "wellFg": "#ffffff",
        }
        layouts = [
            {"id": "classic", "name": "Classic", "columns": 4, "widgets": []},
            {"id": "custom-grid", "name": "Mine", "columns": 4, "widgets": [
                {"id": "camera", "enabled": True, "span": 2},
                {"id": "quick", "enabled": False, "span": 1, "accent": "#ff8800"},
            ]},
        ]
        code, saved = put(endpoint, {
            "dashboard": {"theme": "custom-test", "mode": "dark", "layout": "custom-grid",
                          "density": 1, "font": 2, "scale": 1.1, "radius": 4, "spacing": 2,
                          "animations": False, "layouts": layouts},
            "touchscreen": {"enabled": True, "linkAccent": False, "experimental": True,
                            "accent": "#ff8800", "background": "#0a0c11", "boot": "custom"},
            "themes": [{"id": "custom-test", "name": "Test", "tokens": custom}],
        })
        assert saved == {"saved": True}, saved
        code, doc = get(endpoint)
        assert doc["dashboard"]["theme"] == "custom-test"
        assert doc["dashboard"]["density"] == 1 and doc["dashboard"]["font"] == 2
        assert abs(doc["dashboard"]["scale"] - 1.1) < 1e-9
        assert doc["dashboard"]["layouts"][1]["widgets"][1]["enabled"] is False
        assert doc["dashboard"]["layouts"][1]["widgets"][1]["accent"] == "#ff8800"
        assert doc["touchscreen"]["boot"] == "custom" and doc["themes"][0]["tokens"]["cyan"] == "#ff8800"

        stored = root / "appearance.json"
        assert stored.stat().st_mode & 0o777 == 0o600
        assert json.loads(stored.read_text())["dashboard"]["theme"] == "custom-test"

        # Unknown fields are ignored, and a second accepted save keeps one backup.
        code, _ = put(endpoint, {"dashboard": {"theme": "nord", "unknown": 1}, "unknown": 2})
        assert code == 200
        code, doc = get(endpoint)
        assert doc["dashboard"]["theme"] == "nord"
        assert (root / "appearance.json.bak").is_file()
        assert "unknown" not in (root / "appearance.json").read_text()

        # Invalid values are refused without changing the stored document.
        bad_values = [
            {"dashboard": {"theme": "Bad Theme"}},
            {"dashboard": {"theme": "nord2"}},
            {"dashboard": {"mode": "sepia"}},
            {"dashboard": {"layout": "../x"}},
            {"dashboard": {"density": 9}},
            {"dashboard": {"scale": 2}},
            {"dashboard": {"layouts": [{"id": "x", "name": "X", "columns": 3, "widgets": []}]}},
            {"dashboard": {"layouts": [{"id": "x", "name": "X", "columns": 4,
                                        "widgets": [{"id": "unknown", "span": 2}]}]}},
            {"dashboard": {"layouts": [{"id": "x", "name": "X", "columns": 4,
                                        "widgets": [{"id": "camera", "span": 3}]}]}},
            {"touchscreen": {"accent": "orange"}},
            {"touchscreen": {"boot": "stock"}},
            {"themes": [{"id": "custom-a", "name": "A", "tokens": {**custom, "bg": "red"}}]},
            {"themes": [{"id": "custom-a", "name": "A", "tokens": custom},
                        {"id": "custom-a", "name": "B", "tokens": custom}]},
        ]
        for value in bad_values:
            assert status_of(endpoint, value) == 400, value
        code, doc = get(endpoint)
        assert doc["dashboard"]["theme"] == "nord"

        # Profiles: create, list, apply, refuse deleting the built-in one, delete.
        profile = {
            "id": "profile-night", "name": "Night",
            "dashboard": {"theme": "custom-test", "mode": "dark", "layout": "custom-grid",
                          "layouts": layouts},
            "touchscreen": {"enabled": True, "accent": "#b28aff", "background": "#0a0c11"},
        }
        code, _ = post(endpoint + "/profiles", {"profile": profile,
                                                "theme": {"id": "custom-test", "name": "Test", "tokens": custom}})
        assert code == 200
        code, doc = get(endpoint)
        assert any(item["id"] == "profile-night" for item in doc["profiles"])
        code, _ = post(endpoint + "/profiles/apply", {"id": "profile-night"})
        assert code == 200
        code, doc = get(endpoint)
        assert doc["activeProfile"] == "profile-night"
        assert doc["touchscreen"]["accent"] == "#b28aff"
        assert status_of(endpoint + "/profiles/delete", {"id": "community"}, "POST") == 400
        assert status_of(endpoint + "/profiles/delete", {"id": "missing"}, "POST") == 404
        code, _ = post(endpoint + "/profiles/delete", {"id": "profile-night"})
        assert code == 200
        code, doc = get(endpoint)
        assert doc["activeProfile"] == "community"

        # The interaction guard is the same as every other browser mutation.
        unguarded = urllib.request.Request(endpoint, data=b'{"dashboard":{"theme":"dark"}}', method="PUT")
        try:
            urllib.request.urlopen(unguarded, timeout=2)
            raise AssertionError("request without X-CC2-Request accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 403

        # A body above the bounded parser is answered, not truncated.
        big = json.dumps({"dashboard": {"theme": "dark"}, "pad": "x" * 20000}).encode()
        big_request = urllib.request.Request(endpoint, data=big, method="PUT",
                                             headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
        try:
            urllib.request.urlopen(big_request, timeout=2)
            raise AssertionError("oversized appearance document accepted")
        except urllib.error.HTTPError as error:
            assert error.code in (413, 400), error.code

        # Reset keeps the custom theme unless profiles=True.
        code, _ = post(endpoint + "/reset", {"profiles": False})
        assert code == 200
        code, doc = get(endpoint)
        assert doc["dashboard"]["theme"] == "dark"
        assert doc["dashboard"]["layout"] == "classic"
        assert doc["themes"], "reset without profiles must keep custom themes"
        code, _ = post(endpoint + "/reset", {"profiles": True})
        code, doc = get(endpoint)
        assert doc["themes"] == [] and len(doc["profiles"]) == 1

        # Persistence across a restart.
        process.terminate(); process.wait(timeout=3)
        process = start()
        code, doc = wait()
        assert doc["dashboard"]["theme"] == "dark"
        assert doc["dashboard"]["layout"] == "classic"

        # A damaged file is reported and defaults are served; no silent rewrite.
        process.terminate(); process.wait(timeout=3)
        stored.write_text("{not json")
        process = start()
        code, doc = wait()
        assert doc["recovered"] is True and doc["error"]
        assert doc["dashboard"]["theme"] == "dark"
        assert stored.read_text() == "{not json"

        print("PASS: appearance API validation, persistence, profiles, reset and damage recovery")
    finally:
        process.terminate()
        process.wait(timeout=3)
