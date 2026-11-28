# Architecture

CC2 Control is a small static service designed for the resources available on the Centauri Carbon 2.

## Runtime components

| Component     | Responsibility                                            |
| ------------- | --------------------------------------------------------- |
| `cc2-control` | HTTP API, static UI, printer state and safety policy      |
| MQTT client   | Printer telemetry and supported control requests          |
| Panda bridge  | Compatibility endpoints on port 7125                      |
| Web UI        | Dashboard, files, Canvas, levelling, console and settings |
| procd service | Startup ordering, supervision and clean restart           |

The backend is written in C and serves the single-page UI from `cc2-control/web/index.html`, with its translations loaded from `cc2-control/web/locales/<code>.json` at `/i18n/<code>.json`. The backend reads each of these files whole and sends nothing above a fixed size: 1 MiB for `index.html`, 256 KiB for a locale file (`WEB_INDEX_MAX_BYTES` and `WEB_LOCALE_MAX_BYTES` in `cc2-control/src/main.c`; the page refuses a locale above 262144 characters, too). A larger file is answered with HTTP 500 and a plain-text reason, and `cc2-control/tests/test_web_size_limits_static.py` fails while the files are still below that (940 KiB and 240 KiB). When it installs these files, `cc2-control/installer/install-on-printer.sh` also writes a gzip copy of each (`index.html.gz`, `locales/<code>.json.gz`; none if `gzip` is missing or fails); a browser that accepts gzip gets the copy with `Content-Encoding: gzip`, but only while its gzip trailer names the CRC-32 and length of the installed file and its payload unpacks to exactly that (`cc2-control/src/gzip_check.h`, once per content of the copy), so a missing, stale or damaged copy (for example after a restore or an older installer) falls back to the file itself. The service never compresses anything itself, and the size limits apply to the plain files. Persistent state belongs under `/opt/usr/cc2-control`; immutable application files belong under `/opt/inst/cc2-control`.

## Safety boundary

Routine operations use explicit backend actions. Arbitrary G-code remains protected, dangerous operations require confirmation and printer-state guards are enforced server-side rather than relying only on disabled UI controls.

## First-run setup

The LAN access code is stored only on the printer. After the first successful registration, CC2 Control exits cleanly and procd restarts that service alone. Printer firmware, Klipper, Canvas and the MQTT broker are not restarted.
