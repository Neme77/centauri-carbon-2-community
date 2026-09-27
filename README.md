# Centauri Carbon 2 Community Firmware

Community firmware, local printer control and reproducible build tools for the
**ELEGOO Centauri Carbon 2**, maintained by **Neme77**.

> This is an independent community project. It is not affiliated with,
> endorsed by or supported by ELEGOO.

## Repository status

| Component | `main` | Latest published release |
|---|---:|---:|
| Community firmware | V4.2 development | [V4.2](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2) |
| CC2 Control | 1.1.28 | 1.1.27 |

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

The currently published V4.2 firmware requires one complete power cycle after
installation. Follow the release instructions before opening CC2 Control.

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
