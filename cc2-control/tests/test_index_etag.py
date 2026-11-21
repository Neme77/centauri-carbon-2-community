#!/usr/bin/env python3
"""The start page revalidates with an ETag; every other answer stays no-store.

GET / and GET /index.html answer 200 with an ETag that is the SHA-256 of the page plus
`Cache-Control: no-cache`, and answer a matching If-None-Match with a bodyless 304. API answers and
/i18n/* keep `Cache-Control: no-store`.
"""
import hashlib
import http.client
import os
import shlex
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


binary = Path(sys.argv[1]).resolve()
runner = shlex.split(os.environ.get("CC2_TEST_RUNNER", ""))


def fetch(port, path="/", headers=(), body=None, method="GET"):
    """Raw request so header names and duplicates are sent exactly as given."""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.putrequest(method, path, skip_host=False, skip_accept_encoding=True)
        for name, value in headers:
            connection.putheader(name, value)
        if body is not None:
            connection.putheader("Content-Length", str(len(body)))
        connection.endheaders(body)
        response = connection.getresponse()
        data = response.read()
        return response.status, {k.lower(): v for k, v in response.getheaders()}, data
    finally:
        connection.close()


with tempfile.TemporaryDirectory(prefix="cc2-etag-") as temporary:
    state = Path(temporary)
    web = state / "web"
    (web / "locales").mkdir(parents=True)
    index = web / "index.html"
    index.write_text("<!doctype html><html><body>first</body></html>\n", encoding="ascii")
    (web / "locales" / "en.json").write_text('{"a":"b"}\n', encoding="ascii")
    (state / "cc2-control.conf").write_text("mqtt_username=elegoo\nmqtt_password=etag\n", encoding="ascii")
    port = free_port()
    process = subprocess.Popen([
        *runner, str(binary), "--port", str(port), "--panda-port", "0",
        "--web-root", str(web), "--config", str(state / "cc2-control.conf"),
        "--presets", str(state / "presets.json"), "--preferences", str(state / "preferences.json"),
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                fetch(port)
                break
            except OSError:
                assert process.poll() is None, f"cc2-control exited with {process.returncode}"
                time.sleep(0.1)
        else:
            raise AssertionError("cc2-control did not start listening")

        # 200 carries the validator and asks the browser to revalidate every time.
        status, headers, page = fetch(port)
        assert status == 200 and b"first" in page, f"GET / returned {status}"
        etag = headers.get("etag", "")
        assert len(etag) > 2 and etag[0] == '"' and etag[-1] == '"' and etag.count('"') == 2, f"ETag not a quoted strong tag: {etag!r}"
        assert not etag.startswith("W/"), "ETag must be strong"
        assert headers.get("cache-control") == "no-cache", f"Cache-Control is {headers.get('cache-control')!r}"
        assert headers.get("x-content-type-options") == "nosniff" and headers.get("x-frame-options") == "DENY"
        status, headers, _ = fetch(port, "/index.html")
        assert status == 200 and headers.get("etag") == etag, "/index.html must carry the same ETag as /"

        def not_modified(value, name="If-None-Match", path="/"):
            status, headers, data = fetch(port, path, [(name, value)])
            return status, headers, data

        # 304 for an exact match, a list, a weak tag, a wildcard and any header-name case.
        cases = [
            ("exact", etag, "If-None-Match"),
            ("lower-case name", etag, "if-none-match"),
            ("upper-case name", etag, "IF-NONE-MATCH"),
            ("list", f'"nope", {etag} ,"other"', "If-None-Match"),
            ("list, match first", f'{etag},"nope"', "If-None-Match"),
            ("weak", "W/" + etag, "If-None-Match"),
            ("weak in list", f'"nope", W/{etag}', "If-None-Match"),
            ("wildcard", "*", "If-None-Match"),
            ("tab padding", f"\t{etag}", "If-None-Match"),
        ]
        for label, value, name in cases:
            status, headers, data = not_modified(value, name)
            assert status == 304, f"{label}: expected 304, got {status}"
            assert data == b"", f"{label}: 304 must not have a body"
            assert headers.get("etag") == etag, f"{label}: 304 lacks the ETag"
            assert headers.get("cache-control") == "no-cache", f"{label}: 304 Cache-Control {headers.get('cache-control')!r}"
            assert headers.get("x-content-type-options") == "nosniff", f"{label}: 304 lacks nosniff"
            assert headers.get("x-frame-options") == "DENY", f"{label}: 304 lacks X-Frame-Options"
            assert headers.get("connection", "").lower() == "close", f"{label}: 304 lacks Connection: close"
            assert headers.get("content-length", "0") == "0", f"{label}: 304 Content-Length {headers.get('content-length')!r}"
        status, headers, data = not_modified(etag, path="/index.html")
        assert status == 304 and data == b"", "/index.html must also answer 304"

        # No match means a full 200 again.
        for label, value in [
            ("other tag", '"0-0.0"'),
            ("unquoted", etag.strip('"')),
            ("prefix of the tag", etag[:-2] + '"'),
            ("empty list entries", ", ,"),
            ("quote inside", '"' + etag.strip('"') + 'x"'),
            ("star inside a tag", '"*"'),
        ]:
            status, headers, data = not_modified(value)
            assert status == 200 and b"first" in data, f"{label}: expected 200, got {status}"
            assert headers.get("etag") == etag, f"{label}: 200 lacks the ETag"
        # Duplicate headers are ambiguous: answer the full page, never a 304.
        status, _, data = fetch(port, "/", [("If-None-Match", etag), ("If-None-Match", etag)])
        assert status == 200 and b"first" in data, f"duplicate If-None-Match returned {status}"
        # The header search must stop at the end of the headers: a body is not a header.
        status, _, data = fetch(port, "/", body=f"If-None-Match: {etag}\r\n".encode("ascii"))
        assert status == 200 and b"first" in data, f"If-None-Match in the body returned {status}"
        # A huge value without a match is answered with the page, not a crash.
        status, _, data = fetch(port, "/", [("If-None-Match", '"' + "a" * 10000 + '"')])
        assert status == 200 and b"first" in data, f"10000-character If-None-Match returned {status}"
        status, _, data = fetch(port, "/", [("If-None-Match", ",".join(['"a%04d"' % n for n in range(1400)]))])
        assert status == 200 and b"first" in data, f"long list returned {status}"
        assert process.poll() is None, "server died on the long header"

        # A changed file (new size) gets a new ETag; the old one no longer matches.
        index.write_text("<!doctype html><html><body>second, longer</body></html>\n", encoding="ascii")
        status, headers, data = fetch(port, "/", [("If-None-Match", etag)])
        assert status == 200 and b"second" in data, f"changed size returned {status}"
        etag_size = headers.get("etag")
        assert etag_size and etag_size != etag, "ETag unchanged after the size changed"
        # The tag belongs to the content: the same page with other file times keeps its tag and still gets 304.
        stat = index.stat()
        os.utime(index, ns=(stat.st_atime_ns, stat.st_mtime_ns + 2_000_000_000))
        status, headers, data = fetch(port, "/", [("If-None-Match", etag_size)])
        assert status == 304 and data == b"" and headers.get("etag") == etag_size, f"same content, other mtime returned {status}"
        os.utime(index, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000))
        status, headers, _ = fetch(port, "/")
        assert status == 200 and headers.get("etag") == etag_size, "a change of the file time alone must not change the ETag"
        assert etag_size == '"' + hashlib.sha256(index.read_bytes()).hexdigest() + '"', "ETag is not the SHA-256 of the page"

        # Same size, other content, modification time put back (a copy that keeps times, tar, touch -r): the tag follows
        # the content, so the old tag gets the new page. (A switch of releases that also gives equal change time and inode
        # is tested in test_index_validator.c, where the file metadata can be forced.)
        index.write_text("<!doctype html><html><body>third, size 1</body></html>\n", encoding="ascii")
        status, headers, data = fetch(port)
        etag_third = headers.get("etag")
        assert status == 200 and b"size 1" in data and etag_third, f"setup for the restored time returned {status}"
        stat = index.stat()
        time.sleep(0.1)  # the change time moves in clock ticks of a few milliseconds
        index.write_text("<!doctype html><html><body>third, size 2</body></html>\n", encoding="ascii")
        os.utime(index, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        assert index.stat().st_mtime_ns == stat.st_mtime_ns and index.stat().st_size == stat.st_size
        status, headers, data = fetch(port, "/", [("If-None-Match", etag_third)])
        assert status == 200 and b"size 2" in data, f"same size and restored mtime returned {status}: the old page would stay in the browser"
        assert headers.get("etag") != etag_third, "ETag unchanged although the content changed and the mtime was restored"
        etag_third = headers.get("etag")
        status, _, data = fetch(port, "/", [("If-None-Match", etag_third)])
        assert status == 304 and data == b"", "unchanged file must answer 304 again after the restored time"

        # The page is rewritten in place while it is requested: whatever is sent, the tag is the SHA-256 of the
        # body that was sent (a tag never describes other bytes). Statistical: the window is a few microseconds,
        # so this runs many requests against a writer. A page that changed or was shorter than measured is answered with a 503 and
        # a JSON error, never with a silent close and never with a tag.
        size_a, size_b = 300000, 450000
        index.write_bytes(b"a" * size_a)
        stop = threading.Event()

        def rewrite():
            # Bursts of rewriting, then a quiet pause: a slow runner (ThreadSanitizer, an emulator) can finish reads in the pauses.
            with open(index, "r+b", buffering=0) as handle:
                while not stop.is_set():
                    burst_end = time.monotonic() + 0.04
                    while time.monotonic() < burst_end and not stop.is_set():
                        handle.truncate(0)  # like `cat page > index.html`: the file is empty for a moment
                        handle.seek(0)
                        handle.write(b"b" * size_b)
                        handle.truncate(size_a)
                        handle.seek(0)
                        handle.write(b"a" * size_a)
                    time.sleep(0.06)

        mismatches = []
        completed = [0]
        replaced = [0]
        shortened = [0]  # 200 answers that are neither of the two pages: a cut-off copy, see the note below

        def request_loop(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                try:
                    status, headers, data = fetch(port)
                except (OSError, http.client.HTTPException):
                    mismatches.append("closed without an answer")
                    continue
                if status == 503:
                    replaced[0] += 1
                    if data != b'{"error":"Start page is being replaced"}\n' or "etag" in headers or headers.get("cache-control") != "no-store":
                        mismatches.append(f"503 answer {headers} {data[:60]!r}")
                    continue
                if status != 200:
                    mismatches.append(f"status {status}")
                    continue
                completed[0] += 1
                if not data:
                    mismatches.append("200 with an empty body: an empty start page is never a finished page")
                    continue
                if len(data) not in (size_a, size_b):
                    shortened[0] += 1  # best effort, not asserted: see the note on the 503 check in serve_index()
                tag = headers.get("etag", "")
                if tag != '"' + hashlib.sha256(data).hexdigest() + '"':
                    mismatches.append(f"ETag {tag} is not the SHA-256 of the {len(data)} bytes sent")

        writer = threading.Thread(target=rewrite)
        clients = [threading.Thread(target=request_loop, args=(3,)) for _ in range(4)]
        writer.start()
        for client in clients:
            client.start()
        for client in clients:
            client.join()
        stop.set()
        writer.join()
        assert process.poll() is None, "server died while the page was rewritten"
        assert not mismatches, f"wrong answers in {len(mismatches)} of {completed[0] + replaced[0] + len(mismatches)} requests ({replaced[0]} were 503): {mismatches[:3]}"
        assert completed[0] > 0, "no request completed while the page was rewritten"
        print(f"info: {completed[0]} pages ({shortened[0]} of them cut off) and {replaced[0]} 503 answers while the page was rewritten")
        # A page that is empty at the moment it is read (truncated, the new content not written yet) is a page that is being
        # replaced too: 503 with the JSON error, no tag, never a 200 with an empty body (a browser would show a white page
        # and keep it until the next visit).
        index.write_bytes(b"")
        for headers_in in ([], [("If-None-Match", "*")]):
            status, headers, data = fetch(port, "/", headers_in)
            assert status == 503, f"empty start page returned {status} (headers {headers_in})"
            assert data == b'{"error":"Start page is being replaced"}\n', f"empty start page body {data[:60]!r}"
            assert "etag" not in headers and headers.get("cache-control") == "no-store", f"empty start page headers {headers}"
        status, _, data = fetch(port, "/index.html")
        assert status == 503 and data == b'{"error":"Start page is being replaced"}\n', f"/index.html empty returned {status}"
        index.write_text("<!doctype html><html><body>after the rewrite</body></html>\n", encoding="ascii")

        # Everything else keeps no-store and carries no ETag.
        for path in ("/api/version", "/api/health", "/i18n/en.json", "/definitely-not-a-route"):
            status, headers, _ = fetch(port, path, [("If-None-Match", "*")])
            assert status != 304, f"{path}: If-None-Match must not produce a 304"
            assert headers.get("cache-control") == "no-store", f"{path}: Cache-Control {headers.get('cache-control')!r}"
            assert "etag" not in headers, f"{path}: unexpected ETag"
        status, headers, data = fetch(port, "/i18n/en.json")
        assert status == 200 and b'"a"' in data, "locale file not served"

        # A missing page stays the plain error answer: no ETag, no revalidation.
        missing = web / "index.html"
        missing.unlink()
        status, headers, _ = fetch(port, "/", [("If-None-Match", "*")])
        assert status == 500, f"missing index.html returned {status}"
        assert "etag" not in headers and headers.get("cache-control") == "no-store", f"error answer headers: {headers}"
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

print("PASS: test_index_etag.py")
