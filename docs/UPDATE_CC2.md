# CC2 Control 1.2.22 update — Community Firmware V4.4 / V4.4.1 / V4.4.2 only

[Italiano](UPDATE_CC2.it.md) · [Homepage](../README.md)

1.2.22 includes the redesigned Dashboard and Control layouts, shared Canvas/external material editing and native external-spool controls, optional Spoolman sync, browser-local LAN-code remembering, print-completion notice, bounded spool history and opt-in timed bed heating. Preferences and libraries are retained.

**Reboot only on first activation of the camera fix.** It is already included since 1.2.19; if active, no additional reboot is needed for this app update. Complete firmware V4.4.2 already reboots and activates the fix. If the old downloader reports HTTPS failure, download the signed `.cc2update` on your computer and upload it through the panel, or use the computer installer. Close viewers before updating.

**Only an existing, configured Community Firmware V4.4, V4.4.1 or V4.4.2 installation is supported. Do not install on V4.3, original ELEGOO firmware or another printer model.** Vendor base 02.01.01.07 alone is insufficient: the installer checks the exact community printer executable on disk and in the running process, plus the update daemon, before stopping the service. Unknown fingerprints are refused.

This ZIP updates the application over SSH. It does not flash the firmware image, install SSH or replace the callback module. It also installs the fingerprint-qualified camera startup component described above. Canvas uses MQTT by default; Canvas UDS and Quiet controls are independent optional experiments. LAN/WAN, lights and the existing validated features remain included.

## Choose your update method

- **No Settings → Updates tab yet:** follow the computer-assisted ZIP installation below once. It installs the updater helpers with CC2 Control.
- **Updater panel already installed:** open Settings → Updates, then Check for updates. Enter the printer LAN code, choose Download and verify, and only after verification choose Install and confirm separately. Alternatively upload `CC2-Control-1.2.22-V4.4.cc2update` from the release. The panel does not accept the installer ZIP.
- If the panel already reports 1.2.22, you are current; no reinstallation is required. A manual Check refreshes the cached index.

The same idle/cold/zero-target requirements apply to either method. Verification alone does not install anything. The online index and application are signed; incompatible bases and same/older application versions are refused. Do not turn power off during installation. Wait for completion, reload and recheck connection/Canvas/settings after an idle reboot.

The camera header on the Dashboard contains **Keep active**. Enable it, then explicitly Start live view to retain the stream across navigation. A reload retains the preference but does not automatically start video. Optional features are grouped under **Settings → Integrations → Experimental integrations**.

## Why the LAN code is required

The LAN code confirms that you are authorized to update this printer. **Check for updates** does not require it; **Download and verify**, uploading a `.cc2update` file and **Install** do. The package signature checks the application's origin, while the LAN code confirms authorization on the printer.

1. On the printer display, open **Settings → LAN only** and read the access code. Distinguish the letter **O** from the digit **0** and preserve letter case.
2. In **Settings → Updates**, enter that code in **LAN access code**. Optionally enable **Remember LAN code** in Settings → Connection: the correct code is verified and saved in this browser, then prefilled and masked in protected forms. Forget removes the saved browser value. Every action still requires its normal confirmation.
3. Download/verify the package, then select **Install** and confirm. Entering the code alone does not start installation.

**This is not the SSH/root password or a GitHub code.** Updates also work in the supported hybrid WAN mode using the same code configured in CC2 Control. If you temporarily switch to LAN only to read it, return to your usual mode and check that CC2 Control is connected before updating.

A factory reset may change the code: enter the new code in the **Connection** tab, choose **Change / Revalidate** and confirm. The service saves the code and restarts; verify the connection, then enter the new code in **Updates** too. The panel compares your confirmation with the code configured in the service. After five incorrect attempts, wait 60 seconds before retrying. Do not share the code in public GitHub comments or screenshots.

## 1. Prepare

1. Download **CC2-Control-1.2.22-Multiplatform-Update.zip** and its checksum from the [application release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.22-V4.4).
2. Verify the ZIP SHA-256 against the release checksum:

~~~powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\CC2-Control-1.2.22-Multiplatform-Update.zip'
~~~

3. Confirm a supported V4.4/V4.4.1/V4.4.2 firmware is installed and CC2 Control reports **Printer connected**.
4. Wait for **Idle**, with no print or calibration running, nozzle and bed at most **50 °C**, and both targets **0 °C**. Close live viewers and other control pages during installation.
5. Note the printer IP. The computer needs local network access and OpenSSH (ssh and scp).
6. Extract the entire ZIP into a new folder. Do not run launchers inside the compressed ZIP or use this ZIP in the printer USB firmware-update menu.

LAN credentials, material presets, appearance, plate/nozzle data and spool inventory are preserved. An automatic backup records the previous application and service launcher.

## 2. Install

### Windows

Open the extracted folder and double-click **Install-Windows.cmd**; enter the printer IP.
Alternatively, open PowerShell in that folder:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-CC2-Control.ps1 -PrinterIp 192.168.1.50
~~~

Replace the example IP. Windows OpenSSH Client must be installed.

### Linux and macOS

Open a terminal in the extracted folder:

~~~sh
sh ./install-cc2-control.sh 192.168.1.50
~~~

Replace the example IP.

### SSH and completion

Username **root**, using your SSH password ([SSH reference](FIRST_RUN.md#ssh-access-reference)). Password entry is invisible and may be requested more than once. This is separate from the LAN access code.

Verify the printer host-key fingerprint before accepting a first SSH connection. For an expected host-key change only, follow [host-key recovery](../cc2-control/installer/README.md#ssh-host-key-changes).

The installer verifies payload, actual firmware and state; creates the backup; stops **only CC2 Control**; installs the ARM application/UI; restarts the service. Allow up to **240 seconds** for health, running-binary checksum, MQTT registration and printer snapshot.

Success prints **INSTALLATION PASSED**, the backup directory and exact restore command. If it fails, inspect rollback status and keep the backup path. Do not start a print until the previous or updated service works.

## 3. Verify

1. Open http://PRINTER-IP:8081 and reload; use Ctrl+F5 if the old interface persists.
2. Check **1.2.22**, **Printer connected**, temperatures and Canvas slots. The saved LAN code should not be requested again.
3. Select **Magyar** under **Settings → Appearance**. Eight languages are available.
4. Inspect plates/nozzles in **Bed Levelling**. Calibration accepts bed temperature **40–110 °C**, soak **0–60 minutes** (default **10**) and nozzle. Keep the bed clear before confirming.
5. Each plate keeps measurements by temperature and nozzle. The print dialog uses the nearest bed-temperature measurement or your chosen measurement, with the selected nozzle Z correction.
6. Restart once while Idle, then verify automatic startup, connection and Canvas again.

A factory reset and LAN-code replacement are not required. The printer display retains vendor base 02.01.01.07; the application version is 1.2.22.

## 4. Restore the previous application

While connected, Idle and cold with both heater targets zero, use the exact backup printed by your installation:

~~~sh
ssh root@PRINTER-IP
sh /opt/usr/cc2-control-backup-YOUR-TIMESTAMP-PID/restore.sh
~~~

Replace IP and backup placeholders. Current credentials, materials, preferences, plates/nozzles and spools are retained. For a firmware-integrated predecessor the original launcher can return to the binary under /opt/inst; the /opt/usr backup may contain only persistent data.

This restores the **application**, not an older ELEGOO firmware. Firmware downgrade restrictions remain unchanged. Do not reuse another printer backup.


Plate library: older apps enforcing a 40 °C minimum cannot read new 30–39 °C measurements and may discard uncalibrated profiles on save. Back up the library before reverting.



## Separate extended Canvas pull-back component

The optional experimental R4 download raises the accepted Canvas pull-back maximum to 1000 mm on exact qualified V4.4.1/V4.4.2 runtimes with CC2 Control 1.2.21 or 1.2.22. It is not included or activated by this application update, and it does not extend external-spool movement. Follow its own README and Check step. Remove the component and reboot before ordinary app/firmware updates; removal returns supported values above 350 mm to 350 mm. Physical distances and reload settings depend on the filament path.
