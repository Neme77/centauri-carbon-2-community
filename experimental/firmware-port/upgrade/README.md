# Upgrade preparation

Follow [the firmware upgrade guide](../../../docs/FIRMWARE_UPGRADE.md).
The distributed kit contains the qualified static ARM `cc2-upgrade-probe` and
`cc2-upgrade-evidence` executables with checksums; generated executables are
release assets, not repository inputs.

`upgrade_evidence.c` is read-only. It classifies supported Canvas state without
executing retained helpers or accessing the serial device. The shell requires
the qualified native 4.4.3 baseline and queries the console for idle/cold
conditions before backing up and quarantining an explicit path allowlist.

Evidence qualification covered host, ARM and sanitizers, synthetic version
identities and a retained real restored component. Quarantine fixtures ran on
host Bash and original ARM BusyBox ash. Results do not imply every historical
installation was physically tested. Existing unfinished or unknown state is
refused rather than guessed.

The PC backup helper archives persistent Community paths; it is not a complete
disk backup. The guide requires panel exports and separate personal-data
backups as appropriate. No tool here silently removes firmware protections or
automatically flashes the printer after preparation.
