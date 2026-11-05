# Autonomous Canvas lifecycle — not an installer

This directory starts the coordinated activation/update/restore implementation.
`policy.h` defines operation ordering and boot/update admission; `journal.h`
implements the private persistent record used to commit an intent before its
side effect. It uses a kernel advisory lock, exact bounded records, a checksum,
generation comparison, transaction/plan binding, file and directory fsync, and
atomic replacement. Missing, truncated, public, symlinked or corrupt records
must never be interpreted as a stock Canvas.

Verification after staging or a transfer ACK requires a different persisted
kernel boot UUID, plus live guard and Canvas evidence from the coordinator.
Neither the browser nor a protocol ACK can declare the complete feature ready.
A transfer intent surviving interruption requires stock recovery before native
boot. A modified or not-yet-verified application requires the qualified host
guard. Removing that guard requires stock transfer, a later boot and healthy
Canvas observation first. A base firmware update is refused until disabled;
an app update while active must preserve the exact coordinated native profile.

The journal checksum detects corruption; it does not authenticate root.
`profile.h` binds a private recovery plan with six unique roles (bootstrap,
native binary, guard, entry, recovery worker and activation marker) to the
persisted transaction. Each target must retain its exact digest and ownership.

`recover_worker.c` accepts only boot-policy inspection, a non-UART check and
original-application recovery. It requires a durable transfer intent and the
exact private stock backup, but no running MQTT process or same-boot timeout.
It checks process metadata before opening the UART and again after exclusive
acquisition, never stops a native process, and uses the existing bounded
application-transfer core. A complete transfer must be journaled before jump.
Failed ACKs, cancellation or a journal failure leave native boot blocked.
There is no filter-install command in this executable.

`boot-dispatch.sh` routes both planned native entry paths according to the
worker's verified policy. It performs at most one bounded stock recovery and
then requires a guarded entry. It never falls back to an unguarded vendor start
after a refusal. Its constants remain deliberately unconfigured: the universal
profile installer, mount ordering, strict guarded loader, normal activation
coordinator, existing private-trial migration and updater integration are still
pending. **Do not install these sources on a printer as a complete updater.**

`test_lifecycle.c` checks activation/restoration ordering, all boolean evidence
combinations, boot admission, update refusal and real private journal files.
It also tests stale writers, changed plans, interrupted staging, all single-byte
payload corruptions, truncation, symlinks and advisory-lock reopening. Host,
ARM and sanitizer qualification runs offline; record its receipt separately
from any later real-printer validation.

`test_recovery.py` executes the actual recovery CLI on host and ARM with a dummy
native process, PTY and RAM image. `test_dispatch.py` checks both native routes
on dash/BusyBox using fake worker/entry programs. The original local stock image
is a read-only simulator input and is never committed or executed. These new
executor/dispatcher tests passed on the owner's offline Docker run: 52 actual
host/ARM recovery cases and 36 dash/BusyBox dispatch cases. They do not qualify
installation on a printer or physical recovery after a real power interruption.

`guarded-entry.sh` is the next unconfigured template: it verifies the pinned
dependency manifest and inert loader before enabling native reactor/network
flags. It preserves the qualified optional network-control startup policy,
executes the exact native program and has no vendor fallback. The standalone
host guard is built with `RF_REQUIRED_NATIVE=1` and its own stable component
directory; an absent activation marker then aborts the exact native program.
The default legacy trial build remains unchanged and foreign helpers remain
untouched. `test_guarded_loader.py` checks required/legacy constructor behaviour
on ARM with current and stock GLIBC, plus dash/BusyBox loader fixtures. This new
qualification passed on the owner's offline Docker run: 44 constructor/loader
cases, native callback regressions and stock GLIBC 2.23. Installed profile
generation/migration and activation remain separate release blockers.

`startup_tx.h` stages private snapshots of every original/new startup file,
commits a prepared-plan receipt and only then permits atomic target replacement.
Resume accepts only the exact old/new states; changed targets, snapshots, modes
or plans refuse before the first replacement. Restoration runs in reverse order
and requires a verified-stock, durable host-removal intent or disabled lifecycle
record. This is a low-level
library, not an arbitrary-path CLI: the qualified coordinator must supply fixed
targets and enforce fresh idle/cold state and exclusive update ownership.
`test_startup_tx.c` simulates process exits at snapshot, write, rename and ready
boundaries, resuming through new processes and restoring exact bytes/modes.
Its 29 scenarios passed on host, ARM and sanitizers, including the fixed-plan
`startup_resume.h` bridge. The updated boot-policy test
also requires the guard in HOST_STAGED/HOST_READY, so the first boot actually
provides host evidence before changing Canvas.

Normal transfer now has an explicitly compiled `CV_COORDINATED` mode in
`../transfer/canvas_worker.c`. That mode locks and verifies the lifecycle/profile,
requires HOST_READY before filter installation, commits intent before native
stop, and commits the complete ACK before application jump. It uses the common
lifecycle as its sole durable authority; the legacy same-boot ticket remains
unchanged in default builds. An uncertain final journal commit suppresses both
jump and any new erase in that invocation. The separate boot recovery worker
can then restore the exact original from the durable intent.

Journal temporary names are now unique: interrupted uncommitted files stay
available for diagnosis but cannot block a later authorized commit. The combined
`test_transfer_lifecycle.py` executes the actual legacy and coordinated programs
on host/ARM, including an injected ACK-commit rename failure followed by real
stock recovery without deleting the orphan. This integration and the shared
journal change passed 80 actual host/ARM transfer cases, with 54 recovery cases
and the existing dispatcher/transaction regressions. This remains simulated
qualification, not evidence of physical flash readback or a complete installer.

`installed_profiles.py` recognizes the captured maintainer trial and its known
pre-RFID reactor/network/EXHAUST baseline using a closed hash catalogue. It is
offline planning only, not universal clean-install support or live readiness.
`migration_plan.py` prepares the fixed eight-target transaction while preserving
manifest paths/order and target modes. Captured private files stay outside the
repository; the source catalogue contains hashes only.

`startup_worker.c` and `startup-gate.sh` implement the concrete boot delegate.
The delegate pins the static helper. Its data plan has eight implicit, fixed
paths; each payload hash is bound by the journal through `recovery-profile`'s
bootstrap role, which points to `startup-plan`. Other roles must name the fixed
native, guard, guarded entry, recovery worker and marker paths. These component
files remain stable while the transaction repairs the external startup files.
The helper verifies that fixed profile before and after reconciliation. Only a
staged transaction can finish a mixed install; host removal resumes only from
its durable intent after stock verification. The original root delegate is
restored last. `--prepare-only` never starts a native process or opens a UART.
The worker contains no fresh install, process stop or firmware transfer action.
Its 63 host/ARM/sanitizer fixture cases and 14 dash/BusyBox root-delegate cases
passed in offline Docker qualification. Concrete assembly, live authorization
and updater integration remain required before an installable candidate.

`boot_assembly.py` now generates the closed maintainer profile using the
qualified strict guard, recovery worker and startup worker. Both legacy entry
paths become delegates to the pinned root gate; network qualification and
manifests retain their original checks, updated to the exact new file hashes.
The new guard is required when qualifying an existing native network mapping;
native restart through the legacy network-spawn path remains refused. EXHAUST
stays in the core preload chain. A stock lifecycle route retains the guarded
entry until the verified host-removal transaction restores the original root.

`fresh-guard.sh` reuses the exact pinned network control program's loopback
idle/cold readiness reader. It is an observation, not an operation lock or
fresh-install coordinator. It accepts standby/complete according to that
existing policy; cancelled jobs are refused. `test_assembled_boot.py` prepares
all eight exact assembled payloads with an inert native identity, while
`test_assembly_shells.py` checks shell syntax, isolated native preload validation
and readiness wrapper dispatch. The assembled profile passed 12 host/ARM
preparation cases, 18 shell syntax checks and 30 isolated wrapper cases.

The app now observes the existing installer directory lock before processing
mutating requests. Read-only health and both emergency-stop routes stay
available; foreign browser origins remain refused. The shared ownership helper
never takes over stale/unknown objects or removes foreign directory contents.
Real HTTP and ownership tests passed on host, ARM and sanitizers, with existing
calibration/grille regressions and an ARM application smoke test.

`stage_worker.c` is the concrete host-stage coordinator for the captured legacy
frame-fix migration. A separate, configured executable pins the new barrier-aware
app, recovery profile and live guard without creating a boot-helper hash cycle.
It takes the existing installer lock, verifies one exact running app/native,
the owned idle RFID capability and reactor/EXHAUST/RFID mappings (network shim
optional), and checks fresh idle/cold state twice. It preserves a private exact
stock backup before durably creating HOST_STAGED, then prepares and commits the
eight-file transaction. Unknown journals/targets/components refuse. The same
staged transaction can resume; active lifecycle states cannot be restaged.
It never stops the app/native, opens UART or transfers firmware. Default build
constants are intentionally unconfigured. Its first 75 staging cases passed.
The combined coordinator then passed 78 staging and 87 post-boot cases on
host/ARM/sanitizers. Those qualified revisions contain no normal-transfer action.

The next combined revision adds `normal_flow.h`: a pinned normal worker owns
the actual serial transfer and independent live checks, while its parent holds
the shared installer barrier. The parent releases the lifecycle lock for the
child, validates its durable ACK afterwards and retains the app barrier for a
reboot or incomplete intent. Cancellation is cooperative; the parent does not
SIGKILL a worker while it may be restoring the original. `--advance` verifies
a new host boot before installing and verifies later ACKs only after a further
kernel boot. ACK, active and verified-stock states never repeat a transfer.
The original worker's compile-only `CV_BOOT_ID` fixture override defaults to
the unchanged kernel proc path; no boot identity comes from an HTTP request.

`--status` is bounded journal evidence, not permission to reboot or activate.
It probes existing locks and the compiled profile binding without hashing the
29MB native executable on every UI poll. Every action still validates the full
profile and live identity under exclusive ownership. Mutating operations must
never authorize themselves from this cheap status observation.

`test_normal_flow.py` connects the actual coordinator to the actual normal
worker and a PTY/RAM Canvas, including the automatic path, complete install/
verify/restore cycle, bad-offset recovery, cancellation and preflight refusals.
ARM fixtures use an explicitly pinned emulator launcher for child execution;
production uses the pinned static ARM worker directly. The grouped run repeats
all 78 stage, 87 boot and 80 legacy/coordinated worker cases before those 16
parent/worker flows. This newest revision is awaiting qualification.
Updater/UI/capture-tool migration, clean profiles, host-disable flow and full
private candidate validation remain release blockers. No complete installer is
claimed and no printer has been accessed by any of these automated checks.
