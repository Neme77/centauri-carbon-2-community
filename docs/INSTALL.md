# Installation

Always install artifacts and follow instructions from the same GitHub release.

## Full firmware

1. Verify the downloaded firmware checksum against the release checksum file.
2. Install the signed package using the printer's supported update procedure.
3. Follow the release-specific restart instructions.
4. Wait until the printer has completed hardware initialisation.
5. Open `http://PRINTER-IP:8081` and enter the printer LAN access code.

The published V4.2 image requires one complete power cycle immediately after
installation. Development builds may include newer first-run recovery logic;
the release notes remain authoritative for a signed image.

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
