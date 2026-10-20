# CC2 Camera Recovery R2 — experimental tester package

This trial combines video-client thread reclamation, addressing the observed accumulation, with video-control recovery after restarting only the camera service. It preserves the tested one-time LAN enable. These are the exact binaries used in two successful consecutive camera-only restarts, both restoring local and Matrix video.

This is a temporary beta for a small tester group. Extended operation, AI functions and expired cloud sessions are not fully qualified. It is not a watchdog: it does not automatically restart a stuck camera. It does not change firmware, elegoo_printer, CC2 Control or system startup. Activation ends on a full printer reboot.

## Compatibility and baseline

- Community Firmware V4.4/V4.4.1, official base ELEGOO 02.01.01.07, with the exact original camera checksum accepted by the script. Do not use on other firmware.
- Idle printer, SSH available, and CC2 Control on port 8081 with fresh telemetry.
- Verify local and Matrix video from normal startup first. If Matrix is already unavailable, record the cloud issue rather than attributing it to this patch.
- If Recovery R1 was active, reboot normally and verify both videos. R2 refuses a still-loaded R1 recovery library.
- Give each tester an ID: T1, T2 or T3. Never put passwords in reports.

## 1. Copy from the computer

Fully extract the ZIP. On Windows, open `Copy-to-printer.cmd`: it asks for the printer IPv4 address, verifies both transferred files and copies them to `/tmp/`. Normal SSH host-key confirmation and the root password may be requested. This only copies files; it does not activate the patch.

Alternatively, in a computer terminal in the extracted folder, replace `PRINTER_IP` with the actual address:

```sh
scp -O cc2-camera-recovery-r2.tar.gz cc2-camera-tester-report.sh root@PRINTER_IP:/tmp/
```

Do not upload this archive through the CC2 Control updater. It is a separate diagnostic package.

## 2. Prepare over SSH — run commands separately

```sh
mkdir -p /tmp/cc2-camera-recovery-r2
```

```sh
tar -xzf /tmp/cc2-camera-recovery-r2.tar.gz -C /tmp/cc2-camera-recovery-r2
```

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh check
```

The final line must say `CHECK PASS`. This does not restart processes. If refused, save the output and stop; do not bypass checksum or compatibility checks.

Record the baseline, replacing T1 with your ID:

```sh
sh /tmp/cc2-camera-tester-report.sh T1 baseline
```

## 3. First activation — idle printer only

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh start
```

This restarts only ai_camera and loads both libraries. Video will disconnect during restart. Wait about 15 seconds, then check local and Matrix video; close and reopen the viewer once if necessary. Use one remote client to distinguish recovery from latency with multiple viewers.

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh status
```

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-start
```

Report local and Matrix results separately. Loaded libraries and counters alone do not prove video availability. The successful test showed `ready=1`, `initial_calls=1`, `initial_returns=1`, `connections=2`, `announced=2`, `failed=0`; values can depend on connected control clients.

## 4. Repeatability — only after the first success

With the printer still idle, perform one second camera-only restart:

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh restart
```

Verify both videos again, then:

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-restart
```

After any failure, do not retry automatically. Collect an `issue` report, describe the symptoms and restore normal startup while idle.

## 5. Ordinary use and feedback

After two successes, observe ordinary use. Collect a report after 30 minutes and at the end, noting the duration in `TESTER-REPORT.md`. Do not run connection loops or stress tests. Do not restart the camera during printing. An ordinary longer print is a later check, after the idle-printer tests succeed.

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-30m
```

The report is displayed and saved to a small `/tmp/cc2-camera-report-...txt` file. It collects PIDs, memory, counters and control-socket counts, without HTTP requests or service changes. `spawned=0`/`reaped=0` alone is not a failure: that path may not have been exercised. For AI, name the function actually observed; otherwise write `NOT VERIFIED`.

Return `TESTER-REPORT.md` with the small text reports. Do not publish `ai_camera-trial.log` or full camera logs: they may contain session data. WAN latency and Wi-Fi throughput remain separate investigations.

## Rollback

With the printer idle, a normal full reboot removes this in-memory activation:

```sh
reboot
```

Verify both videos again afterwards. `/tmp/` files normally disappear at reboot. This package neither edits the existing startup configuration nor rearms an earlier one-shot experiment. Any separately configured persistent modifications remain as they were.

`SHA256SUMS.txt` checks file integrity; it is not an authenticity signature. `validation/` contains binary provenance and the two real-printer results; `sources/` contains original patch sources. No vendor firmware, private keys or credentials are included.

Thanks to **tharje** for the diagnosis and captures, and to the beta testers for real local and remote validation.
