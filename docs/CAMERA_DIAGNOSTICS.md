# Camera diagnostics: safe first steps (CC2 / ai_camera)

This guide concerns the CC2 camera's **vendor MJPEG streamer** (`ai_camera`,
usually port **8080**) on Community Firmware V4.4/V4.4.1. It complements
[issue #160](https://github.com/Neme77/centauri-carbon-2-community/issues/160)
and the [CC2 Control guide](CC2_CONTROL.md). **It does not fix the streamer.**

## First: rule out normal viewer behavior

1. Check whether CC2 Control has paused the view or handed camera ownership
   to another tab, device, or pop-out window. Use **Watch here** when offered;
   close unused viewers. The stream's ownership controls are intentional.
2. If the browser page is responsive but its image is frozen, avoid assuming
   that the entire printer is frozen. Check the printer status separately.
3. Record the installed CC2 Control version, V4.4/V4.4.1 family, approximate
   camera uptime, number of viewers, and whether the symptom follows Wi-Fi
   reconnection or many browser visits. Use the manual **Settings → About →
   Download diagnostics** report if available; review it before sharing.
4. Do **not** repeatedly reconnect as a diagnostic, run a connection stress
   test, reset USB, kill/restart vendor processes, or deliberately interrupt
   Wi-Fi during a print. High connection turnover has reproduced a
   resource-exhaustion crash in the affected vendor streamer.

## Distinguish the symptom, without modifying the printer

| Observation | What it means (and what it does not mean) |
| --- | --- |
| CC2 says the view is paused/owned elsewhere | Usually an intentional UI state, **not** evidence of `ai_camera` failure. |
| Port 8080 accepts a connection, but images arrive slowly | The HTTP socket can still work while frame delivery or Wi-Fi transmission is slow; this alone does **not** identify the cause. |
| Port 8080 refuses a new connection, process absent | A crashed/stopped camera process is possible; preserve timestamps and available logs for maintainers. |
| Rising `VmSize` with low `VmRSS` | Could reflect retained **virtual** thread-stack mappings. `VmSize` is *not* physical RAM usage or proof of an OOM. |
| CC2 camera works after reboot | Does not establish whether the original problem was the browser, network, or vendor process. |

**Advanced, read-only checks, only if you already have authorized SSH access:**

```sh
P="$(pidof ai_camera)"
if [ -z "$P" ]; then
  echo 'ai_camera is not running'
else
  # Device-wide process details only; do not publish a raw process map.
  grep -E '^(VmSize|VmRSS|VmData|Threads):' "/proc/$P/status"
  printf 'open_fds='
  ls "/proc/$P/fd" | wc -l
  printf 'memory_mapping_count='
  wc -l < "/proc/$P/maps"
fi
```

These commands **only read** process metadata. Run them once if the problem
occurs; repeated probing or automated high-frequency polling is unnecessary.
Report counts, approximate times and whether the image was slow, blank or
connection-refused. Do not share the complete `/proc/PID/maps`, core dumps,
system logs or shell histories without checking for device identifiers,
addresses, credentials and private filenames.

If the printer is **idle** and you need a single bounded network throughput
observation, this command runs on a **computer on the same LAN** (not on the
printer); replace the address locally and do not publish it:

```sh
curl --max-time 10 --silent --output /dev/null \
  --write-out 'HTTP %{http_code}, received %{size_download} bytes in %{time_total} s\n' \
  http://PRINTER_LAN_IP:8080/mjpeg
```

A stream is intentionally endless; the ten-second timeout is expected.
One measurement cannot prove a thread leak. Avoid frequent repeats, especially
while printing. A good ping does not establish good Wi-Fi *throughput*.

## Known investigation and patch status

As documented in [issue #160](https://github.com/Neme77/centauri-carbon-2-community/issues/160),
a reproducible vendor `ai_camera` crash involved retained client-thread
resources and an uncaught `std::system_error` while creating a client
thread. An **experimental R2 test** survived 450 connection cycles with stable
virtual memory and 20 FPS on one CC2; it is **not** the same as an official
signed firmware/application release. Cloud/AI paths and unrelated slow Wi-Fi
transmission require separate validation. Follow the issue for the maintainer's
current test and rollback guidance; this document intentionally provides no
instructions to install the experimental binary.

**Do not assume that updating CC2 Control alone patches `ai_camera`:**
a signed application-only update and a patch to the vendor camera executable
are different things.

## Safe support report

Include: CC2 Control version, Community Firmware version, approximate event
time and printer uptime, whether a viewer was paused or owned by another tab,
one observed connection symptom, and any *sanitized* metadata counts above.
Also note whether a second stream/viewer was connected. Omit LAN access code,
SSH password, tokens, device serial/MAC, private IP, filenames and raw core
dumps. Never upload a diagnostic report without reviewing its contents.
