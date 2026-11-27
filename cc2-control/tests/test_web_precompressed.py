#!/usr/bin/env python3
"""The start page and the locale files are sent as their gzip copy (written by the installer) to a browser that accepts gzip.

GET / and GET /i18n/<code>.json send web/index.html.gz or web/locales/<code>.json.gz with
`Content-Encoding: gzip` when Accept-Encoding takes gzip and the copy unpacks to exactly the file
(CRC-32 and length in its trailer). Every other case, a missing, stale, cut-off or larger copy included,
gets the plain file as before. Both forms carry `Vary: Accept-Encoding`; the start page keeps its ETag
(the SHA-256 of the bytes sent) and 304, the locale files keep `Cache-Control: no-store`, and the size
limits still apply to the plain files.
"""
import gzip
import hashlib
import http.client
import os
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


binary = Path(sys.argv[1]).resolve()
runner = shlex.split(os.environ.get("CC2_TEST_RUNNER", ""))


def fetch(port, path="/", headers=()):
    """Raw request so Accept-Encoding and duplicates are sent exactly as given (none unless listed)."""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.putrequest("GET", path, skip_host=False, skip_accept_encoding=True)
        for name, value in headers:
            connection.putheader(name, value)
        connection.endheaders()
        response = connection.getresponse()
        data = response.read()
        return response.status, {k.lower(): v for k, v in response.getheaders()}, data
    finally:
        connection.close()


def packed(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def tag(data):
    return '"' + hashlib.sha256(data).hexdigest() + '"'


PAGE = b"<!doctype html><html><body>" + b"cc2 control page " * 4000 + b"</body></html>\n"
LOCALE = b'{"common.ok":"' + "ОК ".encode() * 3000 + b'"}\n'
GZIP = (("Accept-Encoding", "gzip, deflate, br"),)


def expect_plain(port, path, plain, headers, what):
    status, got, data = fetch(port, path, headers)
    assert status == 200 and data == plain, f"{what}: {status}, {len(data)} bytes"
    assert "content-encoding" not in got, f"{what}: Content-Encoding {got.get('content-encoding')!r}"
    assert got.get("vary") == "Accept-Encoding", f"{what}: Vary {got.get('vary')!r}"
    assert got.get("content-length") == str(len(plain)), f"{what}: Content-Length {got.get('content-length')!r}"
    return got


def expect_gzip(port, path, plain, gz, headers, what):
    status, got, data = fetch(port, path, headers)
    assert status == 200 and got.get("content-encoding") == "gzip", f"{what}: {status} {got}"
    assert data == gz and gzip.decompress(data) == plain, f"{what}: body is not the gzip copy of the file"
    assert got.get("content-length") == str(len(gz)) and got.get("vary") == "Accept-Encoding", f"{what}: {got}"
    return got


with tempfile.TemporaryDirectory(prefix="cc2-gzip-") as temporary:
    state = Path(temporary)
    web = state / "web"
    (web / "locales").mkdir(parents=True)
    index, index_gz = web / "index.html", web / "index.html.gz"
    locale, locale_gz = web / "locales" / "ru.json", web / "locales" / "ru.json.gz"
    index.write_bytes(PAGE)
    index_gz.write_bytes(packed(PAGE))
    locale.write_bytes(LOCALE)
    locale_gz.write_bytes(packed(LOCALE))
    (web / "locales" / "en.json").write_text('{"a":"b"}\n', encoding="ascii")
    (state / "cc2-control.conf").write_text("mqtt_username=elegoo\nmqtt_password=gzip\n", encoding="ascii")
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

        gz = index_gz.read_bytes()
        # Without Accept-Encoding nothing changes but Vary: the plain page and its own tag.
        got = expect_plain(port, "/", PAGE, (), "GET / without Accept-Encoding")
        assert got.get("etag") == tag(PAGE) and got.get("cache-control") == "no-cache", got
        # With gzip: the gzip copy, tagged by its own bytes, with every header the plain page has.
        for path in ("/", "/index.html"):
            got = expect_gzip(port, path, PAGE, gz, GZIP, f"GET {path} with gzip")
            assert got.get("etag") == tag(gz) != tag(PAGE), f"{path}: ETag {got.get('etag')!r}"
            assert got.get("cache-control") == "no-cache" and got.get("content-type") == "text/html; charset=utf-8", got
            assert got.get("x-content-type-options") == "nosniff" and got.get("x-frame-options") == "DENY", got
            assert got.get("connection") == "close", got
        # The tag of the copy revalidates the copy; the tag of the plain page does not (and vice versa).
        status, got, data = fetch(port, "/", GZIP + (("If-None-Match", tag(gz)),))
        assert status == 304 and data == b"" and got.get("etag") == tag(gz) and got.get("vary") == "Accept-Encoding", got
        assert "content-encoding" not in got and "content-length" not in got, got
        assert got.get("x-content-type-options") == "nosniff" and got.get("x-frame-options") == "DENY", got
        expect_gzip(port, "/", PAGE, gz, GZIP + (("If-None-Match", tag(PAGE)),), "plain tag sent with gzip")
        expect_plain(port, "/", PAGE, (("If-None-Match", tag(gz)),), "gzip tag sent without gzip")
        status, got, _ = fetch(port, "/", (("If-None-Match", tag(PAGE)),))
        assert status == 304 and got.get("etag") == tag(PAGE), "plain tag must still revalidate the plain page"

        # Accept-Encoding: which values take gzip.
        for value in ("gzip", "GZIP", "x-gzip", "deflate, gzip;q=0.5", "*", "br;q=1, *;q=0.1", "gzip ;q=1",
                      "identity, gzip", "gzip;q=0.001"):
            expect_gzip(port, "/", PAGE, gz, (("Accept-Encoding", value),), f"Accept-Encoding {value!r}")
        for value in ("identity", "br", "deflate", "gzip;q=0", "gzip; q=0.000", "GZIP;Q=0", "*;q=0", "gzip;q=0, *",
                      "x-gzip;q=0", "*, gzip;q=0", "gzipx", "gzi", "g zip", "," * 20):
            expect_plain(port, "/", PAGE, (("Accept-Encoding", value),), f"Accept-Encoding {value!r}")
        # A repeated or oversized header is not read: the plain page is always a valid answer.
        expect_plain(port, "/", PAGE, (("Accept-Encoding", "gzip"), ("Accept-Encoding", "gzip")), "repeated header")
        expect_plain(port, "/", PAGE, (("Accept-Encoding", "br, " * 200 + "gzip"),), "oversized header")

        # Locale files: the same choice, no-store kept, no ETag.
        locale_packed = locale_gz.read_bytes()
        got = expect_gzip(port, "/i18n/ru.json", LOCALE, locale_packed, GZIP, "locale with gzip")
        assert got.get("cache-control") == "no-store" and "etag" not in got, got
        assert got.get("content-type") == "application/json; charset=utf-8", got
        assert got.get("x-content-type-options") == "nosniff" and got.get("x-frame-options") == "DENY", got
        got = expect_plain(port, "/i18n/ru.json", LOCALE, (), "locale without gzip")
        assert got.get("cache-control") == "no-store" and "etag" not in got, got
        # A locale without a copy (older installation) is sent plain to a gzip browser.
        expect_plain(port, "/i18n/en.json", b'{"a":"b"}\n', GZIP, "locale without copy")

        # Copies that cannot stand in for the file: the plain file, never a wrong page.
        def page_with(copy, what, page=PAGE):
            index.write_bytes(page)
            if copy is None:
                index_gz.unlink(missing_ok=True)
            else:
                index_gz.write_bytes(copy)
            expect_plain(port, "/", page, GZIP, what)

        page_with(None, "no copy (installation from before the copies)")
        page_with(packed(b"<!doctype html><html><body>old release</body></html>\n"), "stale copy of another page")
        page_with(packed(PAGE)[:-1], "cut-off copy")
        page_with(packed(PAGE)[: len(packed(PAGE)) // 2], "copy cut off in the middle")
        page_with(b"\x1f\x8b\x08", "copy shorter than a gzip member")
        page_with(b"", "empty copy")
        page_with(b"PK\x03\x04" + packed(PAGE)[4:], "copy without the gzip magic")
        changed = PAGE.replace(b"page", b"PAGE", 1)
        page_with(packed(PAGE), "page changed, copy still the old one", changed)
        small = b"<html>tiny</html>\n"
        page_with(gzip.compress(small, mtime=0), "copy not smaller than the page", small)
        index_gz.unlink()
        index_gz.mkdir()
        expect_plain(port, "/", small, GZIP, "copy is a directory")
        index_gz.rmdir()
        # A copy written after the page: served once it fits again.
        index.write_bytes(changed)
        index_gz.write_bytes(packed(changed))
        expect_gzip(port, "/", changed, packed(changed), GZIP, "rebuilt copy")

        locale_gz.write_bytes(packed(b'{"common.ok":"old"}\n'))
        expect_plain(port, "/i18n/ru.json", LOCALE, GZIP, "stale locale copy")

        # The limits stay on the plain files: a page above 1 MiB is refused even with a small copy.
        big = b"<html>" + b"a" * (1024 * 1024) + b"</html>"
        index.write_bytes(big)
        index_gz.write_bytes(packed(big))
        status, got, data = fetch(port, "/", GZIP)
        assert status == 500 and "content-encoding" not in got, f"oversized page with copy: {status} {got}"
        big_locale = b'{"a":"' + b"b" * 262144 + b'"}'
        locale.write_bytes(big_locale)
        locale_gz.write_bytes(packed(big_locale))
        status, got, data = fetch(port, "/i18n/ru.json", GZIP)
        assert status == 500 and "content-encoding" not in got, f"oversized locale with copy: {status} {got}"
        assert process.poll() is None, "cc2-control exited during the test"
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

print("PASS: test_web_precompressed.py")
