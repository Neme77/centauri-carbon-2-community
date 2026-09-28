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

        assert status == 200 and b"<html" in page.lower(), "index.html not served"
        status, body = get(base + "/api/health")
        assert status == 200, f"/api/health returned {status}"
        health = json.loads(body)
        assert isinstance(health, dict) and health.get("version"), f"unexpected /api/health: {health}"
        status, _ = get(base + "/api/preferences")
        assert status == 200, f"/api/preferences returned {status}"
        status, _ = get(base + "/definitely-not-a-route")
        assert status == 404, f"unknown route returned {status}"
        assert process.poll() is None, "cc2-control stopped during the smoke test"
    finally:
        process.terminate()
        process.wait(timeout=10)

print(f"PASS: {'emulated ' if runner else ''}cc2-control {health['version']} serves /, /api/health and /api/preferences")
