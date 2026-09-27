# ELEGOO Centauri Carbon 2 Community Firmware

Open-source community firmware, tools and documentation for the **ELEGOO Centauri Carbon 2 (CC2)**, maintained by **Neme77**.

> This project is not affiliated with, endorsed by, or supported by ELEGOO.

## Community Firmware V4.2

**V4.2** integrates **CC2 Control 1.1.27** and retains the validated V4.1/R8 persistent-storage startup fix.

> [!IMPORTANT]
> **Mandatory power cycle after installation**
>
> After installing Community Firmware V4.2, **do not use the printer immediately**.
> **Switch the printer completely off, then power it on again before doing anything else.**
> This full power cycle is required to correctly realign and reinitialize **Canvas** and the related background services.
>
> After power-on, wait approximately **30–60 seconds** before normal use.

### V4.2 highlights

- CC2 Control **1.1.27** integrated into the firmware
- unified **Bed Levelling** workflow
- saved Side A / Side B mesh visibility
- four-screw corrections displayed in **microns**
- guarded **Optimized reference adjustment**
- operational dashboard **Quick Actions**
- global one-second press-and-hold **Emergency Stop**
- compact, navigable **Settings** panels
- persistent **Dark** theme and new monochrome **Light** theme
- theme shared between browser and OrcaSlicer
- all CC2 Control 1.1.26 file, upload, Canvas, thumbnail and print fixes
- original `elegoo_printer` retained unchanged

CC2 Control is available at:

```text
http://PRINTER-IP:8081
```

### Release files

| Component | File | SHA-256 |
|---|---|---|
| Firmware V4.2 | `CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig` | `4e9944dd0b3e5eaff24bdaf6ed34d32002f25a069e26ec05549b612e94c9ec6b` |
| Builder V4.2 | `CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip` | `dd38b9aa4ecd779a246403aa3ba8c075f231d538b12694fa4d7303cdf01b71dd` |
| CC2 Control 1.1.27 updater | `CC2-Control-1.1.27-Multiplatform-Update.zip` | `17012fc53eaca3bd3ab1a1829172c9d12e8ed56dcf4b135d9e17a8acfc6fdccc` |
| CC2 Control 1.1.27 source | `CC2-Control-1.1.27-Complete-Source-and-Builder.zip` | `1c3039678c27cbad6e9916dc203d4ea1b87910c7c35f7702c6a1195b33a2be63` |

Direct downloads after the release assets are uploaded:

- [Firmware V4.2](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig)
- [Builder V4.2](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip)
- [CC2 Control 1.1.27 updater](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.27-Multiplatform-Update.zip)
- [CC2 Control 1.1.27 source](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/CC2-Control-1.1.27-Complete-Source-and-Builder.zip)
- [Combined checksums](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.2/SHA256SUMS_V4_2.txt)

See [V4.2 release notes](releases/v4.2.md), [installation](docs/INSTALL_V4_2.md), [firmware details](docs/FIRMWARE_V4_2.md), [build guide](docs/BUILD.md), and [source/reproducibility notes](docs/SOURCE.md).

## Already running Community Firmware V4.1?

You can update **CC2 Control only** to 1.1.27 without reflashing the full firmware using:

```text
CC2-Control-1.1.27-Multiplatform-Update.zip
```

The updater supports Windows, Linux and macOS and preserves existing persistent settings.

> [!IMPORTANT]
> After the CC2 Control 1.1.27 update completes, **switch the printer completely off and power it on again before using it** so Canvas and the background services are realigned.

See [Installing CC2 Control 1.1.27](docs/INSTALL_CC2_CONTROL_1_1_27.md).

## CC2 Control 1.1.27

### Bed Levelling
- one Bed Levelling entry for mesh and screw levelling
- Side A / Side B profiles and active mesh in one workflow
- screw corrections shown in microns
- guarded reference optimization suggestion
- no automatic screw movement

### Controls and safety
- Quick Actions: Home All, All Heaters Off, Fans Off and Motors Off
- global Emergency Stop on every page
- one-second press-and-hold protection
- printer-state safety guards remain active

### Settings and appearance
- Connection, Safety, Integrations, Appearance and About are separate panels
- compact layout
- persistent Dark theme
- new monochrome Light theme
- appearance shared with OrcaSlicer

## Historical V4.1 release

Community Firmware **V4.1 / R8** remains preserved as the previous release and historical reproducibility snapshot.

- [V4.1 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.1)
- [V4.1 R8 builder snapshot](builder/v4.1-r8/README.md)

## Documentation

- [Firmware V4.2](docs/FIRMWARE_V4_2.md)
- [Install Firmware V4.2](docs/INSTALL_V4_2.md)
- [Install CC2 Control 1.1.27](docs/INSTALL_CC2_CONTROL_1_1_27.md)
- [CC2 Control](docs/CC2_CONTROL.md)
- [Reproducible builds](docs/BUILD.md)
- [Source code and reproducibility](docs/SOURCE.md)
- [Technical research](docs/CC2_RESEARCH.md)

## License and third-party material

See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
