# Verification and emulation: how to run it, what the results mean

All four scripts below are **read-only**: they load a binary (into a Python
byte buffer or a Unicorn emulator instance) and print a result; none of them
writes to any file or touches a device. Full captured output for the
corrected binaries is in `VERIFY_OUTPUT.txt`, `EMULATE_E1_OUTPUT.txt`,
`EMULATE_E2_OUTPUT.txt`, `EMULATE_E345_OUTPUT.txt`, `EMULATE_E6_OUTPUT.txt`.

## 1. `verify_patched.py` — static branch/placement verification

```sh
python3 -I verify_patched.py PATH/TO/elegoo_printer.patched
```

Decodes the ARM instruction at every one of the 32 branch/placement sites
(17 payload-internal + 12 payload-reference + 2 cross-function + 1 "payload
not empty" check — see `PATCH_SITES.md` sections 2/5/6) plus the 10 verbatim
sites, and compares the decoded branch target (or raw bytes, for verbatim)
against the specification in `PAYLOAD_BASE_FIXATION_AND_BRANCH_PLAN.md`.

**Meaning of "42 PASS, 0 FAIL":** every branch in the patched binary points
exactly where the port's specification says it should, and every verbatim
byte range matches its manifest source. It does **not** mean the branches'
*targets* do the right thing at runtime, or that the payload *body* (the
code between branches) is intact — that is a content check, not covered
here. See `OPEN_QUESTIONS.md` point 0 for why this distinction matters: the
first manually-built `elegoo_printer.k1` passed this same 42/42 check while
having its payload body almost entirely zeroed.

A `FAIL` means one specific site's branch target or verbatim bytes don't
match — the failing line names the exact address and the expected vs. actual
value.

## 2. Emulation harnesses (Unicorn ARM emulator; vendor functions intercepted)

Each loads only the relevant binary's `PT_LOAD` segments into a Unicorn `Uc`
instance, runs a short instruction sequence from a specific entry point, and
checks where execution actually went. Calls into vendor functions outside
the tested region are intercepted and returned from immediately (so the test
does not need the rest of the binary's runtime environment).

```sh
python3 emulate_e1.py PATH/TO/ec-eeb001-gui.patched
python3 emulate_e2.py PATH/TO/ai_camera.patched
python3 emulate_e345.py PATH/TO/elegoo_printer.patched
python3 emulate_e6.py PATH/TO/elegoo_printer.patched
```

| Harness | Entry point | What it checks | PASS means | FAIL would mean |
|---|---|---|---|---|
| `emulate_e1.py` | `0x747c8` (GUI patch site) | Reaches `0x74bd0` (Z-offset menu) and never calls `data_get_print_status` (`0xae640`) | The print-status gate is skipped unconditionally | GUI still gates the Z-offset menu on print status |
| `emulate_e2.py` | `0x13287c` (camera patch site) | First call made is `resume` (`0x16a7f8`), not `pause` (`0x16a748`) | Camera coexistence redirect works | Camera still calls `pause` |
| `emulate_e345.py` | 3 `set_status`/`set_sub_status` guards + the cancel path, all 32 machine states | Each guard only lets the allowed states through (1/11, or 11 only); the member read is at `0xbbc`, not the stale `0xbb4`; cancel in a non-idle/transfer state sets HTTP 409 (`R4==409`) without reaching the delete path | 128/128 PASS = all 32 states × 4 code paths behave as specified | Any one state/path combination reaching the wrong branch target |
| `emulate_e6.py` | `0x826924` (`LanStrategy::stop`, 184 B) | `stop_http_server_file` (`0x827148`) is never called; the function still runs to its return; at least one other call (e.g. `0x8272c8` = `stop_mqtt_client`) still happens | HTTP server survives a LAN stop, MQTT/other teardown still runs | Either HTTP stop still fires, or nothing else runs (function broken) |

### Confirmed results (corrected binaries, re-run 2026-11-08)

| Case | Result |
|---|---|
| E1 (GUI) | **PASS** — reaches `0x74bd0`, `data_get_print_status` not called |
| E2 (camera) | **PASS** — first call is `0x16a7f8` (`resume`) |
| E3/E4/E5 (HTTP guards + `0xbbc`) | **128 PASS, 0 FAIL** |
| E6 (LanStrategy NOP) | **PASS** — HTTP stop skipped, 7 other calls still happen (incl. MQTT stop) |

All four were re-run against `WORK/corrected/elegoo_printer.patched` (not
just the earlier `elegoo_printer.k1`) after the payload fix in
`OPEN_QUESTIONS.md` point 0 — none of these four exercises the payload body
directly, so the results are identical to before the fix, but this was
checked rather than assumed.

## 3. Hard limits of this emulation — read before concluding anything

- **Vendor functions are intercepted, not executed.** A call to
  `DeviceStatus::getInstance`, `remove_client`, `stop_mqtt_client`, etc. is
  short-circuited; their real behavior (and anything they might do that
  changes register/memory state the test depends on) is not exercised.
- **No real HTTP, network, MQTT, or camera I/O happens.** The emulator never
  opens a socket, talks to hardware, or runs a second thread.
- **No UI, no boot, no printer firmware init.** GUI and camera emulation
  tests one function each, from a cold `Uc` instance with hand-set
  registers — not the running application.
- **The payload *body* content (everything that isn't one of the 17
  branches) is not emulated by any of these four harnesses** — only its
  wiring (branch targets) is checked, by `verify_patched.py`, and only its
  *content* is checked by `apply_patches.py`'s sourcing from the real
  manifest (see point 0 in `OPEN_QUESTIONS.md`). No harness currently runs
  the coexistence payload end-to-end.

**"Emulation PASS" proves control-flow correctness for the specific
instructions each harness touches. It does not prove the firmware works
correctly on the printer.** Hardware test remains required before any
release decision — see `HARDWARE_TEST_PLAN.md` and `ROLLBACK_PLAN.md`.
