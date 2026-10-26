#!/usr/bin/env python3
import base64
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse


def http(port, path, status=200):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    sock.sendall(f"GET {path} HTTP/1.0\r\nHost: localhost\r\n\r\n".encode())
    data = b""
    while True:
        part = sock.recv(8192)
        if not part:
            break
        data += part
    sock.close()
    head, body = data.split(b"\r\n\r\n", 1)
    assert head.split()[1] == str(status).encode(), (path, head)
    return json.loads(body, parse_constant=lambda value: (_ for _ in ()).throw(AssertionError('non-JSON number: '+value)))


def ws_frame(payload, opcode=1):
    mask = os.urandom(4)
    size = len(payload)
    if size < 126:
        header = bytes((0x80 | opcode, 0x80 | size))
    else:
        header = bytes((0x81, 0x80 | 126)) + size.to_bytes(2, "big")
    return header + mask + bytes(value ^ mask[i % 4] for i, value in enumerate(payload))


def ws_read_frame(sock):
    header = sock.recv(2)
    assert len(header) == 2
    size = header[1] & 127
    if size == 126:
        size = int.from_bytes(sock.recv(2), "big")
    data = b""
    while len(data) < size:
        data += sock.recv(size - len(data))
    return header[0] & 15, data


def ws_read(sock):
    opcode, data = ws_read_frame(sock)
    assert opcode == 1
    return json.loads(data, parse_constant=lambda value: (_ for _ in ()).throw(AssertionError('non-JSON number: '+value)))


def ws_call(sock, payload):
    sock.sendall(ws_frame(json.dumps(payload, separators=(",", ":")).encode()))
    while True:
        reply = ws_read(sock)
        if reply.get("id") == payload["id"]:
            return reply


# Slicer output as ElegooSlicer/OrcaSlicer writes it: usage before the config
# block, filament_type inside it, both near the end of a large file.
def gcode(types, used):
    return ("; HEADER_BLOCK_START\n; HEADER_BLOCK_END\n" + "G1 X1 Y1 E0.1\n" * 40000 +
            f"; filament used [mm] = {used}\n; total filament used [g] = 54.27\n"
            "; CONFIG_BLOCK_START\n; chamber_temperature = 0,0\n"
            f"; filament_settings_id = \"Elegoo PETG\";\"Elegoo PLA\"\n; filament_type = {types}\n"
            "; CONFIG_BLOCK_END\n")


binary = sys.argv[1]
gcode_root = tempfile.mkdtemp(prefix="cc2-panda-gcode-")
# ElegooSlicer-style name, longer than the 63 bytes DragonBreath keeps.
multi = 'ECC2_0.6_Compact "CC2" Hood_Elegoo PETG-CF 0.6 Hyper CHT_0.24_1h13m.gcode'
assert len(multi) > 63
with open(os.path.join(gcode_root, multi), "w") as f:
    f.write(gcode("PETG;PLA", "17677.21, 232.46"))
with open(os.path.join(gcode_root, "asa.gcode"), "w") as f:
    f.write(gcode("ASA", "45383.49"))
with open(os.path.join(gcode_root, "plain.gcode"), "w") as f:
    f.write("G28\n")
os.mkdir(os.path.join(gcode_root, "folder.gcode"))
outside = tempfile.mkdtemp(prefix="cc2-panda-outside-")
with open(os.path.join(outside, "outside.gcode"), "w") as f:
    f.write(gcode("ABS", "100"))
os.symlink(os.path.join(outside, "outside.gcode"), os.path.join(gcode_root, "linked.gcode"))
os.mkfifo(os.path.join(gcode_root, "pipe.gcode"))
bad_usage = ["nan", "NaN", "inf", "-1", "1e999", "1e-999", "12junk", "12,", "1, 2, nan", "", ",1", ",".join(["1"] * 17)]
for index, value in enumerate(bad_usage):
    with open(os.path.join(gcode_root, f"bad-{index}.gcode"), "w") as f:
        f.write(gcode("PLA", value))
control_port, panda_port = 18081, 17125
process = subprocess.Popen(
    [binary, "--port", str(control_port), "--panda-port", str(panda_port),
     "--web-root", "web", "--config", "/tmp/cc2-panda-test-missing.conf",
     "--presets", "/tmp/cc2-panda-test-presets.json", "--gcode-internal", gcode_root],
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
    objects = http(panda_port, "/printer/objects/list")["result"]["objects"]
    assert "heater_bed" in objects and "toolhead" in objects

    # server.files.metadata over HTTP: the slicer's filament list and per-filament usage.
    meta = http(panda_port, "/server/files/metadata?filename=" + urllib.parse.quote(multi))["result"]
    assert meta["filename"] == multi
    assert meta["filament_type"] == "PETG;PLA"
    assert meta["filament_used_mm"] == [17677.21, 232.46]
    assert http(panda_port, "/server/files/metadata?filename=asa.gcode")["result"]["filament_type"] == "ASA"
    plain = http(panda_port, "/server/files/metadata?filename=plain.gcode")["result"]
    assert "filament_type" not in plain and "filament_used_mm" not in plain
    for index in range(len(bad_usage)):
        meta = http(panda_port, f"/server/files/metadata?filename=bad-{index}.gcode")["result"]
        assert meta["filament_type"] == "PLA"
        assert "filament_used_mm" not in meta, (bad_usage[index], meta)
    # Rejected special files must return promptly, leaving the bridge responsive.
    for name in ["linked.gcode", "pipe.gcode"]:
        started = time.monotonic()
        http(panda_port, "/server/files/metadata?filename=" + name, status=404)
        assert time.monotonic() - started < 2
    assert http(panda_port, "/server/files/metadata?filename=asa.gcode")["result"]["filament_type"] == "ASA"
    # Only plain names inside the G-code folder; never a path, a hidden file or a directory.
    # A cut-off name only stands for the file being printed; with no print it matches nothing.
    for cut in (multi[:20], multi[:63]):
        http(panda_port, "/server/files/metadata?filename=" + urllib.parse.quote(cut), status=404)
    for name in ["missing.gcode", "../" + os.path.basename(gcode_root) + "/asa.gcode",
                 "/etc/passwd", "..", ".hidden", "folder.gcode", "%2e%2e%2fasa.gcode", ""]:
        http(panda_port, "/server/files/metadata?filename=" + name, status=404)

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
    status = reply["result"]["status"]
    assert set(status["heater_bed"]) == {"temperature", "target"}
    # DragonBreath fetches metadata for print_stats.filename and picks the active tool's filament.
    assert status["print_stats"] == {"state": "standby", "filename": ""}
    assert status["toolhead"] == {"extruder": "extruder"}
    notification = ws_read(sock)
    assert notification["method"] == "notify_status_update"
    assert notification["params"][0]["print_stats"]["filename"] == ""

    # ESP-IDF clients (DragonBreath) send empty pings and drop the link without a pong.
    for payload in (b"", b"keepalive"):
        sock.sendall(ws_frame(payload, opcode=9))
        deadline = time.monotonic() + 3
        while True:
            assert time.monotonic() < deadline, f"no pong for ping {payload!r}"
            opcode, data = ws_read_frame(sock)
            if opcode == 10:
                assert data == payload
                break
            assert opcode == 1

    # The same request over the WebSocket, as DragonBreath sends it.
    reply = ws_call(sock, {"jsonrpc": "2.0", "method": "server.files.metadata",
                           "params": {"filename": multi}, "id": 2})
    assert reply["result"]["filament_type"] == "PETG;PLA"
    assert reply["result"]["filament_used_mm"] == [17677.21, 232.46]
    for name in ["../asa.gcode", "missing.gcode", "a\\u002fb", "linked.gcode", "pipe.gcode"]:
        reply = ws_call(sock, {"jsonrpc": "2.0", "method": "server.files.metadata",
                               "params": {"filename": name}, "id": 3})
        assert reply["error"]["code"] == 404 and "result" not in reply
    reply = ws_call(sock, {"jsonrpc": "2.0", "method": "server.files.metadata",
                           "params": {"filename": "bad-0.gcode"}, "id": 4})
    assert reply["result"]["filament_type"] == "PLA" and "filament_used_mm" not in reply["result"]
    # An empty close frame is handled, rather than leaving the socket subscribed.
    sock.sendall(ws_frame(b"", opcode=8))
    sock.settimeout(2)
    pending = b""
    while True:
        part = sock.recv(4096)
        if not part:
            break
        pending += part
        assert len(pending) < 16384
    sock.close()
    print("Panda compatibility tests: PASS")
finally:
    process.terminate()
    process.wait(timeout=3)
    shutil.rmtree(gcode_root)
    shutil.rmtree(outside)
