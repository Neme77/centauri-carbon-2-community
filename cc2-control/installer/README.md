# CC2 Control updater

Updates an existing, configured CC2 Control installation on a Centauri Carbon 2
with SSH access. The printer must be connected, Idle, and reporting fresh state.
This is an application updater; it does not flash firmware or install SSH.

Extract the entire ZIP before running a launcher. Configuration (including the
LAN code), material presets, UI preferences and the plate library are preserved. The updater keeps
a backup, verifies checksums and the running binary, and waits for MQTT registration
and a printer snapshot for up to 240 seconds, including the launcher delay.
On failure, inspect the output for rollback status.

Windows: double-click `Install-Windows.cmd`, or run:

```powershell
.\Install-CC2-Control.ps1 -PrinterIp 192.0.2.10
```

Linux/macOS:

```sh
sh ./install-cc2-control.sh 192.0.2.10
```

Windows requires the OpenSSH client. SSH credentials are requested by OpenSSH.
After installation, restart the printer while Idle to verify automatic startup
and realign Canvas. The saved LAN code should not be requested again.

## SSH host-key changes

Only for an expected host-key change, add `-ResetHostKey` on Windows or
`--reset-host-key` on Linux/macOS. Recovery requires typing the exact printer
address. It removes only that address from the default `known_hosts` file with
`ssh-keygen -R`; OpenSSH then performs normal verification. Verify the new
fingerprint through a trusted channel before accepting it. Printer keys are
not regenerated. Custom `known_hosts` files and `HostKeyAlias` need manual recovery.

## Restore

The updater prints the backup directory and exact restore command. Run it only
while the printer is connected and Idle. Restore preserves current configuration,
material presets, UI preferences and the plate library. Firmware builds remain in `/opt/inst`;
this updater installs the application in `/opt/usr` and points the service there.

## Rebuild the package

After preparing the ARM component, from the repository root:

```sh
python3 cc2-control/installer/package.py
```

The package uses the prepared ARM binary and UI, standalone scripts and source
archive. It emits a ZIP with explicit payload permissions, checksum files,
launchers, rollback script and source archive. Generated artifacts are not
committed. The packaging script does not compile or sign firmware.

Tests from the repository root:

```sh
python3 cc2-control/tests/test_installer_ssh.py
python3 cc2-control/tests/test_installer_package.py
```

The serial discovery binary was validated through firmware installation,
first configuration with one LAN-code entry, and automatic reconnection after
reboot. Validation of this standalone updater on a printer is a separate step.
