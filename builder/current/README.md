# Builder 4.4 — official OTA 02.01.01.07 required

The separate Canvas-ejection prototype is under qualification: [trial status](../../experimental/canvas-eject/TRIAL.md). Default firmware builds do not enable it. The optional old-base adapter is explicitly rejected on the new base.

This builder works **only with the new official CC2 firmware base `02.01.01.07`**, identified by the exact input SHA-256 pins. Provide `cc2_eeb001_02.01.01.07_20260922210235.zip.sig` in `original_firmware/`; do not substitute the firmware input from older builders. The 4.4 patch addresses and ELF layouts are specific to this base. The checks refuse unknown inputs.

CC2 Control 1.2.3 includes the aligned Control panel, Enter-to-apply fan percentages, confirmed four-slot Canvas material reset, persistent camera viewing while scrolling and seven UI languages (English, Italian, French, Chinese, Russian, Polish and Czech). Canvas retains the bounded MQTT path; the experimental full Canvas UDS cache is excluded.

Existing validated callback O0, light/display acknowledgement, authenticated LAN/WAN fallback, application registration recovery, own-PONG freshness, UDS homing/light diagnostics, Z offset, PID/Input Shaper and Orca Canvas synchronization remain included. The source snapshot and launcher checksum are updated together.

Run `Build-Release.cmd` or the qualified Linux build sequence. `build_release.py` runs builder/launcher tests, full CC2 host tests, ARM compilation and signing checks before `CANDIDATE BUILD PASS`. The generated Dual Trust ARM wrapper is reproduced from source and checked against the unchanged qualified payload hash; no generated vendor firmware, ARM runtime modules or private keys are included in Git.

The combined R2 predecessor passed a signed WSL build and an initial clean-reset printer trial reported by the maintainer. New languages and source-only wrapper generation require this branch's CI/toolchain validation. Treat the output as a hardware-test candidate before publication.

# Current firmware builder

## Optional Canvas ejection component

The imported touchscreen/web ejection prototype is opt-in on its legacy qualified base. The current 4.4 builder rejects it before installation: native integration and object layouts still require qualification. See [trial status](../../experimental/canvas-eject/TRIAL.md) for blockers and required physical tests. Default builds do not activate ejection.

This directory contains the current firmware assembly logic and its host-side
tests. Working filenames are stable; release numbers are recorded in Git tags,
the changelog and source constants.

## Inputs not stored in Git

The builder fails closed when an input is absent or does not match its pinned
hash. Supply these locally before building:

- a legally obtained Centauri Carbon 2 stock firmware package;
- the verified official OTA identified by the current baseline manifest;
- the SSH executable required by the image;
- SquashFS 4.6.1 tools built from the pinned upstream source;
- the appropriate private signing key;
- the generated/prepared CC2 Control component files.

Vendor binaries, private keys and generated firmware are intentionally excluded
from the repository. Their expected hashes and validation logic are in
`core/firmware_builder.py`.

The exact CC2 Control source snapshot used by the preparation step is committed
as `components/cc2-control/source/source.zip`; the unpacked canonical source is
available at the repository root under `cc2-control/`.

The component is pinned to source commit
`35937a00a46f24c794e1e4309a9fc4dd96d54262`, incorporating the validated
PR57–59 integration and authenticated loopback HTTP serial discovery, plus manual camera streaming
with visibility suspension and bounded reconnection attempts, the Russian interface translation,
printer sub-states and refusals, Canvas auto refill and the print history with time-lapse videos. It includes persistent UDS telemetry, MQTT workload reduction,
current UI and seven languages, speed/flow controls, 128 MiB uploads, live Z offset,
A/B calibration, adaptive mesh start/rendering, emergency recovery and downloads.
The Canvas print popup identifies each G-code filament by its colour and material before slot mapping.
The 3D bed mesh frames absolute positive and negative probe offsets using the actual height range.
Automatic Canvas discovery is bounded across MQTT reconnects, with explicit Sync recovery and diagnostic counters; registration still follows when another client's Canvas reply completes discovery first.
Files lists the newest 128 G-code files of each storage with their total, and refused browser uploads report the error.
An OrcaSlicer upload with a taken name is saved as a numbered copy instead of being refused.
A bed mesh calibration from Bed Levelling homes X, Y and Z first when the printer is not homed.
Motors left energised after homing or calibration are released after ten idle minutes, so the mainboard fan stops.
Telemetry asks the printer only when its stream falls silent, and the Job page reads object state from that stream, because the firmware keeps every request on its local socket in memory.
Bed Levelling keeps a build-plate library with each plate's mesh and Z offset; mounting a plate whose mesh is not in its side's slot writes that slot and restarts the printer.
Spools tracks filament spools per Canvas slot from the printer's measured extrusion and asks which spool new filament belongs to.
The snapshot also restores Input Shaper, adds calibration Quick Actions, aligns Control panels and expands the dashboard Live View without changing the video aspect ratio.
Build plates keep a mesh per bed temperature and nozzle; a print started here loads the measurement nearest to its bed temperature, and bed mesh calibration with a bed soak runs on CC2 Control.
The release candidate reads the canonical version files.
Latest spool tracking and full firmware callback integration require hardware validation.
Preparation runs the complete component host suite before the ARM build and
records source commit, archive checksum and every installed file checksum.
Stale prepared manifests from the previous snapshot are rejected: rerun
`prepare.ps1` before firmware preflight.

The maintainer installed the preceding serial-discovery stock V4.2 package and verified first
configuration, automatic reconnection after reboot and fresh UDS telemetry.
Scripts were checked in the rebuilt image for LF, no BOM and mode 755.
This validation does not cover every printer function or the separately packaged
standalone updater. The camera streaming changes, the Russian translation, sub-states, refusals, auto refill
and print history have host and browser tests and still require printer validation. Private/vendor inputs and signed firmware are not published
by this source tree.

Preparation normalizes script line endings and records explicit executable/data
modes so Windows checkout and permission readback cannot produce CRLF or mode
777 in the image. The standalone updater is generated with
`python3 cc2-control/installer/package.py` from the repository root; see
`../../cc2-control/installer/README.md`.

## Layout

- `core/firmware_builder.py`: fail-closed image builder;
- `prepare.py`: validates and stages CC2 Control;
- `build_stock.ps1`, `build_community.ps1`: Windows/WSL entry points;
- `launch_helpers/`: argument, logging and output handling;
- `dualtrust/`, `patches/`, `tools/`: source-level build helpers;
- `tests/`: host-side validation.

See [`../../docs/BUILD.md`](../../docs/BUILD.md) for the full workflow.


The current test integration adds confirmed history-record deletion, per-print timelapse selection
and coordinated live camera ownership. Hardware validation is pending before release.

## Release candidate pipeline

See [candidate notes](../../docs/RELEASE_CANDIDATE.md).
Build-Release.ps1 imports keys, OpenSSH and SquashFS tools from the qualified
previous builder, prepares CC2 Control and the new O0 reactor, then runs signing
key check, preflight and firmware construction. The vendor executable is patched
directly from the new official OTA; no old full-binary reference is installed.
This candidate pipeline produces a firmware image only. A matching standalone
updater is a separate deliverable.

The community target is read from cc2-control/FIRMWARE_VERSION.
The current base is official OTA 02.01.01.07 with develop commit 5740954.
The callback remains experimental: the vendor retention defect remains in the
examined new callback paths; host/ARM bridge tests do not establish a definitive
fix for all OOM/803 failures. First-boot Canvas, LAN/WAN, webcam, upload, controls
and long-print memory tests are required before release.
