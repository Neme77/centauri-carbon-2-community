# Original ELEGOO layout with Community colours

An optional, separate **manual/offline display-style kit**, requested in [#428](https://github.com/Neme77/centauri-carbon-2-community/issues/428).
It restores the original touchscreen layout, icons and boot artwork while retaining
the Community palette, additional languages, large previews and the corrected Back touch handling.
This changes appearance; it does not reinstall firmware or update CC2 Control.

## Compatibility and download

Use the kit only after installing the **published CC2 Control 1.2.30 Universal update**
on a recognised Community .13 installation. The original Universal 07 download still
includes Control 1.2.29: update Control first. A modified or unmatched installation
is refused; the version label alone is insufficient.

[Download the manual style kit](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Original-Display-Style-1.2.30/CC2-Original-Display-Style-1.2.30.zip)
([release and SHA-256](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Original-Display-Style-1.2.30)).

There is **no bell notification or online installation** for this optional kit.
Keep the whole extracted folder in a writable local location. Windows needs its
OpenSSH client; macOS and Linux need Python 3.9+ and OpenSSH (`ssh` and `scp`).
The Windows route uses PowerShell and does not need Python.

## Install manually

1. Keep the printer cold and idle, heater targets at zero. Know its LAN IPv4 address
   and SSH root password. Confirm the SSH host identity when first connecting.
2. Extract the entire ZIP. Do not run files directly inside the ZIP.
3. Windows: double-click **Check.cmd**. macOS/Linux: open a terminal in the extracted
   folder and run `sh Check.sh`. Enter the printer IP and SSH password when prompted.
4. Wait for **STOCK_STYLE_DISPLAY_READ_ONLY_PASSED**. Check only uploads a private kit
   and reads admission/RFID evidence; it does not replace active files or restart services.
   If it stops, retain the report and ask for help; do not remove guards or retry blindly.
5. Review the passed report, then run **Install.cmd** (Windows) or `sh Install.sh`
   (macOS/Linux). All previous display files are backed up and verified before replacement.
6. Wait for **RFID_FILES_UNCHANGED** and **STOCK_STYLE_DISPLAY_APPLIED** with no error.
   Only then reboot the cold, idle printer once. The installer never reboots automatically.
7. Check layout, colours, languages, Back, previews and normal Canvas/light/panel operation.

`Status.cmd` / `sh Status.sh` reads the saved transaction state. Keep the extracted
folder, `printer-stage.json`, reports and the matching private stage/backup on the printer.
Do not share a stage record between printers or run two style operations concurrently.
Passwords are entered into OpenSSH and are not stored by these launchers.

## Restore Community appearance

On the **same supported release**, with the printer cold and idle, run **Restore.cmd**
or `sh Restore.sh` from the same extracted folder. Wait for **RFID_FILES_UNCHANGED**
and **COMMUNITY_DISPLAY_STYLE_RESTORED**, then reboot manually once.
Restore verifies the backed-up files and refuses unrelated changes.

**Before installing another CC2 Control/display/firmware update, restore Community
appearance with this kit first**, unless that later release explicitly documents
compatibility with this style profile. Future updates may overwrite the optional
appearance. A kit for a later release needs fresh compatibility qualification;
do not use this old Restore over a newer firmware or panel release.

## Validation and scope

The maintainer tested Check, Install, reboot, display operation, Restore and another
reboot on a real printer. Languages, previews, Back, Canvas, light and the panel
worked in both styles. RFID evidence remained unchanged during Install and Restore.
Calibration, identity and active panel files are outside the display write list.
No Canvas firmware transfer or RFID lifecycle action is invoked.

Offline evidence includes 8 ARM constructor cases, 15 installer cases (all 862 targets
in the host cycle and reduced transaction/error cases with original Tina ARM shell
and utilities), and 8 Windows launcher cases. Native-idle/RFID outputs in transaction
fixtures were simulated; physical checks were separately passed on the printer.
The portable SSH launcher adds 10 mocked command-routing/refusal cases; macOS/Linux
interactive execution has not yet been physically tried. It runs the identical
printer-side archive used in the Windows physical trial.

Source, packaging and sanitised qualification: [tools/display-style](../tools/display-style).
Native ELEGOO code/artwork remains ELEGOO's; Community language/preview contributions
retain their existing attribution. Request: **fffuffff** (#428); integration and
physical testing: **Neme77**. See [project credits](CONTRIBUTORS.md).

For help, report starting versions, operation, full error and log. Send private
reports privately and never post passwords or LAN access codes.
