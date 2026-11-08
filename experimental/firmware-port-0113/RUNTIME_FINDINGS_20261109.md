# Runtime findings, 2026-11-09 (hardware observation, no write actions)

Follow-up to `RUNTIME_FINDINGS_20261108.md`, reported by the user from the
same live device. **No hash value was changed, no file was written to the
device, and no 02.01.01.13 update was applied as part of this.**

## 1. Prior OOM root cause identified

`/tmp/cc2_port_test/backup` held copies of `elegoo_printer`, `ai_camera`,
`ec-eeb001-gui` (~35.5 MB). `/tmp` is tmpfs, so this consumed real RAM: with
`Shmem` at 35,668 kB the device had only ~21.9 MB `MemAvailable` and no
swap, which is what the kernel OOM-killer (`ec-eeb001-gui`, PID 7761, see
`RUNTIME_FINDINGS_20261108.md` §6) was responding to. Deleting
`/tmp/cc2_port_test` dropped `Shmem` to 148 kB and raised `MemAvailable` to
~57 MB. This resolves the "root cause not identified" open item from the
2026-11-08 finding — it was a one-off tmpfs backup, not a leak in any
running binary.

## 2. CC2 Control is now reachable; printer/camera/GUI stable

After the `/tmp` cleanup, `/etc/init.d/cc2-control start` brought up
`cc2-control` (PID 8611) on port 8081 via procd, and the browser UI at
`http://192.168.2.220:8081/` connects after entering the printer's LAN code.
`elegoo_printer` (PID 7793, port 80, `/` → 404 as expected for the native
HTTP service), `ai_camera` (PID 7762, MJPEG on port 8080), and
`ec-eeb001-gui` are all running. This confirms CC2 Control's own web
service is independent of the reactor/camera hook fallback described in
`RUNTIME_FINDINGS_20261108.md` — it runs as its own procd service on 8081,
not through either hooked binary.

## 3. Hash provenance confirmed: these are this repo's own port output, not an external release

Searching this repository for the three installed hashes
(`elegoo_printer` `688b7bf8…`, `ai_camera` `90da7634…`, `ec-eeb001-gui`
`668ab8cb…`) finds them only inside
`experimental/firmware-port-0113/` (`README_PORT.md`,
`RUNTIME_FINDINGS_20261108.md`, `WORK/corrected_v2/report.json`, etc.) —
they do not appear in `builder/current/base_profiles/*.json` or any
release/changelog file. This directly answers the "identify the current
community firmware source/release" question raised in the 2026-11-09
status note: **there is no separate external release installed here** —
the running binaries are this repository's own local, not-yet-qualified
02.01.01.13 port (`ported_patch_set` in
`builder/current/base_profiles/02.01.01.13.json` is still `false`, matching
`README_PORT.md`'s Status section).

## 4. Carried forward, unchanged

- Reactor and camera hooks remain in native fallback, exactly as in
  `RUNTIME_FINDINGS_20261108.md` — the new CC2 Control availability on 8081
  does not change this; it is a separate service.
- No hash constant was changed, no file was written to
  `/opt/bin`/`/opt/usr`/`/opt/inst` on the device, and no OTA/update to
  02.01.01.13 (or any other base) was applied as part of this finding.
- `/tmp` tmpfs memory pressure (no swap present) stays a standing
  operational risk for any future on-device backup/test workflow — avoid
  staging large binary copies there.
