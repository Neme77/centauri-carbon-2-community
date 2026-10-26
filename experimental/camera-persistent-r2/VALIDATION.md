# Camera R2 validation and scope

This experimental change addresses two separate camera failures on Community Firmware V4.4/V4.4.1 (ELEGOO base 02.01.01.07).

## Behaviour

- Reclaim completed client threads, avoiding accumulation of their retained stacks in the 32-bit virtual address space.
- Announce readiness after native camera initialization and to newly published control connections, suppressing duplicate announcements. The printer reuses its own session state to restore local/Matrix video after a camera-only restart.
- Offer a reversible, opt-in persistent boot wrapper for the exact verified startup configuration. The wrapper checks executable, payload, dependencies and startup fingerprints before loading either library; a failed pre-launch check uses the native camera.

The original camera executable is not replaced. The active stream limit is unchanged. There is no watchdog, automatic crash restart, Wi-Fi tuning, cloud credential copying, or automatic activation through a CC2 Control application update.

## Hardware observations, 21 October 2026

On the qualified camera SHA-256 `ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99`:

- Two camera-only restart trials restored both local port 8080 video and Matrix video; the user explicitly confirmed both.
- Nearly two hours of ordinary use were reported without a new failure; AI options were enabled. This does not establish detection accuracy or qualification of individual AI events.
- A persistent installation passed its read-only preflight and survived its first full printer reboot. Both libraries were mapped from the persistent directory in PID 1902.
- That boot reported recovery ready=1, epoch=1, announced=2, failed=0, initial_calls=1, initial_returns=1 and connections=2. Thread-reaper last_error=0. These are activation/notification observations, not independent video verification.
- Actual local/Matrix playback after this persistent boot and a second reboot remain to be confirmed.

Long-duration endurance, expired RTC sessions, specific AI detections and independent tester replication remain pending.

## Reproducible checks

The workflows build and test the thread reaper and recovery code on the host and under ARM QEMU, including sanitizer, allocation-failure, ABI, socket-ordering and wrong-executable refusal checks. Persistent startup fixtures run under both sh and BusyBox ash (51 each), including exact rollback, repeated boots, disabled activation, missing/tampered payloads and changed firmware/startup refusal.

Prior successful source-validation runs:
- Thread reaper: https://github.com/Neme77/centauri-carbon-2-community/actions/runs/39297401612
- Recovery: https://github.com/Neme77/centauri-carbon-2-community/actions/runs/39491902374
- Persistent startup: https://github.com/Neme77/centauri-carbon-2-community/actions/runs/39505462152

CI does not replace printer testing. Generated build validation records describe offline results; the hardware observations above are recorded separately.

## Installation boundaries

The temporary trial is documented in ../camera-recovery-r2/README.en.md and README.it.md. The persistent installer in this directory is pinned to a personal startup file with the previous unarmed one-shot wrapper; it is not a generic installer for other users. No stable catalog, builder payload or application version is changed by this PR.

## Credits

Thanks to [tharje](https://github.com/tharje) for issue #160, controlled reproduction, traces, crash evidence and the client-thread stack-exhaustion diagnosis; and to the community beta testers for local and remote playback validation. Vendor components remain ELEGOO components. No vendor executable, private key, cloud/session credential or raw trace is included.

Related: #160. Kept open for independent tester confirmation and broader qualification.
