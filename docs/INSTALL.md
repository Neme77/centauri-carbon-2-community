# Installation

Always install artifacts and follow instructions from the same GitHub release.

## Full firmware

1. Verify the downloaded firmware checksum against the release checksum file.
2. Install the signed package using the printer's supported update procedure.
3. Allow the normal update reboot to complete.
4. Wait until the printer has completed hardware initialisation.
5. Open `http://PRINTER-IP:8081` and enter the printer LAN access code.
6. Allow several seconds for CC2 Control to register and realign its services.

V4.2 with CC2 Control 1.1.30 does not require an additional manual power cycle.
If the printer or Canvas is not ready, wait for initialisation to finish before
attempting control actions.

## CC2 Control-only update

Use the multiplatform updater attached to the relevant release. It supports
Windows, Linux and macOS over SSH and preserves persistent settings unless the
release notes explicitly say otherwise.

## Verification

```sh
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/v1/system/info
wget -qO- http://127.0.0.1:7125/server/info
```

Do not install files from different releases as a mixed set.
