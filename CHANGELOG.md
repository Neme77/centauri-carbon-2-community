# Changelog

This file records user-visible project changes. Signed artifacts and exact
checksums remain attached to each GitHub release.

## Unreleased

No user-visible changes yet.

## CC2 Control 1.1.31

- added OrcaSlicer Moonraker-agent Canvas filament synchronization through
  read-only `/server/info` and `/server/database/item?namespace=lane_data`
  compatibility endpoints;
- maps Canvas tray material, colour and nozzle temperature into OrcaSlicer's
  filament synchronization data without adding a new polling loop or MQTT
  subscription;
- added LAN access-code replacement and revalidation from **Settings →
  Connection**;
- LAN-code changes are written atomically and restart only CC2 Control, leaving
  printer services and an active print untouched;
- retained the documented SSH service-restart and configuration-reset procedures
  as recovery fallbacks;
- added regression tests for Orca lane data and LAN-code revalidation;
- thanks to **@efiten** for the original OrcaSlicer compatibility proposal,
  investigation and printer-side validation.

## V4.2 — CC2 Control 1.1.30

- unified Bed Levelling workflow;
- Side A and Side B saved-mesh visibility;
- screw corrections in microns and guarded reference optimisation;
- operational dashboard Quick Actions and global emergency stop;
- compact settings panels and persistent dark/light themes;
- file-manager, upload, Canvas, thumbnail and print fixes;
- removed remaining demonstration values from live job and object statistics;
- completed elapsed, remaining and total-layer status handling;
- added first-run service realignment after LAN-code registration;
- removed the old post-install manual reboot/power-cycle requirement: CC2 Control
  now restarts and realigns its own local service automatically;
- rejected implausible G-code temperature metadata instead of displaying values
  such as 6211 °C in the file preview;
- restored fast thumbnail display by loading the image before full metadata and
  caching metadata by storage, path, size and modification time;
- simplified print preparation to Side A/Side B plus one calibration option;
- automatically uses the selected side's saved mesh, adaptive G-code probing or
  full-bed calibration as appropriate;
- requires full-bed calibration when the selected side has no saved mesh.

## V4.1 — CC2 Control 1.1.25

- integrated local CC2 Control service;
- fixed persistent-storage startup ordering;
- established the V4.1/R8 reproducible builder snapshot.

Earlier release details remain available through GitHub Releases, tags and Git
history.
