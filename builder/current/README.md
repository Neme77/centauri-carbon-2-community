# Current firmware builder

This directory contains the current firmware assembly logic and its host-side
tests. Working filenames are stable; release numbers are recorded in Git tags,
the changelog and source constants.

## Inputs not stored in Git

The builder fails closed when an input is absent or does not match its pinned
hash. Supply these locally before building:

- a legally obtained Centauri Carbon 2 stock firmware package;
- the pinned vendor printer reference binary;
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
`3e93adba0c0de210c55fa7c7db588540c6b35c81`, incorporating the validated
PR57–59 integration and authenticated loopback HTTP serial discovery, plus manual camera streaming
with visibility suspension and bounded reconnection attempts, the Russian interface translation,
printer sub-states and refusals, Canvas auto refill and the print history with time-lapse videos. It includes persistent UDS telemetry, MQTT workload reduction,
current UI and five languages, speed/flow controls, 128 MiB uploads, live Z offset,
A/B calibration, adaptive mesh start/rendering, emergency recovery and downloads.
The Canvas print popup identifies each G-code filament by its colour and material before slot mapping.
The 3D bed mesh frames absolute positive and negative probe offsets using the actual height range.
Automatic Canvas discovery is bounded across MQTT reconnects, with explicit Sync recovery and diagnostic counters; registration still follows when another client's Canvas reply completes discovery first.
Files lists the newest 128 G-code files of each storage with their total, and refused browser uploads report the error.
An OrcaSlicer upload with a taken name is saved as a numbered copy instead of being refused.
A bed mesh calibration from Bed Levelling homes X, Y and Z first when the printer is not homed.
Motors left energised after homing or calibration are released after ten idle minutes, so the mainboard fan stops.
Telemetry asks the printer only when its stream falls silent, and the Job page reads object state from that stream, because the firmware keeps every request on its local socket in memory.
The runtime identity remains 1.1.31, matching the printer-tested build.
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
