"""Exercise the real event loop against a local fake UDS peer."""
import json
import os
import shlex
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="cc2-uds-api-") as temporary:
    root = Path(temporary)
    path = str(root / "peer.sock")
    try:
        listener = socket.socket(socket.AF_UNIX)
    except PermissionError:
        print("SKIP UDS peer integration: environment prohibits AF_UNIX sockets")
        sys.exit(0)
    listener.bind(path)
    listener.listen(1)
    listener.settimeout(10)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen([
        *shlex.split(os.environ.get("CC2_TEST_RUNNER", "")), str(binary),
        "--port", str(port), "--panda-port", "0", "--uds-socket", path,
        "--config", str(root / "missing.conf"),
        "--preferences", str(root / "preferences.json"),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    peer = None
    def get(route):
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{route}", timeout=3) as reply:
            return json.load(reply)
    def wait_for(route, predicate):
        for _ in range(80):
            try:
                value = get(route)
                if predicate(value):
                    return value
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise AssertionError(f"Timed out: {route}")
    def send(message):
        peer.sendall(json.dumps(message).encode() + b"\x03")
    try:
        peer, _ = listener.accept()
        peer.settimeout(3)
        request = b""
        while b"\x03" not in request:
            request += peer.recv(4096)
        subscription = json.loads(request.split(b"\x03")[0])
        assert subscription["method"] == "objects/subscribe"
        assert "gcode/script" not in request.decode()
        send({"id": 11, "result": {"eventtime": 1, "status": {
            "extruder": {"temperature": 210, "target": 215},
            "fan": {"speed": 0.6, "rpm": 8700},
            "gcode_move": {"speed_factor": 0.8, "extrude_factor": 0.95},
        }}})
        value = wait_for("/api/uds", lambda x: x["fresh"])
        assert value["values"]["speed_factor"] == 0.8
        assert value["values"]["extrude_factor"] == 0.95
        send({"id": 0, "report": {"message": "vendor report"}})
        send({"method": "other_notification", "params": {}})
        value = wait_for("/api/uds", lambda x: x["ignored_messages"] == 2)
        assert value["connections"] == 1 and value["disconnects"] == 0
        assert value["values"]["speed_factor"] == 0.8
        printer = get("/api/printer")
        assert printer["extruder"] == {"temperature": 210, "target": 215}
        assert printer["fans"]["part"] == 153
        assert printer["tuning"] == {"speed_percent": 80, "flow_percent": 95, "live_velocity": None}
        send({"method": "cc2_status", "params": {"eventtime": 2, "status": {
            "extruder": {"temperature": 211}, "gcode_move": {"speed_factor": 1.3}
        }}})
        value = wait_for("/api/uds", lambda x: x["values"]["speed_factor"] == 1.3)
        assert value["values"]["nozzle_target"] == 215
        assert value["values"]["part_rpm"] == 8700
        peer.close()
        peer = None
        wait_for("/api/uds", lambda x: not x["fresh"])
        assert get("/api/printer")["extruder"]["temperature"] is None
        # Accept the automatic reconnection and confirm the old cache is reset.
        peer, _ = listener.accept()
        peer.settimeout(3)
        assert b"objects/subscribe" in peer.recv(4096)
        send({"id": 11, "result": {"eventtime": 0.1, "status": {
            "extruder": {"temperature": 30}
        }}})
        value = wait_for("/api/uds", lambda x: x["fresh"])
        assert value["values"]["nozzle_temperature"] == 30
        assert value["values"]["speed_factor"] is None
        print("PASS UDS subscription, HTTP cache, delta preservation, disconnect fallback and reconnect")
    finally:
        if peer is not None:
            peer.close()
        listener.close()
        process.terminate()
        process.wait(timeout=5)
