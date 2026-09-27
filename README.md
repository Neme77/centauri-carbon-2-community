# Centauri Carbon 2 Community Firmware

Community firmware, local printer control and reproducible build tools for the
**ELEGOO Centauri Carbon 2**, maintained by **Neme77**.

> This is an independent community project. It is not affiliated with,
> endorsed by or supported by ELEGOO.

## Repository status

| Component | `main` | Latest published release |
|---|---:|---:|
| Community firmware | V4.2 | [V4.2](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2) |
| CC2 Control | 1.1.30 | 1.1.30 |

The source on `main` can be newer than the latest signed firmware. Release
artifacts, checksums and notes remain attached to their GitHub release.

## What is included

- local web dashboard on port `8081`;
- live status, camera, temperatures, fans and motion controls;
- G-code/3MF file manager, thumbnails and OrcaSlicer upload/print support;
- Canvas material slots, colours, presets and spool selection;
- saved and adaptive mesh display, Side A/Side B profiles and screw levelling;
- object exclusion, protected console and global emergency stop;
- English and Italian interface, dark and light themes;
- Panda/Moonraker compatibility endpoint on port `7125`;
- source, tests, integration scripts and firmware builder logic in this repository.

CC2 Control is available after installation at:

```text
http://PRINTER-IP:8081
```

## Download V4.2

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig"><img alt="Download complete firmware V4.2" src="https://img.shields.io/badge/Download-Firmware%20V4.2-00b8d9?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Multiplatform-Update.zip"><img alt="Update CC2 Control only" src="https://img.shields.io/badge/Update-CC2%20Control%201.1.30-22c55e?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Source-and-Multiplatform-Builder-R4.zip"><img alt="Download CC2 Control source" src="https://img.shields.io/badge/Source-CC2%20Control%201.1.30-6f42c1?style=for-the-badge"></a>
</p>

<p align="center"><a href="https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2">Release notes, builders, checksums and all downloads</a></p>

| Choose this file | When to use it |
|---|---|
| [`CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig) | New installation or complete Community Firmware V4.2 update |
| [`CC2-Control-1.1.30-Multiplatform-Update.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Multiplatform-Update.zip) | The printer already runs Community Firmware and only CC2 Control must be updated |
| [`CC2-Control-1.1.30-Source-and-Multiplatform-Builder-R4.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Source-and-Multiplatform-Builder-R4.zip) | Complete CC2 Control 1.1.30 source and reproducible updater builder |
| [`SHA256SUMS_V4_2.txt`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/SHA256SUMS_V4_2.txt) | Verify downloaded release files |

## Update CC2 Control only

Use the standalone updater when Community Firmware is already installed and a
full firmware reflash is unnecessary. It preserves the LAN code, UI preferences
and material presets, and automatically rolls back if the health check fails.

1. Download and extract
   [`CC2-Control-1.1.30-Multiplatform-Update.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Multiplatform-Update.zip).
2. Open a terminal inside the extracted directory.
3. Run the installer for your operating system.

### Windows PowerShell

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\Install-CC2-Control-1.1.30.ps1 -PrinterIp 192.168.1.103
```

### Linux and macOS

```sh
chmod +x install-cc2-control-1.1.30.sh
./install-cc2-control-1.1.30.sh 192.168.1.103
```

Replace `192.168.1.103` with the printer IP address. Both installers require
`ssh` and `scp` and will request the printer root password. After installation,
wait for service alignment and open `http://PRINTER-IP:8081`; an additional
printer reboot or power cycle is not required.

## CC2 Control 1.1.30 source

The complete, buildable source is always available directly in
[`cc2-control/`](cc2-control/), including the C backend, web interface, startup
scripts, tests, configuration examples and protocol documentation. The release
also provides a separate
[`Source-and-Multiplatform-Builder-R4.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.30-Source-and-Multiplatform-Builder-R4.zip)
for offline use. Firmware binaries, standalone update packages and source
archives are deliberately named and documented separately.

## Source layout

| Path | Contents |
|---|---|
| [`cc2-control/`](cc2-control/) | C backend, web UI, scripts, tests and protocol notes |
| [`builder/current/`](builder/current/) | Current firmware build logic and host-side tests |
| [`builder/v4.1-r8/`](builder/v4.1-r8/) | Historical reproducibility snapshot |
| [`docs/`](docs/) | Installation, architecture, build and testing documentation |
| [`releases/`](releases/) | Historical release notes |

## Build CC2 Control

Native development build:

```sh
cd cc2-control
make clean all CROSS= CC=gcc
python3 -m unittest discover -s tests -p 'test_*.py'
```

The production executable targets 32-bit ARM and uses the toolchain described
in [`docs/BUILD.md`](docs/BUILD.md). Firmware construction also requires a
legally obtained stock firmware package and separately supplied build inputs;
vendor binaries and private signing material are not committed.

## Installation

Use the files and instructions belonging to the same GitHub release:

- [Firmware installation](docs/INSTALL.md)
- [Build and reproduce](docs/BUILD.md)
- [Source and third-party inputs](docs/SOURCE.md)
- [Testing](docs/TESTING.md)

After installation, allow the printer to finish hardware initialisation before
opening CC2 Control. First-run registration now realigns the local services
without requiring an additional power cycle.

## Interface preview

### Dashboard

<p align="center">
  <img src="docs/images/v4.2/dashboard-light-theme.jpg" alt="CC2 Control dashboard" width="1000">
</p>

### Bed Levelling

<table>
  <tr>
    <td align="center"><strong>Dark theme</strong></td>
    <td align="center"><strong>Light theme</strong></td>
  </tr>
  <tr>
    <td><img src="docs/images/v4.2/bed-levelling-dark-theme.jpg" alt="Bed Levelling dark theme"></td>
    <td><img src="docs/images/v4.2/bed-levelling-light-theme.jpg" alt="Bed Levelling light theme"></td>
  </tr>
</table>

## Contributing

Please read [`CONTRIBUTING.md`](CONTRIBUTING.md). Keep changes focused, update
tests with behaviour changes and avoid committing credentials, firmware images,
generated binaries or private keys.

## License and notices

Project code is provided under GPL-3.0-only where the contributors have the
right to license it. Vendor firmware and third-party components keep their own
terms. See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
