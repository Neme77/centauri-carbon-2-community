# First installation and LAN access code

[Italiano](FIRST_RUN.it.md) · [OrcaSlicer and Canvas sync](ORCASLICER.md) · [Homepage](../README.md)

This guide covers Community Firmware **V4.4 with CC2 Control 1.2.3**, built on the official **CC2 02.01.01.07** base. V4.4 was officially released on 13 October 2026. Download the firmware, source builder and checksums from the [V4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4).

**CC2 Control-only compatibility:** this release includes the application in the complete V4.4 firmware. Do not install this new CC2 Control separately on V4.3; a standalone application updater is deferred.

## 1. Choose the installation file

Open the project's [GitHub Releases](https://github.com/Neme77/centauri-carbon-2-community/releases) and read the notes for the release you are installing.

| File | Purpose |
| --- | --- |
| Signed firmware ending in `.zip.sig` | Complete printer firmware installation. |
| Builder ZIP | Builds the firmware on a computer; it is not an installable printer update. |
| CC2 Control-only updater, if provided with that release | Updates an existing compatible Community Firmware installation. It does not install the complete firmware. |
| SHA-256 checksum file | Verifies the corresponding downloaded files. |

Use files from the same release. V4.4 uses the new 02.01.01.07 base; an older builder or firmware input is not interchangeable. This project is for the **Centauri Carbon 2**, not the first Centauri Carbon.

Before installation, verify the checksum in PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\CC2_V4_4_STOCK_20261013_001035_414d6629.zip.sig'
```

For the published V4.4 firmware above, the expected SHA-256 is `fa06d40104ba9d90df4ffd6c5183626995f2e6c823763c684cd823e8fe53a729`. Compare it with [SHA256SUMS.txt](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/SHA256SUMS.txt). If it differs, download the file again and do not install it.

## 2. Install and allow the printer to start

1. Download **`CC2_V4_4_STOCK_20261013_001035_414d6629.zip.sig`** from the [V4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4) and verify its checksum. This complete package includes the base firmware; the source-builder ZIP is not an installation file.
2. With the printer idle, copy the signed **`.zip.sig` unchanged into the root directory of a USB drive** the printer can read. Do not extract it, rename it to `update.bin`, or copy only its inner files.
3. Insert the USB drive into the printer. On the display, open **Settings → Check for Updates → Offline Update** and select the signed package. Labels can differ with the display language. Confirm installation on the printer.
4. Let installation and the normal reboot finish. Keep power connected and leave the USB drive inserted until the update is complete.
5. Reconnect to the local network if needed, read the printer IP on its network screen and allow services to finish starting before opening CC2 Control.
6. After startup, check the printer base version **02.01.01.07** and CC2 Control **1.2.3**. The printer display shows the vendor base, while Community Firmware **V4.4** is the project release name.

The USB menu path is documented in the [OpenCentauri CC2 update procedure](https://docs.opencentauri.cc/software/updates-cc2/#updating-locally-via-usb).

A factory reset is not required for this update. Existing CC2 Control credentials, material profiles and interface preferences are normally retained; a factory reset can change the LAN access code.

## 3. Read the LAN access code on the printer

On the printer's display, open **Settings → LAN Only** and reveal the current **LAN access code**. This menu path was confirmed by the maintainer on the release printer.

Copy the code exactly, preserving uppercase and lowercase. In particular, distinguish **O** from **0**, and **I** from **1**. Use the LAN access code, not your Wi-Fi password or an account-pairing QR/PIN. You do not need to type the printer serial: CC2 Control discovers it.

The computer or phone used for setup must be able to reach the printer on the same local network.

## 4. Complete the first setup

1. In a browser, open `http://PRINTER-IP:8081`, replacing `PRINTER-IP` with the address you just read. For example: `http://192.168.1.50:8081`.
2. On the first-configuration screen, enter the LAN access code and select **Connect printer**.
3. Allow CC2 Control to save the code, restart its service and synchronize the printer and Canvas. Credentials are kept locally on the printer.
4. If the screen says synchronization is taking longer, wait and reload the page.
5. Check that the header reports **Printer connected**, that temperatures appear, and that the Canvas page shows the printer's slots when a Canvas is connected.

Successful setup does not require an additional manual power cycle. A clean setup asks for the LAN code; an installation that kept its configuration may connect without asking again.

## 5. Select your language and theme

Open **Settings → Appearance**. The interface supports **English, Italian, French, Simplified Chinese, Russian, Polish and Czech**. Themes: **Dark, Light, Dracula, Nord, Monokai and Solarized Light**. Saved preferences are shared by the browser and OrcaSlicer's embedded CC2 Control page.

## If the page does not open

Check that the printer has completed startup, that its IP has not changed, and that the browser uses `http://PRINTER-IP:8081`. The computer must be able to reach that local address. If CC2 Control opens but remains waiting for the printer, recheck the current LAN code; a factory reset may have changed it. Do not reset the whole printer merely to re-enter the code.

## If the LAN code changes

Open **Settings → Connection**, enter the replacement code and select **Change / Revalidate**, then confirm. CC2 Control saves it and restarts its own service. This button completes the replacement; there is no separate “Save changes” step for the LAN code in this interface.

If it still does not connect, recheck the current code on the display, the IP address and network access. A code saved successfully does not by itself mean the printer has accepted it: wait for **Printer connected**.

For service restart or connection-configuration recovery over SSH, see [Installation troubleshooting](INSTALL.md#recovery-if-the-old-code-blocks-configuration-access). A configuration reset is a recovery option, not a routine first-installation step.

## SSH access reference

The Community Firmware provides local SSH access for maintenance:

| Setting | Default |
| --- | --- |
| Username | `root` |
| Password | `MTY4ODE2` |

From PowerShell or a terminal, run `ssh root@PRINTER-IP`, replacing `PRINTER-IP` with the printer’s local IP, and enter the password when prompted. Password entry is not displayed. If you have changed the root password, use your own replacement. This is the SSH password, separate from the printer LAN access code used by CC2 Control. These credentials refer to the Community Firmware; SSH is not required for normal browser setup.

## Return to original ELEGOO firmware

[Download original CC2 02.01.01.07 firmware](https://public.suchmeme.nl/cc2-02.01.01.07-1cbee81a985601d1944cea50773793ba-release-abroad.zip.sig). This is an externally hosted mirror of the official package, not an ELEGOO-hosted download. Its bytes match the exact official base pinned by the V4.4 builder.

SHA-256: `278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef53ac5f406`.

**ELEGOO blocks downgrades to older firmware.** This link provides the original **same-version 02.01.01.07** package; it does not enable a downgrade to 02.01.00.00 or earlier. Reinstalling the stock package is a separate operation from a factory reset, which does not replace the installed firmware.

Use the same USB/offline-update procedure above with the original package. File identity has been verified, and the maintainer confirmed a successful return from V4.4 to this same-version original firmware on the printer. If the printer refuses the package, stop and record its message; do not change version metadata or bypass its checks. Original firmware does not include CC2 Control or community features.

## Next

Follow [OrcaSlicer setup and Canvas synchronization](ORCASLICER.md), then upload a small sliced file and confirm its filament-slot mapping before printing.
