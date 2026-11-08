#!/usr/bin/env python3
"""Integration test for the print queue: adding, ordering, copies, policy, starts without a
printer link, missing files and persistence across restarts. The binary path is argv[1]."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Server:
    def __init__(self, binary, root, queue_file):
        self.port = free_port()
        self.process = subprocess.Popen(
            [str(binary), "--port", str(self.port), "--web-root", str(root),
             "--config", str(root / "missing.conf"), "--presets", str(root / "presets.json"),
             "--preferences", str(root / "ui-preferences.json"), "--plates", str(root / "plates.json"),
             "--spools", str(root / "spools.json"), "--queue", str(queue_file),
             "--gcode-internal", str(root / "internal"), "--gcode-usb", str(root / "usb")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        for _ in range(50):
            try:
                self.get()
                return
            except Exception:
                if self.process.poll() is not None:
                    stdout, stderr = self.process.communicate()
                    raise AssertionError(f"server stopped\n{stdout}\n{stderr}")
                time.sleep(0.1)
        raise AssertionError("server did not expose /api/queue")

    def call(self, path, body=None):
        request = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}")
        if body is not None:
            request.data = body.encode("utf-8")
            request.method = "POST"
            request.add_header("X-CC2-Request", "1")
        try:
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def get(self):
        status, payload = self.call("/api/queue")
        assert status == 200, status
        return payload

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)


def add_body(path, copies=3, position="end", mapping="", side="A", leveling="saved", timelapse="0",
             estimate="6000", grams="42.5", storage="internal", measure="", nozzle=""):
    return "\n".join([storage, path, mapping, side, leveling, timelapse, measure, nozzle, str(copies), position, estimate, grams])


def queue_file_text(items, policy=("wait", 60, True), next_id=None):
    """A queue file as queue_save() writes it, for restart tests."""
    rows = []
    for item in items:
        row = {
            "id": item["id"], "storage": "internal", "path": item["path"], "mapping": "", "side": "A",
            "leveling": "saved", "timelapse": False, "measure": "", "nozzle": "", "copies": item["copies"],
            "done": item["done"], "estimated_seconds": -1, "filament_grams": -1.0, "added": 1790899200,
            "state": "done" if item["done"] >= item["copies"] else "pending", "last_result": "",
            "print_name": "", "native_seen": False, "job_uuid": "",
        }
        rows.append(row)
    return json.dumps({
        "version": 1, "next_id": next_id or max(i["id"] for i in items) + 1,
        "policy": {"after": policy[0], "delay": policy[1], "remove_done": policy[2]}, "items": rows,
    }) + "\n"


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cc2-queue-") as temporary:
        root = Path(temporary)
        internal = root / "internal"
        internal.mkdir()
        (root / "usb").mkdir()
        for name in ("part.gcode", "other.gcode"):
            (internal / name).write_text("G28\nG1 X10\n", encoding="ascii")
        queue_file = root / "print-queue.json"
        server = Server(binary, root, queue_file)
        try:
            payload = server.get()
            assert payload["available"] is True and payload["items"] == [], payload
            assert payload["policy"] == {"after": "wait", "delay": 60, "remove_done": True}, payload["policy"]
            assert payload["next"] is None and payload["active"] is None

            # Adding keeps every print choice; "next" goes to the head.
            status, reply = server.call("/api/queue/add", add_body("part.gcode"))
            assert status == 201 and reply["ok"] and reply["id"] == 1 and reply["count"] == 1, (status, reply)
            status, reply = server.call("/api/queue/add", add_body(
                "other.gcode", copies=1, position="next", mapping="0:1", side="B", leveling="calibrate", timelapse="1",
                estimate="", grams=""))
            assert status == 201 and reply["id"] == 2, (status, reply)
            items = server.get()["items"]
            assert [i["id"] for i in items] == [2, 1], items
            head, tail = items
            assert head["mapping"] == "0:1" and head["side"] == "B" and head["leveling"] == "calibrate"
            assert head["timelapse"] is True and head["estimated_seconds"] is None and head["filament_grams"] is None
            assert tail["copies"] == 3 and tail["done"] == 0 and tail["estimated_seconds"] == 6000
            assert tail["filament_grams"] == 42.5 and tail["state"] == "pending" and tail["present"] is True
            assert server.get()["next"] == 2

            # Validation refuses what the print path would refuse, before anything is stored.
            for body, expected in [
                (add_body("part.gcode", copies=0), 400),
                (add_body("part.gcode", copies=100), 400),
                (add_body("part.gcode", side="C"), 400),
                (add_body("part.gcode", leveling="sometimes"), 400),
                (add_body("part.gcode", timelapse="2"), 400),
                (add_body("part.gcode", mapping="zz"), 400),
                (add_body("part.gcode", position="later"), 400),
                (add_body("part.gcode", estimate="soon"), 400),
                (add_body("missing.gcode"), 404),
                (add_body("../part.gcode"), 404),
                (add_body("part.gcode", storage="floppy"), 400),
                # Measurement and nozzle are plate-library ids: nothing else is stored or written to the file.
                (add_body("part.gcode", nozzle='x"y'), 400),
                (add_body("part.gcode", measure="a\\b"), 400),
                (add_body("part.gcode", measure="0123456789abcdef0"), 400),
            ]:
                status, reply = server.call("/api/queue/add", body)
                assert status == expected and reply["ok"] is False, (body, status, reply)
            assert len(server.get()["items"]) == 2
            status, reply = server.call("/api/queue/add", add_body("part.gcode", measure="0123456789abcdef", nozzle="fedcba9876543210"))
            assert status == 201, (status, reply)
            kept = next(i for i in server.get()["items"] if i["id"] == reply["id"])
            assert kept["measure"] == "0123456789abcdef" and kept["nozzle"] == "fedcba9876543210"
            assert server.call("/api/queue/remove", str(reply["id"]))[0] == 200
            # The estimates and the position are optional.
            status, reply = server.call("/api/queue/add", "internal\npart.gcode\n\nA\nsaved\n0\n\n\n2")
            assert status == 201 and reply["id"] == 4, (status, reply)  # id 3 was the removed measurement test
            assert [i["id"] for i in server.get()["items"]] == [2, 1, 4]

            # Order, copies and policy.
            assert server.call("/api/queue/reorder", "2\n1")[0] == 200
            assert [i["id"] for i in server.get()["items"]] == [1, 2, 4]
            assert server.call("/api/queue/reorder", "99\n0")[0] == 404
            assert server.call("/api/queue/reorder", "nonsense")[0] == 400
            assert server.call("/api/queue/copies", "1\n5")[0] == 200
            assert server.get()["items"][0]["copies"] == 5
            assert server.call("/api/queue/copies", "1\n0")[0] == 400
            assert server.call("/api/queue/copies", "99\n1")[0] == 404
            assert server.call("/api/queue/policy", "auto\n30\n1")[0] == 200
            assert server.get()["policy"] == {"after": "auto", "delay": 30, "remove_done": True}
            assert server.get()["auto_in"] is None, "no copy was counted, so no automatic start is pending"
            assert server.call("/api/queue/hold", "")[0] == 200
            assert server.call("/api/queue/abandon", "")[0] == 409, "no start is waiting to be given up"
            assert server.call("/api/queue/policy", "auto\n5\n1")[0] == 400
            assert server.call("/api/queue/policy", "sometimes\n30\n1")[0] == 400
            assert server.call("/api/queue/policy", "wait\n60\n0")[0] == 200

            # Without a printer link a start is refused by the print path itself; nothing changes.
            status, reply = server.call("/api/queue/start", "")
            assert status == 503 and reply["accepted"] is False and reply["error"] == "Printer MQTT is not ready", (status, reply)
            status, reply = server.call("/api/queue/start", "1")
            assert status == 503, (status, reply)
            assert server.call("/api/queue/start", "99")[0] == 404
            payload = server.get()
            assert payload["active"] is None and all(i["state"] == "pending" for i in payload["items"])

            # Removal; a missing file keeps its job but nothing can start.
            assert server.call("/api/queue/remove", "2")[0] == 200
            assert server.call("/api/queue/remove", "2")[0] == 404
            assert [i["id"] for i in server.get()["items"]] == [1, 4]
            (internal / "part.gcode").unlink()
            payload = server.get()
            assert all(i["present"] is False for i in payload["items"]) and payload["next"] is None
            status, reply = server.call("/api/queue/start", "")
            assert status == 409 and reply["error"] == "Nothing in the queue can start", (status, reply)
            status, reply = server.call("/api/queue/start", "1")
            assert status == 404, (status, reply)  # the print path's own answer for an unavailable file
            (internal / "part.gcode").write_text("G28\n", encoding="ascii")
            assert server.get()["next"] == 1
            assert server.call("/api/queue/clear-done", "")[0] == 200
            assert len(server.get()["items"]) == 2
        finally:
            server.stop()

        # The queue survives a restart, with its order, counts and policy.
        saved = json.loads(queue_file.read_text(encoding="utf-8"))
        assert saved["version"] == 1 and saved["next_id"] == 5 and len(saved["items"]) == 2, saved
        server = Server(binary, root, queue_file)
        try:
            payload = server.get()
            assert payload["available"] is True
            assert [(i["id"], i["copies"]) for i in payload["items"]] == [(1, 5), (4, 2)], payload["items"]
            assert payload["policy"] == {"after": "wait", "delay": 60, "remove_done": False}
            status, reply = server.call("/api/queue/add", add_body("other.gcode"))
            assert status == 201 and reply["id"] == 5
        finally:
            server.stop()

        # Finished jobs: listed until cleared, or dropped by remove_done, also when the copies are lowered to the count printed.
        queue_file.write_text(queue_file_text([
            {"id": 1, "path": "part.gcode", "copies": 2, "done": 0},
            {"id": 2, "path": "other.gcode", "copies": 1, "done": 1},
            {"id": 3, "path": "part.gcode", "copies": 3, "done": 1},
        ], policy=("wait", 60, True)), encoding="utf-8")
        server = Server(binary, root, queue_file)
        try:
            payload = server.get()
            assert [(i["id"], i["state"]) for i in payload["items"]] == [(1, "pending"), (2, "done"), (3, "pending")], payload["items"]
            assert payload["next"] == 1
            assert server.call("/api/queue/clear-done", "")[0] == 200
            assert [i["id"] for i in server.get()["items"]] == [1, 3]
            assert server.call("/api/queue/copies", "3\n1")[0] == 200
            assert [i["id"] for i in server.get()["items"]] == [1], "a job finished by its copies count is dropped with remove_done"
            assert server.call("/api/queue/policy", "wait\n60\n0")[0] == 200
            assert server.call("/api/queue/copies", "1\n5")[0] == 200
            assert server.get()["items"][0]["copies"] == 5
        finally:
            server.stop()

        # An unreadable file keeps the queue closed instead of silently emptying it.
        queue_file.write_text("{\"version\":1,", encoding="utf-8")
        server = Server(binary, root, queue_file)
        try:
            payload = server.get()
            assert payload["available"] is False and payload["error"], payload
            status, reply = server.call("/api/queue/add", add_body("other.gcode"))
            assert status == 503, (status, reply)
        finally:
            server.stop()
        assert queue_file.read_text(encoding="utf-8") == "{\"version\":1,", "a broken queue file is never rewritten"
    print("PASS: print queue API, validation, missing files and persistence")


if __name__ == "__main__":
    main()
