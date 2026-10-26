# Camera components in a CC2 Control update

Some application updates also include the camera components identified in their signed installer and release notes. These updates require a **full manual printer reboot after installation, while idle**. Refreshing the page, restarting CC2 Control or restarting only the printer's control process does not activate a newly staged camera component.

## Install

Use Settings → Updates to verify and install the signed `.cc2update`, or use the supplied multiplatform computer installer. The printer must be connected, idle and cool, with nozzle/bed at most 50 °C and heater targets zero. Close live viewers and keep the printed backup path. No automatic printer reboot or camera restart occurs during installation.

The combined installer verifies the unchanged R2 camera libraries, camera executable, dependencies and supported startup fingerprints. It stages the boot component after the new application passes binary/health/MQTT checks. A recognized existing personal R2 installation and its activation preference are preserved. Unknown startup modifications are refused.

After a full reboot check local port 8080 video and Matrix playback. New installations report activation with:

```sh
sh /opt/usr/cc2-control/camera/manage.sh status
```

A preserved personal installation reports through its existing `/opt/usr/cc2-camera-recovery-r2/manage.sh status` command. Markers prove activation and notifications, not actual video or individual AI detections.

## Recovery and rollback boundaries

The thread-reclamation component prevents the known accumulation of completed client-thread stacks. The recovery component restores readiness notifications when the camera is relaunched through its patched launcher. This is not a watchdog or an automatic restart service.

The application's backup restore command also restores the previous camera startup when that update changed it. It refuses to overwrite unrelated later startup edits. Restore while idle and cool, then reboot completely to change an already loaded in-memory patch. Disable/enable choices apply to the next camera start.

The camera executable, printer executable and callback patch are not replaced. The stream limit is unchanged. These application-update components are separate from subsequent builder and firmware-image integration.

## Validation

The R2 runtime was observed through seven hours of printing with AI options enabled and streaming working. Local and Matrix recovery were separately confirmed after camera-only restarts. Specific AI detection events, expired RTC sessions and longer endurance remain separate qualifications.

The combined installer passed 58 previous-trial-profile and 53 native-profile startup/rollback/refusal fixtures under each of sh and BusyBox ash, plus isolated application transaction tests for preflight, health and post-commit failures, exact manual rollback and foreign-edit refusal. Its ARM build, browser tests and sanitizers passed. Combined installer acceptance on the printer remains pending before stable publication.

Thanks to tharje for reproduction, traces, crash analysis and diagnosis, and to the community beta testers.

Preparation validates keyboard file selection with browser focus restored after native dialogs. It serializes runs for this branch; no camera payload bytes change.

Unchanged application archives retain their verified source pins during preparation. Merging the standalone date fix retains the combined 1.2.19 archive rather than the older application snapshot.
