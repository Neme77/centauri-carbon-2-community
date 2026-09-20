# Centauri Carbon 2 Community Firmware V4.0

Firmware V4.0 is based on official ELEGOO firmware 02.01.00.00 and integrates CC2 Control 1.1.22.

## What's new

- Guided four-screw load-cell bed tramming at X35/Y30, X225/Y30, X225/Y225, and X35/Y225, using three samples per point and the front-left screw as the reference.
- Klipper `EXCLUDE_OBJECT` support for safely excluding a selected component from a labelled multi-object print.
- Temporary expert terminal unlock with two confirmations, an exact unlock phrase, a five-minute in-memory token, idle-state enforcement, and a full-homing requirement for movement G-code.
- Minimal read-only Moonraker-compatible bridge on port 7125 for BTT Panda Breath.
- Corrected X orientation in the 3D Bed Mesh view.
- Isolation of screw-measurement output from the saved 11×11 Bed Mesh.
- CC2 Control 1.1.22 integrated into the firmware root filesystem.

## Retained functionality

- Root SSH, extended GUI Z-offset, Dual Trust v2, local HTTP access, and upload-during-print support.
- Local dashboard, camera, telemetry, thermal history, and protected printer controls.
- ELEGOO Canvas four-slot status and controls.
- Persistent custom material presets.
- Internal and USB G-code library with nested-folder support and native method 1020 print start.
- 2D and interactive 3D Bed Mesh visualisation.

## Validation

The release passed stock-key signing self-tests, prerequisite and SquashFS round-trip checks, official package verification/decryption, patched-rootfs verification, SWU reconstruction, and the complete ELEG packaging/signature chain.

Hardware validation confirmed CC2 Control 1.1.22, MQTT registration and snapshots, clean first-run LAN-code configuration, Panda Breath connectivity on port 7125, object exclusion, and the revised screw coordinates.

## Credits

Maintained by **Neme77**. Hardware validation and testing contribution by **Barry Green**, whose remote tests and detailed feedback helped validate the Panda Breath bridge, object exclusion, and bed-tramming workflow.

## Important limits

- Centauri Carbon 2 only; no compatibility with other printer models is claimed.
- Panda Breath access is intentionally read-only.
- Object exclusion requires slicer-generated object labels.
- Expert terminal access does not make arbitrary commands safe.
- A complete firmware reflash restores the root password contained in the firmware image. If you changed it previously, set it again with `passwd` after installation.
- Modified firmware is installed entirely at the user's risk and is not supported by ELEGOO.
