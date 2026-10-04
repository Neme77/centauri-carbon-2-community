#!/usr/bin/env python3
"""Simulated Centauri printer for CC2 Control development.

Publishes printer state and answers CC2 Control's MQTT handshake so a host
build shows live-looking data without a real printer. Speaks raw MQTT 3.1.1
(QoS 0), no dependencies.

This is a dev fixture, not the real printer protocol: it only produces the
fields cc2-control parses (see src/mqtt.c update_state / consume_packets).
It is the starting point for layer 4 of REGRESSION_PREVENTION_PLAN.md; a real
end-to-end test would replay *recorded* traffic instead of this invented state.

Usage:
    # 1. a broker on 127.0.0.1:1883, e.g.
    docker run -d --name cc2-broker -p 1883:1883 eclipse-mosquitto:2 \
        sh -c 'printf "listener 1883\nallow_anonymous true\n" \
        > /mosquitto/config/mosquitto.conf && exec mosquitto -c \
        /mosquitto/config/mosquitto.conf'
    # 2. this simulator
    python3 tests/sim_printer.py &
    # 3. cc2-control pointed at the broker (any non-empty mqtt_password;
    #    the broker is anonymous)
    printf 'mqtt_username=elegoo\nmqtt_password=sim\n' > /tmp/cc2.conf
    ./build/cc2-control --port 8081 --web-root web --config /tmp/cc2.conf ...

Then http://localhost:8081 shows the simulated printer.

Gotcha learned the hard way: cc2-control detects the snapshot by the exact
substring "method":1002, so the JSON must be compact (no space after ':').
"""
import argparse
import json
import socket
import struct
import threading
import time


def status():
    return json.dumps({
        "extruder": {"temperature": 24.8, "target": 0, "filament_detect_enable": 1, "filament_detected": 1},
        "heater_bed": {"temperature": 23.5, "target": 0},
        "ztemperature_sensor": {"temperature": 22.9},
        "fans": {"controller_fan": {"speed": 0}, "heater_fan": {"speed": 0},
                 "fan": {"speed": 0}, "aux_fan": {"speed": 0}, "box_fan": {"speed": 0}},
        "machine_status": {"status": 0, "sub_status": 0, "progress": 0, "sub_status_reason_code": 0},
        "print_status": {"enable": False, "filename": "", "state": "", "uuid": "",
                         "current_layer": 0, "total_layer": 0, "print_duration": 0,
                         "remaining_time_sec": 0, "total_duration": 0},
        "gcode_move": {"x": 0.0, "y": 0.0, "z": 0.0, "speed": 0.0, "speed_mode": 0},
        "tool_head": {"homed_axes": ""},
        "external_device": {"camera": False, "u_disk": False},
        "led": {"status": 0},
    })


def enc_str(s):
    b = s.encode()
    return struct.pack(">H", len(b)) + b


def enc_len(n):
    out = b""
    while True:
        d = n % 128
        n //= 128
        out += bytes([d | (0x80 if n else 0)])
        if not n:
            return out


def send(sock, first, payload):
    sock.sendall(bytes([first]) + enc_len(len(payload)) + payload)


def publish(sock, topic, msg):
    send(sock, 0x30, enc_str(topic) + msg.encode())


def read_len(sock):
    mult, val = 1, 0
    while True:
        b = sock.recv(1)
        if not b:
            return None
        val += (b[0] & 127) * mult
        mult *= 128
        if not (b[0] & 0x80):
            return val


def snapshot():
    snap = json.loads(status())
    snap["method"] = 1002
    snap["id"] = 1002
    # compact: cc2-control matches the literal substring "method":1002
    return json.dumps(snap, separators=(",", ":"))


def main():
    ap = argparse.ArgumentParser(description="Simulated Centauri printer for CC2 Control")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--serial", default="SIM0000000000001")
    ap.add_argument("--interval", type=float, default=2.0, help="seconds between status pushes")
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port))
    conn = enc_str("MQTT") + bytes([4, 0x02]) + struct.pack(">H", 30) + enc_str("sim-printer")
    send(s, 0x10, conn)
    if s.recv(4)[0] != 0x20:
        raise SystemExit("CONNACK failed (broker rejected connection)")
    send(s, 0x82, struct.pack(">H", 1) + enc_str("elegoo/#") + b"\x00")  # SUBSCRIBE
    s.recv(5)  # SUBACK
    print(f"sim-printer connected to {args.host}:{args.port} as {args.serial}", flush=True)

    def heartbeat():
        while True:
            publish(s, f"elegoo/{args.serial}/api_status", status())
            # push the snapshot proactively so snapshot_received flips without
            # waiting for cc2-control's slow (60s) request cadence
            publish(s, f"elegoo/{args.serial}/api_response", snapshot())
            time.sleep(args.interval)
    threading.Thread(target=heartbeat, daemon=True).start()

    while True:
        h = s.recv(1)
        if not h:
            break
        if h[0] >> 4 != 3:  # only handle PUBLISH from cc2-control
            n = read_len(s)
            if n:
                s.recv(n)
            continue
        n = read_len(s)
        data = b""
        while len(data) < n:
            data += s.recv(n - len(data))
        tlen = struct.unpack(">H", data[:2])[0]
        topic = data[2:2 + tlen].decode(errors="replace")
        payload = data[2 + tlen:].decode(errors="replace")
        if topic.endswith("/api_register"):
            publish(s, f"elegoo/{args.serial}/register_response", '{"error":"ok"}')
            print("registered <-", topic, flush=True)
        elif topic.endswith("/api_request") and '"method":1002' in payload:
            publish(s, f"elegoo/{args.serial}/api_response", snapshot())
            print("snapshot <-", topic, flush=True)


if __name__ == "__main__":
    main()
