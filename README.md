# Centauri Carbon 2 Community

Independent community project maintained by **Neme77** for the ELEGOO Centauri Carbon 2.

> This project is not affiliated with, endorsed by, or supported by ELEGOO.

## Downloads

> **Firmware v3.8 and ELEGOO-Web v4.2.0: packages prepared; release asset upload pending.**
> The links below open the package information. Direct download buttons will be enabled once the assets are published.

| **FIRMWARE v3.8** | **ELEGOO-WEB v4.2.0** |
| :---: | :---: |
| **[Firmware package & installation](releases/v3.8.md)** | **[Windows portable package & setup](releases/elegoo-web-v4.2.0.md)** |
| Centauri Carbon 2 · Base 02.01.00.00 | Windows 10/11 x64 · .NET Framework 4.8 |

[Browse published releases](https://github.com/Neme77/centauri-carbon-2-community/releases)

## Firmware v3.8

Firmware v3.8 is based on the verified ELEGOO **02.01.00.00** firmware and keeps the v3.7 feature set while completing local access in WAN/cloud mode.

### Firmware features

- Root SSH access.
- Extended GUI Z-offset range.
- Dual Trust v2: official ELEGOO packages remain accepted while community-signed updates can also be installed.
- Local HTTP access on port 80 in both LAN-only and WAN/cloud modes.
- File upload while a print is already running when the client permits it.
- Live OrcaSlicer temperature updates in WAN/cloud mode without manual refresh.
- Local webcam access in WAN/cloud mode while the Matrix service remains operational.
- Tested WAN -> LAN -> WAN mode switching.

The existing four-client video limit is unchanged in v3.8.

Read the dedicated [firmware v3.8 documentation](docs/FIRMWARE_V3_8.md), [installation guide](docs/INSTALL.md), [build guide](docs/BUILD.md), and [test record](docs/TESTING.md).

## ELEGOO-Web v4.2.0

ELEGOO-Web is a small portable Windows launcher for the printer's local web interface. It starts a local server on the PC, stores the printer connection parameters, opens the UI, and provides a QR code for phones or tablets on the same LAN.

**Important:** ELEGOO-Web communicates with the printer over the local network. It does not use a USB cable. USB media is used only for offline firmware installation.

Version 4.2.0 replaces the Go/VBS/PowerShell launcher with a direct C# GUI. The portable package includes the compiled application; no compilation is required for normal use.

Read the separate [ELEGOO-Web v4.2.0 README](elegoo-web/README.md).

## Repository contents

- `cc2_builder_v3_8/` - v3.8 builder, patches, public keys, tools, and tests.
- `launch_helpers_v3_8/` - Windows/WSL build launcher.
- `elegoo-web/` - ELEGOO-Web documentation and original wrapper source.
- `docs/` - installation, build, firmware, and validation documentation.
- `releases/` - release notes and publication records.

The v3.7 source remains in the repository as historical material.

## Files intentionally excluded

The Git source tree does not contain:

- ELEGOO stock firmware or modified vendor executables;
- private signing keys or the firmware AES key;
- printer credentials or personal configuration files;
- the tested `elegoo_printer` binary reference;
- third-party web UI and server binaries used by the portable application.

Release assets are distributed separately from the source tree. Users rebuilding from source must supply legally obtained external components. See [BUILD.md](docs/BUILD.md).

## License and warranty

Original project contributions are licensed under **GPL-3.0-only**. Existing third-party notices and terms continue to apply to their respective material. See `LICENSE` and `NOTICE.md`.

Modified firmware can damage or disable a printer. Use it only on the supported model and firmware base, keep the official recovery package available, and proceed at your own risk. No warranty is provided.

