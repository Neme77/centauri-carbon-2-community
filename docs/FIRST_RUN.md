# First installation and LAN access code

[Italiano](FIRST_RUN.it.md) · [Full upgrade guide](FIRMWARE_UPGRADE.md) · [Homepage](../README.md)

Current release: **V4.4.4 Universal 07 + Hungarian corrections**, based on
**ELEGOO 02.01.01.13**, with **CC2 Control 1.2.29**.

## 1. Download

Get [CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig) from the [V4.4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4).
SHA-256: `a140564016c3ca7402cb91e18434dc350c183b9cdf08a8b90e12c462d5978a2e`. Use the matching checksum attached to that release.
This is complete firmware, not a CC2 Control-only updater or a source ZIP.

## 2. Install

Follow the [installation steps](FIRMWARE_UPGRADE.md): finish printing, keep
your backup, copy the unchanged signed file to the USB root, then select
**Settings → Check for Updates → Offline Update** on the printer.

First-ever installations from original .13 firmware do not need a probe.
Recognised Community upgrades also migrate automatically with this package:
no routine preparation script, downgrade, cleanup or Canvas restoration.
Complete any ongoing Canvas action and requested reboot before updating.
A factory reset is not required; settings are retained.

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


## Updates, language and support

Use **Settings → Appearance** for CC2 Control language and theme. This release
corrects Hungarian web translations; it does not change touchscreen languages.
Full firmware uses Offline Update; application-only updates must declare
compatibility with the installed base.

If the page does not open, allow startup to finish and check the current IP.
If it opens but cannot connect, recheck the printer LAN code rather than
resetting the printer. For an upgrade failure, retain logs and backups and
[ask for help](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose).
See [installation troubleshooting](INSTALL.md) and the [upgrade guide](FIRMWARE_UPGRADE.md)
for support and the separate return-to-original considerations.
