#!/usr/bin/env python3
"""Maximum appearance document: save, restart, reload, compare.

The module advertises six profiles, four layouts of nine widgets and eight
custom themes. This test builds that configuration through the HTTP API, checks
the stored file stays inside the documented size, restarts the server and
verifies that every value survived - the defect this test exists for was a
document that saved successfully and could not be read back.
"""
import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
TOKENS = {
    "bg": "#101010", "panel": "#181818", "edge": "#303030", "text": "#ffffff",
    "muted": "#aaaaaa", "field": "#202020", "ink": "#000000", "cyan": "#ff8800",
    "green": "#00ff00", "amber": "#ffcc00", "red": "#ff0000", "blue": "#0088ff",
    "well": "#050505", "wellFg": "#ffffff",
}
WIDGETS = ["camera", "quick", "job", "tuning", "thermal", "fans", "firstLayer", "position", "system"]
LONG_NAME = "A very long profile and layout name that reaches the documented maximum of sixty-four characters"


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def call(url, value=None, method="GET"):
    data = None if value is None else json.dumps(value).encode()
    request = urllib.request.Request(
        url, data=data, method=method,
        headers={"X-CC2-Request": "1", "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def maximum_layouts(prefix):
    layouts = [{"id": "classic", "name": "Classic", "columns": 4, "widgets": []}]
    for index in range(3):
        layouts.append({
            "id": f"grid-{prefix}{index}",
            "name": LONG_NAME[:64],
            "columns": 4,
            "widgets": [{
                "id": widget,
                "enabled": index != 0,
                "span": 4 if widget == "camera" else (1 if widget in ("fans", "firstLayer") else 2),
                "compact": widget == "system",
                "accent": "#ff8800" if widget == "camera" else "",
            } for widget in WIDGETS],
        })
    return layouts


def maximum_dashboard(prefix):
    return {
        "theme": f"custom-theme-{prefix}",
        "mode": "dark",
        "layout": f"grid-{prefix}0",
        "density": 2,
        "font": 2,
        "scale": 1.3,
        "radius": 24,
        "spacing": 2,
        "animations": False,
        "layouts": maximum_layouts(prefix),
    }


def maximum_themes():
    # "custom-" + 10 characters = 16, inside the 24-character legacy contract.
    return [{
        "id": f"custom-theme-{chr(97 + index)}",
        "name": f"Theme {index} " + "x" * 50,
        "tokens": {**TOKENS, "cyan": "#%06x" % (0x101010 + index * 0x1010)},
    } for index in range(8)]


with tempfile.TemporaryDirectory(prefix="cc2-appearance-capacity-") as temporary:
    root = Path(temporary)
    preferences = root / "ui-preferences.json"
    preferences.write_text('{"language":"en","theme":"light"}\n')
    port = free_port()
    endpoint = f"http://127.0.0.1:{port}/api/appearance"

    def start():
        return subprocess.Popen([
            str(binary), "--port", str(port), "--panda-port", "0",
            "--web-root", str(root), "--config", str(root / "missing.conf"),
            "--presets", str(root / "presets.json"), "--preferences", str(preferences),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def wait(process):
        for _ in range(80):
            try:
                code, doc = call(endpoint)
                if code == 200:
                    return doc
            except OSError:
                pass
            time.sleep(0.1)
        raise AssertionError("appearance endpoint did not start")

    process = start()
    try:
        wait(process)
        themes = maximum_themes()
        dashboard = maximum_dashboard("a")
        profiles = []
        for index in range(5):
            prefix = chr(98 + index)  # b..f
            profiles.append({
                "id": f"profile-{prefix}",
                "name": ("Profile " + prefix.upper() + " " + "y" * 50)[:64],
                "dashboard": maximum_dashboard(prefix),
                "touchscreen": {
                    "enabled": index % 2 == 0, "linkAccent": index % 2 == 1,
                    "experimental": True, "accent": "#7eead8", "background": "#0a0c11",
                    "boot": "custom" if index == 0 else "community",
                },
            })

        # The studio's own payload: dashboard + themes, no profiles.
        code, reply = call(endpoint, {
            "activeProfile": "profile-b",
            "dashboard": dashboard,
            "touchscreen": {"enabled": True, "linkAccent": False, "experimental": True,
                            "accent": "#ff7ab6", "background": "#121a12", "boot": "custom"},
            "themes": themes,
        }, "PUT")
        assert code == 200, reply
        for profile in profiles:
            theme = next(item for item in themes if item["id"] == profile["dashboard"]["theme"])
            code, reply = call(endpoint + "/profiles", {"profile": profile, "theme": theme}, "POST")
            assert code == 200, (profile["id"], reply)
        # The active profile must exist when it is named: apply it after creation,
        # as the studio does.
        code, reply = call(endpoint + "/profiles/apply", {"id": "profile-b"}, "POST")
        assert code == 200, reply

        live = call(endpoint)[1]
        assert len(live["profiles"]) == 6, len(live["profiles"])
        assert len(live["themes"]) == 8, len(live["themes"])
        assert len(live["dashboard"]["layouts"]) == 4, len(live["dashboard"]["layouts"])
        assert live["activeProfile"] == "profile-b"
        stored = root / "appearance.json"
        size = stored.stat().st_size
        assert size < 32768, f"stored document {size} bytes is above AP_MAX_JSON"

        # Destroy the in-memory state: a complete process restart.
        process.terminate()
        process.wait(timeout=5)
        process = start()
        after = wait(process)
        assert after["recovered"] is False, after.get("error")
        assert after["unsupported"] is False
        assert after["activeProfile"] == "profile-b", after["activeProfile"]
        assert len(after["profiles"]) == 6, [p["id"] for p in after["profiles"]]
        assert len(after["themes"]) == 8, len(after["themes"])
        assert len(after["dashboard"]["layouts"]) == 4, len(after["dashboard"]["layouts"])
        assert after["dashboard"]["density"] == 2 and after["dashboard"]["font"] == 2
        assert after["dashboard"]["scale"] == 1.3 and after["dashboard"]["radius"] == 24
        assert after["dashboard"]["animations"] is False
        # The active appearance is the applied profile's snapshot, not the PUT
        # that created the same document before the profiles existed.
        assert after["touchscreen"]["enabled"] is True
        assert after["touchscreen"]["linkAccent"] is False
        assert after["touchscreen"]["accent"] == "#7eead8"
        assert after["touchscreen"]["background"] == "#0a0c11"
        assert after["touchscreen"]["boot"] == "custom"

        # Every saved profile keeps its own four layouts and nine widgets; the
        # built-in community profile keeps the default two.
        for profile in after["profiles"]:
            if profile["id"] == "community":
                assert len(profile["dashboard"]["layouts"]) == 2
                continue
            layouts = profile["dashboard"]["layouts"]
            assert len(layouts) == 4, (profile["id"], len(layouts))
            custom = [item for item in layouts if item["id"].startswith("grid-")]
            assert len(custom) == 3
            for layout in custom:
                assert len(layout["widgets"]) == 9, (profile["id"], layout["id"])
                assert [w["id"] for w in layout["widgets"]] == WIDGETS
                camera = layout["widgets"][0]
                assert camera["span"] == 4 and camera["accent"] == "#ff8800"
                assert layout["widgets"][5]["span"] == 1

        # Spot-check the colour values survived the serializer/parser.
        by_id = {theme["id"]: theme for theme in after["themes"]}
        assert by_id["custom-theme-a"]["tokens"]["cyan"] == "#101010"
        assert by_id["custom-theme-h"]["tokens"]["cyan"] == "#%06x" % (0x101010 + 7 * 0x1010)

        # A theme deletion is explicit and survives a restart.
        code, reply = call(endpoint + "/themes/delete", {"id": "custom-theme-h"}, "POST")
        assert code == 200, reply
        assert call(endpoint)[1]["activeProfile"] == "profile-b"
        process.terminate()
        process.wait(timeout=5)
        process = start()
        after = wait(process)
        assert len(after["themes"]) == 7 and after["recovered"] is False
        print("PASS: maximum appearance document (6 profiles, 4 layouts, 9 widgets, 8 themes) "
              f"round-trips through disk and restart ({size} bytes)")
    finally:
        process.terminate()
        process.wait(timeout=5)
