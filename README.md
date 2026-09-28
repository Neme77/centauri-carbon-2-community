# Centauri Carbon 2 Community Firmware

Community firmware, local printer control and reproducible build tools for the **ELEGOO Centauri Carbon 2**, maintained by **Neme77**.

> Independent community project. Not affiliated with, endorsed by or supported
> by ELEGOO.

## Current release

| Component          |                                                                                         Version |
| ------------------ | ----------------------------------------------------------------------------------------------: |
| Community firmware |                 [V4.2](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2) |
| CC2 Control        | [1.1.31](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.1.31) |

Release binaries, standalone updaters, source archives and checksums are kept with the corresponding GitHub Release. The canonical development source is kept directly on `develop`.

## Downloads

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig"><img alt="Download Community Firmware V4.2" src="https://img.shields.io/badge/Download-Firmware%20V4.2-00b8d9?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.31/CC2-Control-1.1.31-Multiplatform-Update.zip"><img alt="Download CC2 Control 1.1.31 updater" src="https://img.shields.io/badge/Update-CC2%20Control%201.1.31-22c55e?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.31/CC2-Control-1.1.31-Source-and-Multiplatform-Builder-R1.zip"><img alt="Download CC2 Control 1.1.31 source" src="https://img.shields.io/badge/Source-CC2%20Control%201.1.31-6f42c1?style=for-the-badge"></a>
</p>

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_BUILDER_V4_2_CC2_CONTROL_1.1.30_R4_RELEASE.zip"><img alt="Download firmware builder R4" src="https://img.shields.io/badge/Builder-Firmware%20R4-f59e0b?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/SHA256SUMS_V4_2.txt"><img alt="Download SHA256 checksums" src="https://img.shields.io/badge/Verify-SHA256SUMS-64748b?style=for-the-badge"></a>
</p>

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2"><strong>Release notes, checksums and all V4.2 downloads</strong></a>
</p>

| Choose this file                                                                                                                                                                                                      | When to use it                                                                                          |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| [`CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_V4_2_STOCK_20260927_160433_d464d184.zip.sig)                                     | New installation or complete Community Firmware V4.2 update                                             |
| [`CC2-Control-1.1.31-Multiplatform-Update.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.31/CC2-Control-1.1.31-Multiplatform-Update.zip)                               | Existing Community Firmware installation: update only CC2 Control, without reflashing the full firmware |
| [`CC2-Control-1.1.31-Source-and-Multiplatform-Builder-R1.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.31/CC2-Control-1.1.31-Source-and-Multiplatform-Builder-R1.zip) | Complete CC2 Control source and reproducible updater builder                                            |
| [`CC2_BUILDER_V4_2_CC2_CONTROL_1.1.30_R4_RELEASE.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_BUILDER_V4_2_CC2_CONTROL_1.1.30_R4_RELEASE.zip)                               | Firmware builder R4                                                                                     |
| [`SHA256SUMS_V4_2.txt`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/SHA256SUMS_V4_2.txt)                                                                                             | Verify downloaded release files                                                                         |

## Already running an older Community Firmware?

If your printer already runs an earlier Community Firmware release, you do not need to reflash the complete firmware just to update CC2 Control.

Use the standalone updater:

[`CC2-Control-1.1.31-Multiplatform-Update.zip`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.31/CC2-Control-1.1.31-Multiplatform-Update.zip)

The standalone updater replaces only CC2 Control and preserves the persistent LAN access code, material presets and interface preferences. CC2 Control 1.1.31 also adds LAN-code revalidation and OrcaSlicer Moonraker-agent Canvas filament synchronization. After the update, CC2 Control realigns its local services automatically, so an additional printer reboot or full power cycle should not be necessary.

For detailed instructions, see [Installation](docs/INSTALL.md).

## Changed the printer LAN access code?

If you change the LAN access code from the printer, update the same code in CC2 Control at `http://PRINTER-IP:8081` under **Settings → Connection**.

Enter the new code, select **Verify**, then **Save changes**.

If CC2 Control does not immediately reconnect after the LAN code change, restart only the CC2 Control service over SSH:

```sh
/etc/init.d/cc2-control restart
```

This restarts CC2 Control without rebooting or power-cycling the printer.

If the old code prevents access to the configuration page entirely, reset only the CC2 Control connection configuration:

```sh
/etc/init.d/cc2-control stop
rm -f /opt/usr/cc2-control/cc2-control.conf
/etc/init.d/cc2-control start
```

Then reload `http://PRINTER-IP:8081` and complete the initial setup with the new LAN access code. Material presets and interface preferences are preserved.

## Features

- local web dashboard on port `8081`;
- live status, camera, temperatures, fans and motion controls;
- G-code/3MF file management, thumbnails and OrcaSlicer integration;
- Canvas material slots, colours, presets and spool selection;
- Side A / Side B mesh handling, adaptive probing and screw levelling;
- object exclusion, protected console and global emergency stop;
- English and Italian interface with persistent dark and light themes;
- Panda/Moonraker compatibility endpoint on port `7125`.

## Source layout

| Path                                   | Contents                                                    |
| -------------------------------------- | ----------------------------------------------------------- |
| [`cc2-control/`](cc2-control/)         | C backend, web UI, scripts, tests and protocol notes        |
| [`builder/current/`](builder/current/) | Current firmware build logic and host-side tests            |
| [`docs/`](docs/)                       | Installation, architecture, build and testing documentation |
| [`CHANGELOG.md`](CHANGELOG.md)         | User-visible project history                                |
| [`AGENTS.md`](AGENTS.md)               | Repository working rules for contributors and coding agents |

Historical versions are preserved through Git tags, GitHub Releases and Git history instead of parallel version-specific documents on `main`.

## Documentation

- [Installation](docs/INSTALL.md)
- [Build and reproduction](docs/BUILD.md)
- [Architecture](docs/ARCHITECTURE.md)
- [CC2 Control](docs/CC2_CONTROL.md)
- [Source and third-party inputs](docs/SOURCE.md)
- [Testing](docs/TESTING.md)
- [Contributing](CONTRIBUTING.md)

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

## AI-assisted development

This project uses AI-assisted development tools as part of the workflow, including support for code review, refactoring, debugging, documentation and selected code generation tasks.

A substantial part of the codebase is written and maintained directly by the project maintainer. Architecture decisions, reverse engineering, hardware testing, printer-side validation and release approval are performed by the maintainer and community testers.

AI-assisted changes are reviewed and validated before being included in a release.

## License and notices

Project code is provided under GPL-3.0-only where the contributors have the right to license it. Vendor firmware and third-party components keep their own terms. See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
