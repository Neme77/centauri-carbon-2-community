# Persistent camera R2 — controlled personal installation

Loads the exact tested thread-reclamation R2 and connection-recovery R2 libraries at every boot. No native binary is rebuilt or replaced. Two temporary camera-only restarts restored local and Matrix video, followed by ordinary observation with AI enabled. Extended use and specific AI detections remain unqualified.

This installer is pinned to the supplied printer startup SHA-256 `01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc`, including its old one-shot camera hook. It is not a general installer for every tester. Camera/firmware fingerprints must also match.

## Read-only check

With the printer idle, copy `cc2-camera-persistent-r2.tar.gz` to `/tmp/`. Do not use the CC2 Control updater for this package. Run separately over SSH:

```sh
mkdir -p /tmp/cc2-camera-persistent-r2
```

```sh
tar -xzf /tmp/cc2-camera-persistent-r2.tar.gz -C /tmp/cc2-camera-persistent-r2
```

```sh
sh /tmp/cc2-camera-persistent-r2/manage.sh check
```

Share the result before installation. The check does not restart processes.

## Install and verify

After a successful check, while idle:

```sh
sh /tmp/cc2-camera-persistent-r2/manage.sh install
```

This backs up the exact current startup to `/opt/usr/cc2-camera-recovery-r2/printer.original` and replaces only its old camera launch line. It does not change the callback patch, printer executable, print services or camera executable. The current camera process remains running until the next start.

Reboot the idle printer normally:

```sh
reboot
```

After full startup, verify local and Matrix video, then run:

```sh
sh /opt/usr/cc2-camera-recovery-r2/manage.sh status
```

Both libraries should be mapped from the persistent directory, with markers matching the current PID. Markers do not prove actual video. After success, a second ordinary idle-printer reboot can confirm persistence.

## Disable, enable or exact rollback

`sh /opt/usr/cc2-camera-recovery-r2/manage.sh disable` disables the next patched camera start without altering the current process. Reboot while idle to use the native camera. `manage.sh enable` reenables loading after integrity checks; `manage.sh verify` checks installed files.

For exact startup rollback while idle:

```sh
sh /opt/usr/cc2-camera-recovery-r2/manage.sh rollback
```

Rollback refuses to overwrite unrelated later startup changes, keeps the backup, and does not restart services. A subsequent normal idle-printer reboot uses the previous startup, including its unarmed one-shot hook.

If boot preflight finds changed fingerprints, missing payload, unavailable dependencies or disabled activation, it launches the native camera instead. This is pre-launch fallback, not supervision after exec. No watchdog or automatic crash restart is added. A future camera firmware change requires fresh qualification.

Keep full camera logs private: they may contain session data. No vendor firmware, credentials or private keys are included. Thanks to tharje and the community beta testers.
