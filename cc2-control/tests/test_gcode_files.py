#!/usr/bin/env python3
"""Integration test for the bounded, read-only G-code listing endpoint."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import urllib.error
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def check_newest_files_listed(binary):
    """More files than the list holds: the newest ones must be listed,
    whatever order the directory returns them in."""
    with tempfile.TemporaryDirectory(prefix="cc2-gcode-many-") as temporary:
        root = Path(temporary)
        internal = root / "internal"
        internal.mkdir()
        base = 1_700_000_000
        # Newest first: a directory that returns entries in creation order (or
        # its reverse, like tmpfs) cannot satisfy the test by accident.
        for index in reversed(range(200)):
            path = internal / f"part-{(index * 7919) % 200:03d}.gcode"
            path.write_text("G28\n", encoding="ascii")
            os.utime(path, (base + index, base + index))
        newest = {f"part-{(index * 7919) % 200:03d}.gcode" for index in range(72, 200)}
        port = free_port()
        process = subprocess.Popen(
            [str(binary), "--port", str(port), "--web-root", str(root),
             "--config", str(root / "missing.conf"),
             "--presets", str(root / "presets.json"),
             "--gcode-internal", str(internal), "--gcode-usb", str(root / "usb")],
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
            space = payload["internal"]["space"]
            disk = os.statvfs(internal)
            assert space["total_bytes"] == disk.f_frsize * disk.f_blocks
            assert 0 <= space["available_bytes"] <= space["free_bytes"] <= space["total_bytes"]
            assert payload["usb"]["space"] is None
            listing = payload["internal"]
            assert listing["truncated"] is True
            assert listing["count"] == 128 and listing["total"] == 200
            assert {item["path"] for item in listing["files"]} == newest
            times = [item["modified"] for item in listing["files"]]
            assert times == sorted(times, reverse=True) and times[0] == base + 199
            assert payload["usb"]["total"] == 0

            def all_pages():
                with urllib.request.urlopen(endpoint, timeout=2) as response:
                    group = json.load(response)["internal"]
                files = list(group["files"])
                seen = set()
                while group["has_more"]:
                    assert group["scan_truncated"] is False
                    cursor = group["next"]
                    assert isinstance(cursor["modified"], str)
                    query = urllib.parse.urlencode({"storage":"internal", "before":cursor["modified"], "file":cursor["path"]})
                    assert query not in seen
                    seen.add(query)
                    assert len(seen) < 5
                    with urllib.request.urlopen(endpoint + "/page?" + query, timeout=2) as response:
                        group = json.load(response)["internal"]
                    assert group["count"] <= 128
                    files.extend(group["files"])
                assert group["next"] is None
                paths = [item["path"] for item in files]
                assert len(paths) == len(set(paths)), "cursor pages repeat a file"
                return paths

            assert len(all_pages()) == 200, "old files remain accessible"
            for path in internal.iterdir():
                os.utime(path, (base, base))
            (internal / "nested").mkdir()
            extras = ["Foo.gcode", "foo.gcode", "nested/old file &x.gcode"]
            for name in extras:
                path = internal / name
                path.write_text("G28\n", encoding="ascii")
                os.utime(path, (base, base))
            complete = all_pages()
            assert len(complete) == 203 and set(extras).issubset(complete)
            for query in [
                "storage=internal&before=-&file=x.gcode",
                "storage=internal&before=9223372036854775808&file=x.gcode",
                "storage=internal&before=1&file=../x.gcode",
                "storage=internal&before=1&file=%00x.gcode",
                "storage=internal&before=1&file=%zz.gcode",
                "storage=unknown&before=1&file=x.gcode",
                "storage=internal&before=1&file=x.gcode&file=y.gcode",
                "storage=internal&file=x.gcode",
            ]:
                try:
                    urllib.request.urlopen(endpoint + "/page?" + query, timeout=2)
                    raise AssertionError("invalid cursor accepted: " + query)
                except urllib.error.HTTPError as error:
                    assert error.code == 400, query
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_gcode_files.py PATH_TO_CC2_CONTROL")
    binary = Path(sys.argv[1]).resolve()
    check_newest_files_listed(binary)
    with tempfile.TemporaryDirectory(prefix="cc2-gcode-test-") as temporary:
        root = Path(temporary)
        internal = root / "internal"
        usb = root / "usb"
        internal.mkdir()
        (usb / "folder").mkdir(parents=True)
        (internal / "cube.gcode").write_text("G28\n", encoding="ascii")
        (internal / "multicolour.gcode").write_text(
            "; filament_colour = #FF0000;#00FF00\n; filament_type = PLA;PETG\n; T9 and BED_MESH_CALIBRATE FROM_SLICER=1 in comments are ignored\nM140 S0\nM190 S65 A\nT0\nG1 X1\nT1\nG1 X2\n"
            "; filament used [mm] = 1.0, 2.0\n; filament used [mm] = 1234.56, 78.9\n; nozzle_diameter = 0.6\n",
            encoding="ascii",
        )
        (internal / "adaptive.gcode").write_text(
            "BED_MESH_CALIBRATE MESH_MIN=20,30 MESH_MAX=180,190 FROM_SLICER=1\nG28\n",
            encoding="ascii",
        )
        (internal / "metadata.gcode").write_text(
            "; bed_temperature_gcode = M190 S60\n"
            "; bed_temperature_formula = 190\n"
            "; nozzle_temperature = 6211\n"
            "; first_layer_temperature = 215\n"
            "; bed_temperature = 55\n"
            "; first_layer_bed_temperature = 60\n"
            "; filament used [g] = 0, 0, 0, 0\n"
            "G1 X10 Y20\n"
            "; filament used [g] = 0, 21.2, 3.1, 0\n",
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
            assert payload["internal"]["space"]["total_bytes"] > 0
            assert payload["usb"]["space"] is None, "plain USB directory must not report internal flash space"
            assert payload["internal"]["truncated"] is False
            assert payload["internal"]["count"] == payload["internal"]["total"] == 4
            assert payload["usb"]["available"] is True
            assert {item["path"] for item in payload["internal"]["files"]} == {
                "adaptive.gcode", "cube.gcode", "metadata.gcode", "multicolour.gcode"
            }
            assert [item["path"] for item in payload["usb"]["files"]] == ["folder/part.GCODE"]
            cube = next(item for item in payload["internal"]["files"] if item["path"] == "cube.gcode")
            assert cube["size"] == 4
            assert isinstance(cube["modified"], int)

            inspect = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/inspect",
                data=b"internal\nmulticolour.gcode", method="POST",
                headers={"X-CC2-Request": "1", "Content-Type": "text/plain"},
            )
            with urllib.request.urlopen(inspect, timeout=1) as response:
                inspection = json.load(response)
            assert inspection == {"tools": [0, 1], "multicolour": True, "adaptive_mesh": False, "filaments": [{"tool": 0, "color": "#FF0000", "material": "PLA", "mm": 1234.6}, {"tool": 1, "color": "#00FF00", "material": "PETG", "mm": 78.9}], "bed_temperature": 65, "nozzle_diameter": 0.6}

            inspect_adaptive = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/inspect",
                data=b"internal\nadaptive.gcode", method="POST",
                headers={"X-CC2-Request": "1", "Content-Type": "text/plain"},
            )
            with urllib.request.urlopen(inspect_adaptive, timeout=1) as response:
                adaptive = json.load(response)
            assert adaptive == {"tools": [0], "multicolour": False, "adaptive_mesh": True, "filaments": [{"tool": 0, "color": "", "material": ""}], "bed_temperature": None, "nozzle_diameter": None}

            metadata_request = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/metadata",
                data=b"internal\nmetadata.gcode", method="POST",
                headers={"X-CC2-Request": "1", "Content-Type": "text/plain"},
            )
            with urllib.request.urlopen(metadata_request, timeout=1) as response:
                metadata = json.load(response)
            assert metadata["nozzle_temperature"] == 215.0
            assert metadata["bed_temperature"] == 60.0
            assert metadata["filament_grams"] == 24.3

            # Missing mass is unknown, not a fabricated zero. File replacement
            # at the same path must invalidate the bounded analysis cache.
            (internal / "metadata.gcode").write_text(
                "; bed_temperature_gcode = M190 S60\n; filament used [mm] = 1200\n",
                encoding="ascii",
            )
            with urllib.request.urlopen(metadata_request, timeout=1) as response:
                metadata = json.load(response)
            assert metadata["bed_temperature"] is None
            assert metadata["filament_grams"] is None
            (internal / "metadata.gcode").write_text(
                "; bed_temperature_gcode = M190 S190\n"
                "M140 S0\nM140 S30\nM190 S60\n"
                "; nozzle_temperature = 220\n; nozzle_temperature_initial_layer = 225\n",
                encoding="ascii",
            )
            with urllib.request.urlopen(metadata_request, timeout=1) as response:
                metadata = json.load(response)
            assert metadata["bed_temperature"] == 60.0
            assert metadata["nozzle_temperature"] == 225.0
            # Reproduce the reporter's OrcaSlicer export: tool 0 is unused;
            # a start-script comment contains M190 after a bed placeholder.
            (internal / "metadata.gcode").write_text(
                "M140 S55\nM190 S55 A\n"
                "; filament used [g] = 0.00, 21.24\n"
                "; total filament used [g] = 21.24\n"
                "; bed_temperature_formula = by_highest_temp\n"
                "; machine_start_gcode = M140 S[bed_temperature_initial_layer_single]\\nM190 S[bed_temperature_initial_layer_single] A\n"
                "; nozzle_temperature = 220,220\n"
                "; nozzle_temperature_initial_layer = 220,220\n"
                "; first_layer_bed_temperature = 55\n"
                "; first_layer_temperature = 220\n",
                encoding="ascii",
            )
            with urllib.request.urlopen(metadata_request, timeout=1) as response:
                metadata = json.load(response)
            assert metadata["bed_temperature"] == 55.0
            assert metadata["nozzle_temperature"] == 220.0
            assert metadata["filament_grams"] == 21.24
            (internal / "metadata.gcode").write_text(
                "; filament used [g] = 0, 0\n", encoding="ascii"
            )
            with urllib.request.urlopen(metadata_request, timeout=1) as response:
                assert json.load(response)["filament_grams"] == 0.0

            incomplete = urllib.request.Request(
                f"http://127.0.0.1:{port}/api/gcode-files/print",
                data=b"internal\nmulticolour.gcode\n0:1", method="POST",
                headers={"X-CC2-Request": "1"},
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
                headers={"X-CC2-Request": "1"},
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
                headers={"X-CC2-Request": "1"},
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
                headers={"X-CC2-Request": "1"},
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
                headers={"X-CC2-Request": "1"},
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
                headers={"X-CC2-Request": "1", "Content-Type": "text/plain"},
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
                headers={"X-CC2-Request": "1"},
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
