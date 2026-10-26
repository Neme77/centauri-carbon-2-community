# Camera recovery R2 — temporary trial

This trial preserves the qualified R2 thread-reclamation library, including its one-time LAN enable. Recovery R2 also covers direct initial camera startup: it announces readiness after native initialization returns, then to new control connections, suppressing duplicates. The printer can use its own cached session parameters. The patch does not read or copy credentials.

This is not a final fix. Offline checks cannot qualify actual local/Matrix recovery, AI or expired cloud tokens.

Use only with an idle printer and working local/Matrix video as a baseline. After recovery R1, first reboot normally and verify both videos. R2 refuses to start while recovery R1 is still mapped. Do not stress connections, print, change networks or tune Wi-Fi during the trial. Copy `cc2-camera-recovery-r2.tar.gz` to `/tmp/`, then run separately:

```sh
mkdir -p /tmp/cc2-camera-recovery-r2
```

```sh
tar -xzf /tmp/cc2-camera-recovery-r2.tar.gz -C /tmp/cc2-camera-recovery-r2
```

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh check
```

Share the check result before activation. It does not restart processes or send notifications.

After a successful check, `sh /tmp/cc2-camera-recovery-r2/trial.sh start` restarts only ai_camera with thread-reclamation R2 plus recovery R2. Verify local video, Matrix and `sh /tmp/cc2-camera-recovery-r2/trial.sh status`. Counters distinguish startup entry, startup return and control connections; readiness/announcement markers do not prove actual video. Only after success, an assisted second camera restart can use `sh /tmp/cc2-camera-recovery-r2/trial.sh restart`. Never automate retries after failure.

A normal idle-printer reboot removes this in-memory activation and returns to the existing startup configuration. This package does not rearm an earlier one-shot trial or replace original files. Keep full camera logs private: they may contain session data.

Thanks to tharje for stack-leak diagnosis and captures, and to the beta testers for local and remote validation.
