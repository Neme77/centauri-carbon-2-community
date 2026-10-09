# Combined test qualification

Base: `24cf5222301bc1657db2153e637b52217380f509` (develop after PR #88).
The source snapshot includes the complete merged CC2 backend, committed UI,
five locales and original contributor history in CHANGELOG/NOTICE.

Locally completed:
- Complete CC2 host suite: pass, with the AF_UNIX peer integration test skipped
  by the execution environment. PR #88's original integration tests passed in
  GitHub Actions. This PR does not include the separate additional UDS fixes.
- UDS regression harness with AddressSanitizer/UndefinedBehaviorSanitizer:
  pass (LeakSanitizer disabled because this environment cannot inspect tasks).
- Installer simulation: successful install and restoration, configuration
  preservation including a newly changed LAN configuration, rejection while
  printing, and complete restoration after a failed hook confirmation.
- Shell syntax and Python compilation: pass.
- Archive assembly with synthetic ELF fixtures: payload and outer checksums,
  permissions, embedded matching sources and provenance metadata passed.

Already printer-tested before this package:
- The unchanged Reactor O0 source with native libco: 256 timer suspension cycles,
  1024 bridge cycles including 512 exceptions after suspension.
- Integrated O0 module: Home All and one short print with AI and time-lapse,
  stable PID and approximately stable anonymous memory.

Pending:
- Compile these exact sources with the owner's GCC 6.5.0/glibc 2.23 toolchain.
- The generated module's native preflight (the installer runs it).
- Persistent boot, Home All, object exclusion during a real print, short and
  long print on each printer. No OOM/803 resolution claim is made.

The installer changes only the normal launch line in `/opt/bin/run_printer.sh` and creates a mount-aware boot
launcher, preserving the vendor executable on disk. It refuses read-only init
storage instead of using a late boot service restart or weakening compatibility
checks. It never installs allocator settings, camera stack changes or a kernel.
The firmware's unbounded request_log is not fixed by the bridge.

## User-reported integration update, 2026-10-07

Gino confirmed persistent activation after reboot: the module appeared in the
`elegoo_printer` maps and the activation marker matched its PID. MQTT and UDS
were connected/fresh. Home All and a roughly 28-minute print with AI and timelapse
completed without a crash.

A later complex 108 MB G-code print with CC2 live view, AI and timelapse enabled
had about 33 MB MemAvailable initially and 31.8–32 MB after two hours. That test
is still running. It includes beanbo's reduced-polling changes and additional
local CC2 file-analysis fixes not included in this PR, so it does not isolate
the runtime module's benefit. No definitive OOM/803 resolution is claimed.

The combined experimental installer packages the unchanged develop CC2 source
with this module. The PID panel, file-analysis worker and plate library belong
to separate work and are not bundled by this branch.

## Completed long-print reports, 2026-10-07

These are user reports, not instrumented tests performed by CI:

- Gino reports a complex 108 MB print lasting over seven hours, with live view,
  AI and timelapse enabled. He subsequently clarified that MemAvailable varied
  by about 200–300 KB below the starting value during the process and at the end.
- The collaborator reports roughly twenty hours of printing across two long
  jobs (about ten hours and more than eight hours), with very little memory
  decrease and roughly 31–32 MB available again at idle after the jobs.
- With the earlier reduced-UDS-polling mitigation, that collaborator reports
  starting above 30 MB and reaching roughly 25–26 MB at idle after long jobs,
  with limited recovery. He reports improved stability with the combined O0
  runtime and current CC2 changes.

This comparison strengthens the evidence for the combined configuration, but
workload, firmware and feature settings were not held constant in a controlled
A/B experiment. It does not isolate the runtime's incremental effect or prove
that every OOM/803 cause has been eliminated. request_log remains unbounded.

The branch is now aligned with develop and includes the merged CC2 features.
The exact executable/libco compatibility gates, O0 build, cold/idle preflight,
activation verification and per-printer rollback remain unchanged. The standalone runtime
is opt-in. The new complete firmware candidate has a separate boot integration;
that image is not validated by these historical reports. Published ELEGOO
sources are older than deployed firmware; reassess the provisional runtime when
matching sources or an official fix become available.
