# CC2 Control application installer — V4.4 family

Use this computer-assisted installer once when Settings → Updates is not yet available. Existing panel users can download and verify signed updates from GitHub, then confirm installation separately. See the English/Italian installation guides linked below.

**Community Firmware V4.4, V4.4.1 and V4.4.2 (ELEGOO base 02.01.01.07) are supported.** The installer fingerprints the installed and running community printer executable and update daemon. V4.3, stock firmware and unknown fingerprints are refused before backup or service stop.

Extract the entire ZIP on the computer. Windows: Install-Windows.cmd. Linux/macOS: sh ./install-cc2-control.sh PRINTER-IP. Requires local network access and OpenSSH. SSH username root, using your SSH password; this is separate from the LAN access code.

Printer must be configured, connected, Idle and cold: nozzle/bed at most 50 °C, both targets zero. No print or calibration should be running. Configuration, materials, UI preferences, plates/nozzles and spools are retained. Backup and rollback are included. Health, running binary checksum, MQTT registration and snapshot are checked for up to 240 seconds.

**A full manual printer reboot while Idle is required only on first activation of the camera fix. It is already active for users coming from 1.2.19 or full firmware V4.4.2.** The authenticated installer can also stage the fingerprinted camera libraries and startup hook described in the release notes. The printer executable and callback runtime stay in place. See README-TEST.md and README-TEST.it.md for the component scope, compatibility, rollback and restart instructions.

[Detailed English instructions](https://github.com/Neme77/centauri-carbon-2-community/blob/develop/docs/UPDATE_CC2.md) · [Istruzioni italiane](https://github.com/Neme77/centauri-carbon-2-community/blob/develop/docs/UPDATE_CC2.it.md)

## SSH host-key changes

Only for an expected change use -ResetHostKey (Windows) or --reset-host-key (Linux/macOS). Type the exact printer address to confirm. Only that address is removed from default known_hosts via ssh-keygen -R. Verify the new fingerprint before accepting. Custom known_hosts and HostKeyAlias require manual recovery.

## Restore

Use the backup directory and exact restore command printed by your installation while connected, Idle and cold with heater targets zero. Persistent data is retained. A firmware-integrated predecessor uses /opt/inst; the updater uses /opt/usr and restores the previous init script as needed. Do not use another printer backup.

## Rebuild

Run the ARM build, then python3 cc2-control/installer/package.py with --prepared pointing to the application dist directory. Package includes checksums, explicit permissions, source archive, launchers and restore script. It contains no vendor firmware or private signing keys. Run installer/package regression tests and record printer validation separately.
