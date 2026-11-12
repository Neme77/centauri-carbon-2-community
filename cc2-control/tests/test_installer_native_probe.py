"""Exercise the installer CLI over a real native console socket."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

app = Path(sys.argv[1]).resolve()
idle = {
    "print_stats": {"state": "standby", "machine_status": 1},
    "motion_report": {"live_velocity": 0},
    "extruder": {"temperature": 24, "target": 0},
    "heater_bed": {"temperature": 24, "target": 0},
}
cases = [
    ("idle", idle, 0),
    ("idle", {**idle, "print_stats": {"state": "printing", "machine_status": 2}}, 3),
    ("idle", {**idle, "heater_bed": {"temperature": 24, "target": 50}}, 3),
    ("idle", {**idle, "motion_report": {"live_velocity": 1}}, 3),
    ("idle", {**idle, "extruder": {"temperature": 51, "target": 0}}, 3),
    ("canvas", {"canvas_dev": {"canvas_lite": {"connected": 1}}}, 0),
    ("canvas", {"canvas_dev": {"canvas_lite": {"connected": 0}}}, 2),
    ("canvas", {"canvas_dev": {}}, 3),
]
with tempfile.TemporaryDirectory() as folder:
    for i, (mode, status, expected) in enumerate(cases):
        path = str(Path(folder) / f"native-{i}")
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.bind(path)
            server.listen()
            server.settimeout(4)
            with subprocess.Popen([app, f"--installer-native-{mode}", "--uds-socket", path]) as child:
                client, _ = server.accept()
                with client, client.makefile("rb") as reader:
                    client.settimeout(4)
                    request = bytearray()
                    while (byte := reader.read(1)) not in (b"", b"\x03"):
                        request.extend(byte)
                    parsed = json.loads(request)
                    assert parsed["method"] == "objects/query"
                    client.sendall(json.dumps({"id": parsed["id"], "result": {"status": status}}).encode() + b"\x03")
                    assert child.wait(timeout=4) == expected, (mode, status)
    missing = subprocess.run([app, "--installer-native-canvas", "--uds-socket", str(Path(folder) / "missing")])
    assert missing.returncode == 3
print("PASS: direct native idle/cold/motion and Canvas connected/absent/unknown probes")
