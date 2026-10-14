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

For an optional checksum check in PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\YOUR-FIRMWARE.zip.sig'
```

Compare the result with the checksum published for that exact firmware file.

## 2. Install and allow the printer to start

1. With the printer idle, use the signed `.zip.sig` file through its supported firmware-update procedure, following that release's installation instructions. Keep the signed file intact.
2. Let the update and its normal reboot finish. Do not interrupt power during the update.
3. Reconnect the printer to your local network if necessary and find its **IP address** on the printer's network screen.
4. Wait for printer services to finish starting before completing CC2 Control setup. The release keeps a delayed CC2 Control startup to allow vendor initialization.

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

## If the LAN code changes

Open **Settings → Connection**, enter the replacement code and select **Change / Revalidate**, then confirm. CC2 Control saves it and restarts its own service. This button completes the replacement; there is no separate “Save changes” step for the LAN code in this interface.

If it still does not connect, recheck the current code on the display, the IP address and network access. A code saved successfully does not by itself mean the printer has accepted it: wait for **Printer connected**.

For service restart or connection-configuration recovery over SSH, see [Installation troubleshooting](INSTALL.md#recovery-if-the-old-code-blocks-configuration-access). A configuration reset is a recovery option, not a routine first-installation step.

## Next

Follow [OrcaSlicer setup and Canvas synchronization](ORCASLICER.md), then upload a small sliced file and confirm its filament-slot mapping before printing.
