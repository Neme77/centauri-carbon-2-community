#!/usr/bin/env python3
"""Smoke test: the built binary starts and serves its basic endpoints.

Run by `make test` against the host build. CI also runs it against the ARM
build under qemu user-mode emulation:

    CC2_TEST_RUNNER=qemu-arm python3 tests/test_smoke.py dist/cc2-control/cc2-control
"""
import json
import os
import shlex
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


binary = Path(sys.argv[1]).resolve()
runner = shlex.split(os.environ.get("CC2_TEST_RUNNER", ""))
web_root = binary.parent / "web" if (binary.parent / "web/index.html").is_file() else ROOT / "web"

with tempfile.TemporaryDirectory(prefix="cc2-smoke-") as temporary:
    state = Path(temporary)
    (state / "cc2-control.conf").write_text("mqtt_username=elegoo\nmqtt_password=smoke\n", encoding="ascii")
    port = free_port()
    process = subprocess.Popen([
        *runner, str(binary), "--port", str(port), "--panda-port", "0",
        "--web-root", str(web_root), "--config", str(state / "cc2-control.conf"),
        "--presets", str(state / "presets.json"), "--preferences", str(state / "preferences.json"),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    try:
        # Emulated ARM start-up is slower than a native one.
        for _ in range(300):
            try:
                status, page = get(base + "/")
                break
            except OSError:
                assert process.poll() is None, f"cc2-control exited with {process.returncode}"
                time.sleep(0.1)
        else:
            raise AssertionError("cc2-control did not start listening")

        def call(headers, method="GET", payload=None, endpoint="/api/preferences"):
            req = urllib.request.Request(base + endpoint, data=payload, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=3) as response:
                    return response.status
            except urllib.error.HTTPError as error:
                return error.code

        origin = base
        payload = b'{"language":"en"}'
        assert call({"Host": "attacker.example:" + str(port)}) == 403
        assert call({"Origin": "http://attacker.example"}) == 403
        assert call({"Origin": "null"}) == 403
        assert call({"Origin": origin}, "PUT", payload) == 403
        assert call({"Sec-Fetch-Site": "cross-site", "X-CC2-Request": "1"}, "PUT", payload) == 403
        assert call({"Origin": origin, "X-CC2-Request": "1", "Content-Type": "application/json"}, "PUT", payload) == 200
        assert call({"Content-Type": "application/json"}, "PUT", payload) == 403
        assert call({"X-CC2-Request": "1", "Content-Type": "application/json"}, "PUT", payload) == 200
        assert call({"Sec-Fetch-Site": "same-origin"}) == 200
        assert call({}, "POST", b"REBOOT_AFTER_EMERGENCY", "/api/recovery/reboot") == 403
        assert call({"X-CC2-Request": "1"}, "POST", b"REBOOT_AFTER_EMERGENCY", "/api/recovery/reboot") == 409
        assert call({"Origin": "http://attacker.example"}, endpoint="/api/gcode-files/download?storage=internal&file=part.gcode") == 403
        assert call({}, endpoint="/api/gcode-files/download?storage=internal&file=../part.gcode") == 400
        with socket.create_connection(("127.0.0.1", port), timeout=3) as fragmented:
            fragmented.sendall((f"PUT /api/preferences HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\ncontent-length: {len(payload)}\r\nX-CC2-Request: 1\r\n\r\n").encode() + payload[:3])
            assert call({}) == 200
            fragmented.sendall(payload[3:])
            assert fragmented.recv(1024).startswith(b"HTTP/1.1 200")
        crowded = []
        try:
            for _ in range(8):
                client = socket.create_connection(("127.0.0.1", port), timeout=3)
                client.sendall(b"GET /api/preferences HTTP/1.1\r\nHost:")
                crowded.append(client)
            time.sleep(0.1)
            assert call({}) == 503, "full receive pool must return explicit overload"
        finally:
            for client in crowded:
                client.close()
        time.sleep(0.1)
        assert call({}) == 200
        slow = []
        try:
            for _ in range(4):
                client = socket.create_connection(("127.0.0.1", port), timeout=3)
                client.sendall(b"POST /api/control HTTP/1.1\r\nHost:")
                slow.append(client)
            started = time.monotonic()
            assert call({}) == 200
            assert time.monotonic() - started < 0.75, "slow headers blocked the service"
            time.sleep(2.2)
            for client in slow:
                assert client.recv(1) == b"", "total receive deadline was not enforced"
        finally:
            for client in slow:
                client.close()
        with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
            client.sendall((f"GET /api/health HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nHost: attacker.example\r\n\r\n").encode())
            assert client.recv(1024).startswith(b"HTTP/1.1 403")
    finally:
        process.terminate()
        process.wait(timeout=10)

print("PASS: HTTP host/origin validation, browser mutation marker, duplicate header rejection, native client compatibility")
