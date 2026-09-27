# Installing Community Firmware V4.2

## Before installation

This firmware is for the **ELEGOO Centauri Carbon 2**.

Verify the firmware archive checksum before copying it to USB.

Firmware:

```text
CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig
```

SHA-256:

```text
4e9944dd0b3e5eaff24bdaf6ed34d32002f25a069e26ec05549b612e94c9ec6b
```

## Installation

1. Copy the firmware package to the USB drive as required by the normal Centauri Carbon 2 local-update process.
2. Start the local firmware update.
3. Wait for the update to complete fully.
4. Allow the printer to reach its normal post-update state.
5. **Do not start a print, open Canvas controls, run levelling, or perform any other operation yet.**

## Mandatory power cycle

> [!IMPORTANT]
> **Switch the printer completely off, then power it on again before doing anything else.**
>
> This full power cycle is mandatory after installation so **Canvas** and the related background services can realign and initialize correctly.

Do not treat this step as optional.

After powering the printer back on, wait approximately **30–60 seconds** before using CC2 Control, Canvas, OrcaSlicer integration or other background-dependent functions.

## Verify CC2 Control

Open:

```text
http://PRINTER-IP:8081
```

The interface should report CC2 Control:

```text
1.1.27
```

You can also verify locally over SSH:

```sh
wget -qO- http://127.0.0.1:8081/api/health
```

## If Canvas appears out of sync

If the mandatory full power cycle was skipped, shut the printer down completely and power it on again before troubleshooting anything else.

## Existing V4.1 users

If you do not want to reflash the whole firmware, CC2 Control 1.1.27 can also be installed using the standalone multiplatform updater. See [INSTALL_CC2_CONTROL_1_1_27.md](INSTALL_CC2_CONTROL_1_1_27.md).
