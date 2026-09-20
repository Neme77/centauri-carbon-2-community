# ELEGOO Centauri Carbon 2 Community Firmware

Open-source community firmware, tools and documentation for the **ELEGOO Centauri Carbon 2 (CC2)**, maintained by **Neme77**. The project extends the stock platform while preserving the original ELEGOO ecosystem wherever possible.

> This project is not affiliated with, endorsed by, or supported by ELEGOO.

## Community Firmware V4.0

Firmware V4.0 integrates **CC2 Control 1.1.22**, a lightweight on-printer web dashboard available at `http://PRINTER-IP:8081`.

![CC2 Control](docs/images/v3.9/dashboard-overview.jpg)

V4.0 adds four-screw load-cell bed tramming, protected object exclusion for labelled multi-part prints, a temporary expert G-code terminal unlock, and a minimal read-only Moonraker-compatible bridge for BTT Panda Breath on TCP port 7125. It also corrects the Bed Mesh X orientation and keeps screw-measurement output isolated from the stored 11×11 mesh.

The release retains live telemetry and camera, temperature history, protected controls, custom material presets, ELEGOO Canvas support, 2D/3D Bed Mesh visualisation, and printing from internal memory or USB folders.

Hardware validation and testing contribution: **Barry Green**.

See the [V4.0 feature documentation](docs/FIRMWARE_V4_0.md), [installation guide](docs/INSTALL_V4_0.md), [CC2 Control documentation](docs/CC2_CONTROL.md), and [V4.0 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.0).

## Downloads

| **FIRMWARE V4.0** | **ELEGOO-WEB v4.4.0** |
| :---: | :---: |
| [![Download firmware V4.0](https://img.shields.io/badge/DOWNLOAD-FIRMWARE_V4.0-1769aa?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.0/CC2_V4_0_STOCK_20260920_162601_4ab3dab2.zip.sig) | [![Download ELEGOO-Web v4.4.0](https://img.shields.io/badge/DOWNLOAD-ELEGOO--WEB_v4.4.0-238636?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/elegoo-web-v4.4.0/ELEGOO-Web-v4.4.0-Portable.zip) |
| **[Download firmware (.zip.sig)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.0/CC2_V4_0_STOCK_20260920_162601_4ab3dab2.zip.sig)** | **[Download portable app (.zip)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/elegoo-web-v4.4.0/ELEGOO-Web-v4.4.0-Portable.zip)** |
| Centauri Carbon 2 only · Base 02.01.00.00 · CC2 Control 1.1.22 | Windows 10/11 x64 · .NET Framework 4.8 · 5.1 MiB |
| [Release notes](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.0) · [Installation](docs/INSTALL_V4_0.md) · [SHA-256](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.0/CC2_V4_0_STOCK_20260920_162601_4ab3dab2.zip.sig.sha256.txt) | [Release notes](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/elegoo-web-v4.4.0) · [Setup](elegoo-web/README.md) · SHA-256: `704ea08d745ecb16d193852382037a01f6f813b32a3e9422030d2b8fa8097d2f` |

Copy the firmware `.zip.sig` unchanged to USB storage; do not extract or rename it.

### Builder and source packages

[![Download CC2 Builder V4.0](https://img.shields.io/badge/DOWNLOAD-CC2_BUILDER_V4.0-8250df?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.0/CC2_BUILDER_V4_0_UPDATE_v1.1.22_PANDA.zip)

[![Download CC2 Control source](https://img.shields.io/badge/DOWNLOAD-CC2_CONTROL_1.1.22_SOURCE-2ea44f?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.0/CC2-Control-v1.1.22-Source.zip)

SHA-256:

- Firmware: `25cefa00f43ec43ced3a8b00e4240bbcaad9a8dba48c3c2154a6fa7682f8b428`
- Builder overlay: `0e0d961dfb575a3eecf299167bc6f5b6231f90456fc61c3e95b274a9aa8a37be`
- CC2 Control source: `654215f7a5e5cc3d21b7704fa987e9f378fa86c7d47cc3e2f118573c71bb446c`

Previous firmware: [V3.9 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V3.9).
