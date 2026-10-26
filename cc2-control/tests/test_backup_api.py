"""Real HTTP export, auth/size refusal and responsiveness during a slow chunk."""
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import zlib

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="cc2-backup-api-") as temporary:
    root = Path(temporary)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen([
        *shlex.split(os.environ.get("CC2_TEST_RUNNER", "")), binary,
        "--port", str(port), "--panda-port", "0", "--config", str(root / "connection.conf"),
        "--preferences", str(root / "preferences.json"), "--presets", str(root / "presets.json"),
        "--plates", str(root / "plates.json"), "--spools", str(root / "spools.json"),
    ], cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    origin = f"http://127.0.0.1:{port}"
    slow = None
    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(origin + "/api/health", timeout=1):
                    break
            except OSError:
                time.sleep(0.05)
        with urllib.request.urlopen(origin + "/api/backup/export", timeout=3) as response:
            assert response.headers["Content-Type"] == "application/octet-stream"
            text = response.read().decode("utf-8")
        data = json.loads(text)
        assert data["schema"] == 1 and data["format"] == "cc2-control-backup"
        position = text.index('"data":') + len('"data":')
        _, end = json.JSONDecoder().raw_decode(text, position)
        assert f"{zlib.crc32(text[position:end].encode('utf-8')):08x}" == data["crc32"]
        assert set(data["data"]["sections"]) == {"preferences", "materials", "plates", "spools"}
        assert not any(key in text for key in ("password", "access_code", "PRIVATE KEY", "autosave.cfg"))
        assert not list(root.glob("*.json")), "Export must not create storage files"
        request = urllib.request.Request(origin + "/api/backup/import/start", data=b"100",
            headers={"X-CC2-Request": "1", "X-CC2-LAN": "not-configured"}, method="POST")
        try:
            urllib.request.urlopen(request, timeout=3)
            raise AssertionError("unauthenticated restore accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 401
        # Ordinary bounded HTTP receive pool, not a blocking upload worker.
        slow = socket.create_connection(("127.0.0.1", port), timeout=3)
        slow.sendall((f"POST /api/backup/import/chunk HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
                      "X-CC2-Request: 1\r\nContent-Length: 8192\r\n\r\nx").encode())
        started = time.monotonic()
        with urllib.request.urlopen(origin + "/api/health", timeout=2) as response:
            assert json.load(response)["service"] == "cc2-control"
        assert time.monotonic() - started < 2
        with socket.create_connection(("127.0.0.1", port), timeout=3) as oversized:
            oversized.sendall((f"POST /api/backup/import/chunk HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n"
                               "X-CC2-Request: 1\r\nContent-Length: 65536\r\n\r\n").encode())
            assert oversized.recv(1024).startswith(b"HTTP/1.1 413 ")
        assert not list(root.glob("*.json")), "Refused/unfinished imports must not change files"
        print("PASS backup real HTTP: exact CRC export, no credentials/writes, auth and size refusal, responsive slow chunk")
    finally:
        if slow is not None:
            slow.close()
        process.terminate()
        process.wait(timeout=5)
