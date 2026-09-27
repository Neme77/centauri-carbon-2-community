#!/usr/bin/env python3
import base64
import json
import os
import socket
import subprocess
import sys
import time


def http(port, path):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    sock.sendall(f"GET {path} HTTP/1.0\r\nHost: localhost\r\n\r\n".encode())
    data = b""
    while True:
        part = sock.recv(8192)
        if not part:
            break
        data += part
    sock.close()
    return json.loads(data.split(b"\r\n\r\n", 1)[1])


def ws_frame(payload):
    mask = os.urandom(4)
    size = len(payload)
    if size < 126:
        header = bytes((0x81, 0x80 | size))
    else:
        header = bytes((0x81, 0x80 | 126)) + size.to_bytes(2, "big")
    return header + mask + bytes(value ^ mask[i % 4] for i, value in enumerate(payload))


def ws_read(sock):
    header = sock.recv(2)
    assert len(header) == 2 and header[0] == 0x81
    size = header[1] & 127
    if size == 126:
        size = int.from_bytes(sock.recv(2), "big")
    data = b""
    while len(data) < size:
        data += sock.recv(size - len(data))
    return json.loads(data)


binary = sys.argv[1]
control_port, panda_port = 18081, 17125
process = subprocess.Popen(
    [binary, "--port", str(control_port), "--panda-port", str(panda_port),
     "--web-root", "web", "--config", "/tmp/cc2-panda-test-missing.conf",
     "--presets", "/tmp/cc2-panda-test-presets.json"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
)
try:
    for _ in range(30):
        try:
            info = http(panda_port, "/server/info")
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise AssertionError("Panda port did not start")
    assert info["result"]["klippy_state"] == "ready"
    assert "heater_bed" in http(panda_port, "/printer/objects/list")["result"]["objects"]

    key = base64.b64encode(os.urandom(16)).decode()
    sock = socket.create_connection(("127.0.0.1", panda_port), timeout=2)
    sock.sendall((
        "GET /websocket HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode())
    assert sock.recv(4096).startswith(b"HTTP/1.1 101")
    query = json.dumps({"id": 1001, "jsonrpc": "2.0", "method": "printer.objects.query",
                        "params": {"objects": {"webhooks": ["state"],
                        "virtual_sdcard": ["progress"], "print_stats": ["state"],
                        "extruder": ["temperature", "target"],
                        "heater_bed": ["temperature", "target"],
                        "gcode_macro _KNOMI_STATUS": None}}}, separators=(",", ":")).encode()
    sock.sendall(ws_frame(query))
    reply = ws_read(sock)
    assert reply["id"] == 1001
    assert set(reply["result"]["status"]["heater_bed"]) == {"temperature", "target"}
    notification = ws_read(sock)
    assert notification["method"] == "notify_status_update"
    sock.close()
    print("Panda compatibility tests: PASS")
finally:
    process.terminate()
    process.wait(timeout=3)
