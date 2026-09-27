# Changelog

This file records user-visible project changes. Signed artifacts and exact
checksums remain attached to each GitHub release.

## Unreleased

No user-visible changes yet.

## V4.2 — CC2 Control 1.1.30

- unified Bed Levelling workflow;
- Side A and Side B saved-mesh visibility;
- screw corrections in microns and guarded reference optimisation;
- operational dashboard Quick Actions and global emergency stop;
- compact settings panels and persistent dark/light themes;
- file-manager, upload, Canvas, thumbnail and print fixes.
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

Earlier technical release records remain under [`releases/`](releases/) and in
Git history.
