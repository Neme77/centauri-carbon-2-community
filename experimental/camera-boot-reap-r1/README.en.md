# Camera one-boot trial — Boot R1 using the unchanged R2 library

**Experimental, for the exact qualified printer startup only. Not a CC2 Control update or a stable release.**

This loads R2 at the normal camera startup position, preserving vendor LAN/RTC initialization. It does not enable LAN early: the activation marker must report **enable_lan_once=0**. Local video and Matrix/RTC must be validated together. AI and long prints are separate qualification areas.

Strict targets: Community V4.4/V4.4.1, ELEGOO base 02.01.01.07; camera SHA256 `ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99`; `/etc/init.d/printer` SHA256 `bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563`. Different builds are refused. The tested printer has writable overlay storage; never bypass checks or force a remount.

## Scope and one-shot behavior

Staging backs up the exact startup script to `/opt/usr/cc2-camera-reap-boot-r1/printer.original` and replaces only its `ai_camera &` call with a launcher at the same sequence position. No vendor executable, callback, MQTT, motion, heater or CC2 Control is changed. Staging restarts nothing. If the persistent payload is absent, the modified startup calls the native camera directly.

Activation is armed for **one boot only** and consumed before trying R2. A following full printer reboot uses the original camera through the unarmed wrapper. The startup hook and trial files remain until rollback. Failed preliminary payload checks fall back to the original camera. Failures after activation are not hidden by automatic camera restarts.

R2 reclaims completed client threads and keeps the native four-active-client limit. The previous local tests, including tharje's 450 connections with flat virtual memory, do not qualify this new boot integration or Matrix/RTC. Restarting the original camera alone already reproduced missing Matrix video; this package does not implement recovery of that mid-session initialization.

## Copy and read-only preflight

Keep the printer idle with no print, calibration or update running. Confirm original Matrix video first. Extract the ZIP on the PC and open PowerShell in the directory containing cc2-camera-boot-r1.tar.gz. Replace the example IP if needed.

```powershell
scp -O .\cc2-camera-boot-r1.tar.gz root@192.168.1.103:/tmp/cc2-camera-boot-r1.tar.gz
```

In SSH, run each separately:

```sh
mkdir -p /tmp/cc2-camera-boot-r1
```

```sh
tar -xzf /tmp/cc2-camera-boot-r1.tar.gz -C /tmp/cc2-camera-boot-r1
```

```sh
sh /tmp/cc2-camera-boot-r1/manage.sh check
```

Require **CHECK PASS**. It validates checksums, dependencies, the original running process and fresh idle telemetry. It writes temporary diagnostics only, installs nothing, and restarts nothing. On STOP, retain the output and do not continue.

## Stage and reboot manually

After preflight:

```sh
sh /tmp/cc2-camera-boot-r1/manage.sh stage
```

Require **STAGED**. The current camera remains running. Then reboot the idle printer normally; do not use Emergency Stop as a routine restart. Do not begin a print during this first trial.

After the panel reconnects:

```sh
sh /opt/usr/cc2-camera-reap-boot-r1/manage.sh status
```

Require `Boot R2 library mapped: yes` and a marker bound to the current camera PID with `patched=1 enable_lan_once=0`. A marker file or running process alone does not validate video. If the library is absent, working original video is not an R2 success.

## Acceptance

1. Check Matrix printer-online status separately from working remote video.
2. Open CC2 Control local video and keep both streams active for one minute.
3. Close Matrix, confirm local video continues, and reopen Matrix once.
4. Run status again and report both outputs, smoothness and unexpected effects.

No further reconnect stress test is needed at this stage. Do not restart only the camera during this trial: that would again lose the normal initialization being preserved. Do not share raw camera logs; they may contain RTC tokens.

## Rollback

Another normal full reboot while idle returns to an original camera without R2 because activation was consumed. This leaves an unarmed startup hook.

To also restore the exact original startup file while idle:

```sh
sh /opt/usr/cc2-camera-reap-boot-r1/manage.sh rollback
```

Require **ORIGINAL STARTUP RESTORED**, then reboot the idle printer normally. Rollback does not terminate the current process; an active R2 library remains in memory until that process exits. Backup and payload are retained. If another intervention or firmware changed the startup file, rollback refuses to overwrite it.

Do not run alongside a firmware update or use this as permanent activation. Report and restore a failed trial rather than bypassing checks or retrying automatically.

Credit: **tharje** for the thread-retention investigation and reproduction, **dwolfuk** for Matrix comparison. Trial/library sources are included. No vendor executable, private key or credentials are distributed.
