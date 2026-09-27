#!/usr/bin/env python3
"""Integration test for the bounded, read-only G-code listing endpoint."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_gcode_files.py PATH_TO_CC2_CONTROL")
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cc2-gcode-test-") as temporary:
        root = Path(temporary)
        internal = root / "internal"
        usb = root / "usb"
        internal.mkdir()
        (usb / "folder").mkdir(parents=True)
        (internal / "cube.gcode").write_text("G28\n", encoding="ascii")
        (internal / "multicolour.gcode").write_text(
            "; T9 and BED_MESH_CALIBRATE FROM_SLICER=1 in comments are ignored\nT0\nG1 X1\nT1\nG1 X2\n",
            encoding="ascii",
        )
        (internal / "adaptive.gcode").write_text(
            "BED_MESH_CALIBRATE MESH_MIN=20,30 MESH_MAX=180,190 FROM_SLICER=1\nG28\n",
            encoding="ascii",
        )
        (internal / "ignored.txt").write_text("not gcode\n", encoding="ascii")
        (usb / "folder" / "part.GCODE").write_text("G1 X1\n", encoding="ascii")
        try:
            (internal / "outside.gcode").symlink_to(usb / "folder" / "part.GCODE")
        except OSError:
            pass
        port = free_port()
        process = subprocess.Popen(
            [str(binary), "--port", str(port), "--web-root", str(root),
             "--config", str(root / "missing.conf"),
             "--presets", str(root / "presets.json"),
             "--gcode-internal", str(internal), "--gcode-usb", str(usb)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            endpoint = f"http://127.0.0.1:{port}/api/gcode-files"
            for _ in range(30):
                try:
                    with urllib.request.urlopen(endpoint, timeout=1) as response:
                        payload = json.load(response)
                    break
                except Exception:
                    if process.poll() is not None:
                        stdout, stderr = process.communicate()
                        raise AssertionError(f"server stopped\n{stdout}\n{stderr}")
                    time.sleep(0.1)
            else:
                raise AssertionError("server did not expose /api/gcode-files")

            assert payload["internal"]["available"] is True
            assert payload["usb"]["available"] is True
            assert {item["path"] for item in payload["internal"]["files"]} == {
                "adaptive.gcode", "cube.gcode", "multicolour.gcode"
            }
            assert [item["path"] for item in payload["usb"]["files"]] == ["folder/part.GCODE"]
            cube = next(item for item in payload["internal"]["files"] if item["path"] == "cube.gcode")
            assert cube["size"] == 4
            assert isinstance(cube["modified"], int)

            inspect = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/inspect",
                data=b"internal\nmulticolour.gcode", method="POST",
                headers={"Content-Type": "text/plain"},
            )
            with urllib.request.urlopen(inspect, timeout=1) as response:
                inspection = json.load(response)
            assert inspection == {"tools": [0, 1], "multicolour": True, "adaptive_mesh": False}

            inspect_adaptive = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/inspect",
                data=b"internal\nadaptive.gcode", method="POST",
                headers={"Content-Type": "text/plain"},
            )
            with urllib.request.urlopen(inspect_adaptive, timeout=1) as response:
                adaptive = json.load(response)
            assert adaptive == {"tools": [0], "multicolour": False, "adaptive_mesh": True}

            incomplete = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nmulticolour.gcode\n0:1", method="POST",
            )
            try:
                urllib.request.urlopen(incomplete, timeout=1)
                raise AssertionError("incomplete Canvas mapping unexpectedly accepted")
            except urllib.error.HTTPError as error:
                assert error.code == 409
                assert json.load(error)["error"] == "Canvas mapping does not match this G-code"

            mapped = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nmulticolour.gcode\n0:1,1:3", method="POST",
            )
            try:
                urllib.request.urlopen(mapped, timeout=1)
                raise AssertionError("mapped start unexpectedly succeeded without MQTT")
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert json.load(error)["error"] == "Printer MQTT is not ready"

            full_leveling = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nadaptive.gcode\n\nB\nfull", method="POST",
            )
            try:
                urllib.request.urlopen(full_leveling, timeout=1)
                raise AssertionError("full-mesh start unexpectedly succeeded without MQTT")
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert json.load(error)["error"] == "Printer MQTT is not ready"

            saved_start = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nadaptive.gcode\n\nB\nsaved", method="POST",
            )
            try:
                urllib.request.urlopen(saved_start, timeout=1)
                raise AssertionError("saved-mesh start unexpectedly succeeded without MQTT")
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert json.load(error)["error"] == "Printer MQTT is not ready"

            adaptive_start = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nadaptive.gcode\n\nB\nadaptive", method="POST",
            )
            try:
                urllib.request.urlopen(adaptive_start, timeout=1)
                raise AssertionError("adaptive start unexpectedly succeeded without MQTT")
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert json.load(error)["error"] == "Printer MQTT is not ready"

            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\ncube.gcode", method="POST",
                headers={"Content-Type": "text/plain"},
            )
            try:
                urllib.request.urlopen(request, timeout=1)
                raise AssertionError("start request unexpectedly succeeded without MQTT")
            except urllib.error.HTTPError as error:
                assert error.code == 503
                assert json.load(error)["error"] == "Printer MQTT is not ready"

            traversal = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\n../escape.gcode", method="POST",
            )
            try:
                urllib.request.urlopen(traversal, timeout=1)
                raise AssertionError("path traversal unexpectedly accepted")
            except urllib.error.HTTPError as error:
                assert error.code == 404
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
    print("PASS: protected G-code listing, inspection and Canvas mapping endpoints")


if __name__ == "__main__":
    main()
