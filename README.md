# Centauri Carbon 2 Community Firmware

Community firmware, local printer control and reproducible build tools for the
**ELEGOO Centauri Carbon 2**, maintained by **Neme77**.

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig"><img src="https://img.shields.io/badge/DOWNLOAD-Firmware%20V4.4.2-087f8c?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Download complete firmware V4.4.2"></a>
  &nbsp;
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-Multiplatform-Update.zip"><img src="https://img.shields.io/badge/DOWNLOAD-CC2%20Control%201.2.21-2563eb?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Download CC2 Control 1.2.21 multiplatform update"></a>
</p>

| **🖨️ Complete firmware · V4.4.2** | **🔄 CC2 Control · 1.2.21** |
| --- | --- |
| [**Download signed firmware**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig) · [Release notes and checksums](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.2) | [**Download Windows / macOS / Linux package**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-Multiplatform-Update.zip) · [Release notes and checksums](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.21-V4.4) |
| Complete installation with CC2 Control **1.2.21** and the webcam fix already included. No extra reboot after firmware installation. | For community firmware **V4.4/V4.4.1/V4.4.2**. Includes the camera stability and recovery fixes. |

**Updating from the CC2 Control panel?** [Download the signed `.cc2update` file](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-V4.4.cc2update) and select it in **Settings → Updates**. For app-only updates, reboot while idle only on first activation of the camera fix; it is already active for users coming from 1.2.19. Complete V4.4.2 installation needs no additional reboot.

[Italiano](README.it.md) · [Documentation](docs/README.md) · [Downloads](https://github.com/Neme77/centauri-carbon-2-community/releases) · [Screenshots](docs/SCREENSHOTS.md)

> Independent community project, not affiliated with or supported by ELEGOO.
> This project targets the **Centauri Carbon 2**, not the first Centauri Carbon.

<p align="center">
  <img src="docs/images/cc2-control/dashboard-light.png" alt="CC2 Control dashboard with live camera, printer controls and temperature monitoring" width="1200">
</p>

## Get started

| What you need | English | Italiano |
| --- | --- | --- |
| Install the complete community firmware | [First installation](docs/FIRST_RUN.md) | [Prima installazione](docs/FIRST_RUN.it.md) |
| Update an existing CC2 Control installation | [Application updates](docs/UPDATE_CC2.md) | [Aggiornamenti dell’app](docs/UPDATE_CC2.it.md) |
| Connect OrcaSlicer and synchronize Canvas slots | [OrcaSlicer setup](docs/ORCASLICER.md) | [Configurazione OrcaSlicer](docs/ORCASLICER.it.md) |

Download files from [GitHub Releases](https://github.com/Neme77/centauri-carbon-2-community/releases)
and follow the matching guide and release notes. **Complete firmware**, the
**CC2 Control application** and the **source builder** are different downloads:
the builder ZIP is not an installable printer update. Check the supplied SHA-256
checksums before installation.

The current qualified firmware family is **V4.4/V4.4.1/V4.4.2**, based on ELEGOO
**02.01.01.07**. Its application updates are not interchangeable with stock
firmware, older community firmware or experimental builds.

Open **`http://PRINTER-IP:8081`** on your local network. OrcaSlicer also needs
**`:8081`** after the printer IP. App installation requires the printer’s **LAN
access code**, which is different from the SSH password; checking for updates
does not. Installation always requires confirmation. Camera-component updates
require a **full printer reboot** as described in their release notes.

## What CC2 Control offers

- Local dashboard, live video, temperatures, fans, print speed and flow controls.
- Internal/USB files, model previews and interactive object exclusion.
- Canvas slot states, material settings, auto refill and OrcaSlicer filament sync.
- Bed leveling, saved plate/nozzle meshes and optional spool inventory/tracking.
- Backup and selective restore, diagnostics, protected Console and signed app updates.
- Multiple interface languages, themes and desktop/mobile layouts.

See the [application guide](docs/CC2_CONTROL.md) and [screenshot gallery](docs/SCREENSHOTS.md).
Experimental features remain opt-in and require their own validation.

## New in V4.4.2 / CC2 Control 1.2.21

The full firmware includes the current application and the webcam fix from first boot.
Existing V4.4/V4.4.1 users can install the separate application update.

### Redesigned Canvas

Compact slot cards, shared actions and balanced settings/history panels make Canvas easier to read on desktop and mobile. Recovery loading can restore an unassigned filament-head state after a reboot; you choose a present slot and confirm the native loading operation. Observed history is bounded and resets on page reload. Layout inspiration: **Surfoo**; maintainer/tester refinements preserve the project's existing controls and information.

[![New Canvas layout](docs/images/cc2-control/canvas-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/develop/docs/images/cc2-control/canvas-current.png)

### Independent experimental features

**Canvas UDS** and **Quiet motor controls** are independent opt-ins in Settings → Updates. They start disabled on fresh installations and retain existing choices across updates. Canvas UDS has MQTT fallback. Quiet/Normal derives from **BorjaLL**'s contribution; slicer commands can override its initial limits. Complete Normal before disabling Quiet controls or changing Canvas source. Extended pull-back above 350 mm still needs its separate qualified firmware runtime.

[![Independent experimental settings](docs/images/cc2-control/experiments-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/develop/docs/images/cc2-control/experiments-current.png)

*Images show the actual interface with simulated demonstration data.*

### All release improvements

- **Reliable updates:** accept GitHub redirect security-header lines above 8 KiB within the existing 16 KiB budget, retaining TLS and redirect checks; fixes the misleading HTTPS/clock error (#214, report by **efiten**, #215).
- **Webcam:** qualified R2 client-thread reclamation and local/Matrix readiness recovery are included from the first V4.4.2 boot and retained in cumulative app updates, including skipped versions. Verified canonical/dormant installations migrate correctly; unknown startup changes remain refused. Diagnosis by **tharje**.
- **Unified updating:** one stable update stream; previous experimental clients migrate to the same 1.2.21 package. Feature opt-ins remain separate from updates.
- **Canvas layout and recovery:** compact shared actions and session history (#207), explicit user-confirmed recovery loading (#190), optional direct UDS telemetry (#200).
- **Quiet/Normal controls:** independent opt-in, LAN confirmation and idle/cold guards; guidance to reconfirm Normal after telemetry reconnects (original feature by **BorjaLL**, #179).
- **Files:** older entries beyond the first 128 remain accessible; streamed uploads up to **512 MiB** (#197).
- **Object exclusion:** one refined 2D map, linked selection and unchanged original G-code IDs (#199). Map idea/prototype by **Nick Plateringen-Paul**, refinement suggested by **DamiBFryta**.
- **New-spool popup:** create an independent identical spool without moving the original; calculate remaining filament from total minus empty-spool weight while preserving nominal capacity (#210), suggested by **DamiBFryta**.
- **Interface:** consistent spacing/titles, keyboard focus and accessibility improvements (#196); clearer homepage downloads (#201).
- **Preserved features:** backup/restore, plate/nozzle mesh libraries, Orca integration, languages, persistent video, diagnostics and update notifications.
- **Reboot:** app-only users reboot only on first webcam-fix activation (already supplied since 1.2.19); complete V4.4.2 installation already reboots, so no extra reboot is needed.

[Full release notes, checksums and contributor credits](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.21-V4.4).
Spoolman, the proposed dashboard makeover, filament drying and the completed-print popup remain outside this release.

## Help and development

[Documentation index](docs/README.md) · [Report a problem](https://github.com/Neme77/centauri-carbon-2-community/issues) · [Contributing](CONTRIBUTING.md) · [Build guide](docs/BUILD.md) · [Source and provenance](docs/SOURCE.md)

Version history belongs in the [CHANGELOG](CHANGELOG.md) and
[release notes](https://github.com/Neme77/centauri-carbon-2-community/releases).
Installation details and compatibility requirements are maintained in the guides
linked above.

## Community

Thanks to **Surfoo, beanbo, efiten, Skcycos, pezso92, asiersan, Noahwhite2014,
tharje, Nick Plateringen-Paul**, and every contributor, translator and tester.
Special thanks to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our
ultimate beta tester**. [Full credits and original contributions](docs/CONTRIBUTORS.md).

AI-assisted work is reviewed and validated. Architecture decisions, printer-side
testing and release approval remain with the maintainer and community testers.

## License

Project code is GPL-3.0-only where contributors have the right to license it.
Vendor firmware and third-party components retain their own terms.
See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
