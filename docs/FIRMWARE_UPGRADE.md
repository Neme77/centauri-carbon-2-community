# Install Community Firmware V4.4.4 — Universal 07 + Hungarian corrections

**ELEGOO base 02.01.01.13 · CC2 Control 1.2.29 · Centauri Carbon 2 only**

After extensive work on the different upgrade branches and incompatible
persistent components, installation is simple again: **copy the complete
firmware to USB and use the printer's Offline Update**. Migration is automatic
for the recognised starting states. No routine probe, SSH cleanup, downgrade,
intermediate panel update or Canvas firmware restoration is required.

## Download the correct package

Use **[CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig)** from the
[V4.4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4).
This Universal 07 revision supersedes the original V4.4.4 RC6-based download
and its mandatory preparation kit. Do not combine the old kit's instructions
with this package. Recovery07 is separate work and is not included.

SHA-256: `a140564016c3ca7402cb91e18434dc350c183b9cdf08a8b90e12c462d5978a2e`

The package is byte-identical to the verified private Universal 07 HU;
only its outer filename changes. The Hungarian revision changes only the
CC2 Control web translation catalogue. It does not add languages to the
printer touchscreen or import the other features in PR #352.

## Before starting

- Finish any print; let the printer cool and set heater targets to zero.
- Keep an existing verified backup. If you have none, use CC2 Control's
  **Settings → Backup and restore** to save your panel data on the PC.
  This selective backup is not a complete disk image.
- Let any ongoing Canvas activation/restoration, firmware transfer or
  requested reboot finish before starting another update.
- A connected Canvas is optional. Recognised active, never-activated and
  restored RFID components are handled automatically. Do not activate or
  restore RFID solely to make this firmware installable.

## Install

1. Download the signed file above and compare its SHA-256 with the checksum
   attached to the release.
2. Copy the **unchanged .zip.sig file to the root of a readable USB drive**.
   Do not extract it or rename it to update.bin.
3. On the idle printer, select **Settings → Check for Updates → Offline Update**
   and choose that file. Menu wording can vary with the display language.
4. Confirm and allow installation and reboot to finish. Keep power connected
   and leave the USB drive inserted until the update finishes.
5. Open `http://PRINTER-IP:8081`. Confirm Control **1.2.29**, normal temperature
   readings and, if attached, normal Canvas recognition. Check your saved
   settings, mesh, spool data and camera before starting a print.

No factory reset is required. The migration preserves personal data and keeps
the previous component evidence in an on-printer archive. Keep existing backups
and these archives until you have checked the new installation.

## What is supported

The tested upgrade paths include Community 4.4.3 with Control 1.2.25,
1.2.26, 1.2.27, the reviewed private 1.2.28 variant, and supported .13 previews.
The code checks component identities and lifecycle evidence, not just the
version label; this does not cover every unofficial package called 1.2.28.
The original .13 first-installation route remains available without a probe.

Absent RFID software is different from an absent physical Canvas. With no
RFID component, the new firmware installs optional support in its inactive
state; it does not activate it automatically. Recognised active/restored
components retain their state. Valid intermediate lifecycle phases are
understood; this is not a reason to interrupt a transfer or a requested reboot.

## If something goes wrong

We are available to help. [Open a support issue](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose)
with the starting versions, exact package filename, visible message and what
still works. Redact LAN codes, passwords and tokens. Keep backups, logs and
migration archives. Do not delete records or repeatedly reinstall packages to
force an unrecognised state through the checks.

An unknown or damaged state is reported rather than silently declared safe.
The separate Recovery package is not yet released. Return to original firmware
is a separate operation: restore modified Canvas firmware through its supported
workflow first and use the appropriate original base; do not promise a
universal downgrade or loss-free recovery for every possible fault.

## Credits and validation

Thanks to **pezso92** for Hungarian translation work and **beanbo** for its
restoration/refinement in PR #352. Thanks to **Neme77**, the #327 testers,
**SVLoneStar**, **Natan**, **arsian**, and everyone who supplied logs and tested
the upgrade branches, including reports in #356, #359, #362 and #364.

The original build receipts remain attached unchanged: their physical-test
flags describe the time of building. Later maintainer and community results
are recorded separately in UNIVERSAL07-PUBLICATION-VALIDATION.json.
Hungarian projection tests pass; native-speaker review of this exact projected
catalogue remains pending. Successful arming of a reader is not necessarily a
successful physical tag-read test. Protocol ACKs are not physical flash readback.
