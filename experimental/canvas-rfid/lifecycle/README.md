# Autonomous Canvas lifecycle

The configured private PC trial coordinates host staging, complete kernel
reboots, application-only Canvas transfer, verification and exact restoration.
It supports the pinned clean 4.4.3 baseline, the retained legacy migration and
the cumulative EXHAUST baseline; it is not a universal public OTA installer.
No ELEGOO image, proprietary executable, GLIBC runtime or signing key is included.

## Ordering and recovery

`journal.h` persists intent before each side effect with ownership checks,
checksummed bounded records, generations, plan/profile binding, fsync and atomic
replacement. Corrupt, truncated, foreign or public records refuse. `profile.h`
binds the bootstrap, native binary, guard, entry, recovery worker and marker.

`startup_worker.c` and `startup_tx.h` prepare and resume an exact eight-target
startup transaction. Original backups remain private and immutable; foreign edits
refuse. `startup-gate.sh` and `boot-dispatch.sh` route the two startup paths through
that transaction and recovery policy before entering the native executable.

`stage_worker.c` validates one pinned app/native identity and fresh idle/cold
observations under the shared installer barrier, backs up the exact stock Canvas
image, commits HOST_STAGED and stages the host transaction. No UART or native
stop occurs during staging. `coordinator.c` subsequently verifies a different
kernel boot UUID and the guard mappings before running the transfer worker.
The parent retains maintenance ownership across the bounded child operation.
Both emergency-stop routes and read-only status remain available in the app.

`normal_flow.h` connects the actual transfer worker to the persistent lifecycle.
Completed ACKs never repeat a transfer. Native entry remains protected when a
modified or unverified Canvas may be present. An incomplete transfer intent
requires original-application recovery before native boot; the exact stock
backup must validate. `recover_worker.c` cannot install the filter. Transfer ACKs
are durable protocol evidence, not physical flash readback.

Restoration writes the original Canvas first and verifies it after another full
boot. Only then may the host transaction restore its original startup files.
A final boot verifies the disabled state. The UI uses the lifecycle rather than
assuming that one reboot or a queued request means completion.

## Cumulative EXHAUST baseline

`cumulative_profile.py` derives the expected exact profile after the accepted
EXHAUST manager stages the chamber component. The live publisher independently
checks those resulting dependencies. The initial pre-reboot observation may
precede the floor mapping; subsequent boot checks require its pinned library and
private current-PID idle state channel. RFID removal restores that floor baseline.

`retire_completed.sh` archives only the known, verified-disabled old private
trial, without deleting files or touching the native process. Active, unknown,
changed or incomplete trials refuse. The source in `../cumulative/` includes the
Windows/SCP permission correction and the actual staging/order boundary tests.

## Validation and limits

Grouped host/ARM/sanitizer qualification covers freshness, staging, post-boot
identity, actual parent/worker transfers over a simulated UART and RAM flash,
interruption/recovery, cancellation, restore/remove/reactivate, app API guards,
readiness, publishers and both shell startup routes. A complete private bundle
also qualifies installer and rollback and pins every unchanged reused artifact.
The cumulative post-boot test has 96 cases, including missing floor, missing
channel and foreign-PID channel refusals. The publisher is tested independently
for RFID and floor namespaces. Tests do not access a physical UART or printer.

Neme77 physically completed the autonomous RFID-only activation, original-Canvas
restore and host-removal cycle, then accepted the complete cumulative activation
with EXHAUST, LAN, RFID and approved graphics. Anycubic PETG and one Creality spool
were read. Tagged ELEGOO spools and all brands/formats were not physically tested.

Public app/base-firmware update delivery and migration remain release blockers.
The ordinary public updater does not yet deliver this complete native suite.
Keep protocol ACKs, simulated flash tests and physical acceptance distinct. The
pending #301 change is excluded from this implementation and qualification.
