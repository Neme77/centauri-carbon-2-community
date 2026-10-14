# Centauri Carbon 2 Community Firmware

Community firmware, local printer control and reproducible build tools for the **ELEGOO Centauri Carbon 2**, maintained by **Neme77**.

> Independent community project. Not affiliated with, endorsed by or supported
> by ELEGOO.

<p align="center"><strong>Local control · Canvas · OrcaSlicer sync · Seven languages · Six themes</strong></p>

<p align="center">
  <a href="docs/FIRST_RUN.md">First installation</a> · <a href="docs/ORCASLICER.md">OrcaSlicer &amp; Canvas</a> · <a href="docs/SCREENSHOTS.md">Screenshots</a> · <a href="docs/CONTRIBUTORS.md">Community credits</a>
</p>

<p align="center">
  <img src="docs/images/cc2-control/dashboard-light.png" alt="CC2 Control 1.2.3: live camera, printer controls and thermal monitoring" width="1200">
</p>

## Start here / Guide rapide

| Guide | English | Italiano |
| --- | --- | --- |
| First installation, printer IP and LAN access code | [First setup](docs/FIRST_RUN.md) | [Prima installazione](docs/FIRST_RUN.it.md) |
| OrcaSlicer connection, upload and Canvas filament sync | [OrcaSlicer guide](docs/ORCASLICER.md) | [Guida OrcaSlicer](docs/ORCASLICER.it.md) |
| All V4.4 improvements and retained features | [Changelog](CHANGELOG.md#community-firmware-v44--cc2-control-123--2026-10-13) | [Novità della release](CHANGELOG.md#unreleased) |

## V4.4 — now available

**[Community Firmware V4.4](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4) is officially available**, with CC2 Control 1.2.3 and official CC2 base 02.01.01.07. Released on **13 October 2026** with the tested complete firmware, public source builder and checksums.

| V4.4 highlight | What changes for the user |
| --- | --- |
| New official base and connection fixes | Ported firmware patches, LAN/WAN reconnection and native light commands with display-state acknowledgement. |
| Control panel | Aligned cards, fan percentage fields applied with Enter, guarded PID/Input Shaper tools and calibration Quick Actions. |
| Material profiles | Names such as **PLA MATTE** are accepted; a themed dropdown shows preheat presets without squeezing names into a row of buttons. Selecting a preset loads the fields; **Apply temperatures** starts heating. |
| Canvas | Confirmed reset of the four filament records; empty slots are dimmed without a colour. MQTT Canvas telemetry and Orca filament sync remain supported. |
| Camera | Live viewing continues while scrolling; hidden-tab, navigation and viewer-ownership controls remain active. |
| Languages | English, Italian, French, Simplified Chinese, Russian, **Polish and Czech**. |

The O0 callback runtime, protected live Z offset, spool inventory, build-plate library, mesh tools, history/timelapse, file uploads/downloads and Panda Breath compatibility are retained. PR103/104 and the experimental full Canvas UDS variant are not included in this candidate.

## Long-print results and callback patch

The community beta campaign delivered encouraging results: **40-hour printing sessions without reported freezes** and **more than 20 confirmed long tests**. Tester feedback also helped resolve smaller issues in the interface and printer integration.

| Evidence | Result |
| --- | --- |
| Long printer sessions | 40-hour prints and more than 20 long tests reported by the maintainer and beta testers, with no freezes reported in those sessions. |
| Automatic validation | Host tests, ARM build/smoke checks, address/undefined and thread sanitizers, web UI checks, builder tests and reactor fixtures passed on the release-preparation PR. |
| Callback runtime | Qualified O0 cleanup of completed callback references, timers and completion waiters, ported to the fingerprinted new printer executable. |

The **callback patch is a central part of this work**: it addresses retained references in the reactor so completed requests do not keep the corresponding objects alive through those structures. Its cleanup and exception paths were qualified, and the 4.4 port preserves the same principle with the new executable addresses. During the beta cycle, the printer recovered available memory after activity and completed prolonged printing without the reported freezes we were investigating.

These are positive integration results, rather than a controlled measurement attributing every memory change to the callback patch. The runtime targets reactor retention; camera buffers, caches and other vendor allocations have separate lifecycles. [Read the callback mechanism and validation evidence](docs/CALLBACK_RUNTIME.md).

**Release packaging:** the V4.4 release supplies complete firmware and its builder. The new CC2 Control application is included in that compatible firmware; **installing it separately on V4.3 is not supported**. A standalone application updater is deferred. Future application updates are planned in two clearly identified variants: the current MQTT Canvas path and an experimental UDS Canvas path, each with explicit firmware compatibility and separate validation results.

## Downloads — V4.4

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/CC2_V4_4_STOCK_20261013_001035_414d6629.zip.sig"><img alt="Download complete Firmware V4.4" src="https://img.shields.io/badge/Download-Firmware%20V4.4-00b8d9?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/CC2-Builder-V4.4-Source.zip"><img alt="Download V4.4 source builder" src="https://img.shields.io/badge/Builder-V4.4%20Source-f59e0b?style=for-the-badge"></a>
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/SHA256SUMS.txt"><img alt="Download SHA-256 checksums" src="https://img.shields.io/badge/Verify-SHA--256-22c55e?style=for-the-badge"></a>
</p>

| Download | Purpose |
| --- | --- |
| [Complete signed V4.4 firmware](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/CC2_V4_4_STOCK_20261013_001035_414d6629.zip.sig) | Install or update the complete Community Firmware, including its compatible base and CC2 Control 1.2.3. |
| [Public V4.4 source builder](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/CC2-Builder-V4.4-Source.zip) | Build locally; qualified official firmware, vendor library, local tools and private signing/AES material remain external. Read PUBLIC-BUILDER.txt and [Build guide](docs/BUILD.md). |
| [SHA256SUMS.txt](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4/SHA256SUMS.txt) | Verify both release downloads. |
| [Release notes and all assets](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4) | Features, callback evidence, validation, installation and acknowledgements. |

**There is no standalone CC2 Control updater for V4.4 in this release.** Do not install the new application separately on V4.3. For V4.4, use the complete signed firmware above. A factory reset is not required.

<details>
<summary>Previous releases and historical downloads</summary>

- [V4.3 experimental pre-release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.3), with CC2 Control 1.2.0 and the first callback integration.
- [Community Firmware V4.2-R5](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2-R5).
- [CC2 Control 1.1.31fix](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.1.31fix), a historical standalone updater for its documented firmware compatibility.

These releases keep their original assets, checksums and validation records. They are not the current V4.4 downloads.

</details>

## Changed the printer LAN access code?

Open `http://PRINTER-IP:8081`, go to **Settings → Connection**, enter the replacement code and choose **Change / Revalidate**, then confirm. This saves the code and restarts the CC2 Control service; a separate “Save changes” step is not required for the LAN code.

Read [first setup and LAN-code recovery](docs/FIRST_RUN.md#if-the-lan-code-changes) or [prima configurazione e codice LAN](docs/FIRST_RUN.it.md#se-cambia-il-codice-lan). Advanced SSH recovery remains in [Installation](docs/INSTALL.md#recovery-if-the-old-code-blocks-configuration-access).

## Features

| Area | What you can do |
| --- | --- |
| Dashboard and print tuning | Monitor temperatures, fans, print progress and live movement; adjust **print speed (25–200%)** and **extrusion flow (50–150%)** during an active or paused print with fresh telemetry. |
| File manager | Browse internal and USB storage, search and sort files, inspect model thumbnails and print metadata, upload G-code, **download G-code to your computer**, delete files and start a protected print. |
| Canvas and print continuity | View four material slots, colours and presets; select, load or unload a spool; enable **Auto refill** so the printer can continue from another slot holding the same filament when one spool runs out. |
| Spools | Keep a filament inventory that knows which spool is in which Canvas slot or on the external holder, charged with what the printer really extrudes. New filament in a slot asks which spool it is, a spool that runs out is set to zero, weigh-ins correct the count, and the print popup warns when a spool holds less than the file needs. Off until switched on. |
| Print popup and timelapse | Choose filament-slot mapping and bed side before printing, use a saved mesh or a calibrated start, and **enable timelapse recording for that print**. |
| History | Review jobs recorded by the printer, including start time, duration and result; download ready timelapse videos and render recorded frames into a video while Idle. Delete individual records or clear loaded completed records with confirmation; G-code files are preserved. |
| Camera | Start live viewing on request, take snapshots and open the viewer in a separate window. CC2 coordinates camera ownership between its pages and windows and releases the viewer when it closes; the vendor camera service remains active. In V4.4, scrolling away from the camera card does not stop the stream. |
| Printer control | Home and move axes, set nozzle and bed targets, use material temperature presets, control fans and lights, extrude filament and adjust the live session Z offset. V4.4 adds numeric fan inputs and the themed preheat selector. |
| Bed levelling | View the mesh in 2D or interactive 3D, inspect values and saved profiles, choose Side A / Side B, use adaptive probing and measure four-screw adjustment with the nozzle load cell. |
| Quick Actions and safety | Configure dashboard shortcuts with real light-state feedback; use object exclusion, a protected console and global emergency stop. Heater-off actions require fresh Idle telemetry. |
| Printer status and connection | See native machine sub-states and refusal messages; obtain telemetry through persistent UDS sessions, discover the printer serial automatically using saved LAN credentials, and revalidate a changed LAN code. |
| Slicer and integrations | OrcaSlicer upload, Device UI and Moonraker-agent Canvas filament sync use port **8081**. The separate read-only Panda Breath compatibility bridge uses port **7125**. |
| Interface | Use English, Italian, French, Simplified Chinese, Russian, Polish or Czech; choose Light, Dark, Dracula, Nord, Monokai or Solarized Light themes; save appearance preferences and collapse the navigation menu. |

## Community contributions

Our sincere thanks to the contributors whose code, ideas, translations and validation helped build CC2 Control. **V4.4 retains their work from throughout the project's history**; credit follows original authorship, including work integrated through a later maintainer PR.

| Contributor | Contributions included in the project |
| --- | --- |
| **[@Surfoo](https://github.com/Surfoo)** | The complete interface redesign, Control-page and mobile/tablet improvements; French translation and JSON localization; CI, sanitizers and ARM smoke tests; upload-slot fixes, byte-exact inputs, LAN-code visibility and release/version tooling. Selected PRs: [#24](https://github.com/Neme77/centauri-carbon-2-community/pull/24), [#15](https://github.com/Neme77/centauri-carbon-2-community/pull/15), [#19](https://github.com/Neme77/centauri-carbon-2-community/pull/19), [#50](https://github.com/Neme77/centauri-carbon-2-community/pull/50). |
| **[@beanbo](https://github.com/beanbo)** | Spool inventory, the original build-plate library, persistent UDS subscriptions, printer states/refusals, Canvas auto-refill, camera handover, history, upload improvements, idle motor release and Russian translation. Selected PRs: [#97](https://github.com/Neme77/centauri-carbon-2-community/pull/97), [#83](https://github.com/Neme77/centauri-carbon-2-community/pull/83) → [#89](https://github.com/Neme77/centauri-carbon-2-community/pull/89), [#88](https://github.com/Neme77/centauri-carbon-2-community/pull/88), [#67](https://github.com/Neme77/centauri-carbon-2-community/pull/67). |
| **[@efiten](https://github.com/efiten)** | Original OrcaSlicer/Canvas filament-sync implementation proposal, analysis and printer-side validation: [#8](https://github.com/Neme77/centauri-carbon-2-community/pull/8), integrated through [#12](https://github.com/Neme77/centauri-carbon-2-community/pull/12). |
| **[@Skcycos](https://github.com/Skcycos)** | Simplified Chinese interface translation: [#26](https://github.com/Neme77/centauri-carbon-2-community/pull/26). |

Special recognition to **Surfoo** for reshaping the interface and its development foundations, and to **beanbo** for the extensive additions to everyday printing and printer integration. Thank you to every contributor for the care, time and follow-through behind these improvements.

We also thank **[@sauadhi](https://github.com/sauadhi)** for the full Canvas filament-ejection work in [#103](https://github.com/Neme77/centauri-carbon-2-community/pull/103), and **beanbo** for the further mesh/calibration work in [#104](https://github.com/Neme77/centauri-carbon-2-community/pull/104). These two extensions remain under review and **are not included in V4.4**; acknowledging the contribution does not announce its release.

**[@Neme77](https://github.com/Neme77)** maintains the project, develops and integrates the firmware/control fixes, coordinates hardware validation and prepares release artifacts. The recent integration includes the Control/Canvas changes, Polish/Czech translations and new firmware-base port ([#107](https://github.com/Neme77/centauri-carbon-2-community/pull/107), [#108](https://github.com/Neme77/centauri-carbon-2-community/pull/108), [#109](https://github.com/Neme77/centauri-carbon-2-community/pull/109), [#110](https://github.com/Neme77/centauri-carbon-2-community/pull/110)).

See [the detailed contributor credits and original PRs](docs/CONTRIBUTORS.md).

### Beta testers

Special thanks to **DamiBFryta**, our very first beta tester, for being reliable, impartial and always available throughout development.

Our sincere thanks also go to **Barry Green**, **Michael Husbyn**, **[Jiří Heider](https://www.facebook.com/heiderakcze)**, **James Morriss Hill** and **Brian Mcmillion**, and to **all the other beta testers not named here**. By offering their time and their printers, running long printing sessions and providing patient, precise feedback, they helped identify issues and improve the stability of this release.

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

- [First installation and LAN access code](docs/FIRST_RUN.md) / [Italiano](docs/FIRST_RUN.it.md)
- [OrcaSlicer and Canvas sync](docs/ORCASLICER.md) / [Italiano](docs/ORCASLICER.it.md)
- [Installation and advanced recovery](docs/INSTALL.md)
- [Build and reproduction](docs/BUILD.md)
- [Architecture](docs/ARCHITECTURE.md)
- [CC2 Control](docs/CC2_CONTROL.md)
- [Source and third-party inputs](docs/SOURCE.md)
- [Testing](docs/TESTING.md)
- [Contributor credits](docs/CONTRIBUTORS.md)
- [Contributing](CONTRIBUTING.md)

## Interface preview

Real-printer screenshots of **CC2 Control 1.2.3**, supplied on 13 October 2026 for the V4.4 release candidate. [Open the complete screenshot gallery](docs/SCREENSHOTS.md) to see all eleven views at full size.

### Dashboard

The opening image shows the Light-theme dashboard with live camera, Quick Actions, print status and temperature history. [View it at full size](docs/images/cc2-control/dashboard-light.png).

### Control panel and Canvas

The Control panel includes numeric fan fields applied with Enter and a preheat dropdown supporting names such as PLA MATTE. Canvas displays empty slots without a filament colour and provides Reset all filaments.

<table>
  <tr><td align="center"><strong>Control and preheat presets</strong></td><td align="center"><strong>Canvas and filament reset</strong></td></tr>
  <tr><td><a href="docs/images/cc2-control/control-presets.png"><img src="docs/images/cc2-control/control-presets.png" alt="Control panel with PLA MATTE preset, fan inputs and live Z offset"></a></td><td><a href="docs/images/cc2-control/canvas.png"><img src="docs/images/cc2-control/canvas.png" alt="Four Canvas slots, dimmed empty slots and Reset all filaments"></a></td></tr>
</table>

### Bed mesh and spool inventory

Interactive mesh inspection and optional tracking of the filament inventory.

<table>
  <tr><td align="center"><strong>3D bed mesh</strong></td><td align="center"><strong>Spool inventory</strong></td></tr>
  <tr><td><a href="docs/images/cc2-control/bed-mesh.png"><img src="docs/images/cc2-control/bed-mesh.png" alt="Interactive 3D bed mesh, saved profiles and height statistics"></a></td><td><a href="docs/images/cc2-control/spools.png"><img src="docs/images/cc2-control/spools.png" alt="Spool inventory and optional tracking controls"></a></td></tr>
</table>

The [complete gallery](docs/SCREENSHOTS.md) also shows PID/Input Shaper and material profiles, the Job page, G-code files and model previews, the protected console, integration checks and the six themes.

## AI-assisted development

This project uses AI-assisted development tools as part of the workflow, including support for code review, refactoring, debugging, documentation and selected code generation tasks.

A substantial part of the codebase is written and maintained directly by the project maintainer. Architecture decisions, reverse engineering, hardware testing, printer-side validation and release approval are performed by the maintainer and community testers.

AI-assisted changes are reviewed and validated before being included in a release.

## License and notices

Project code is provided under GPL-3.0-only where the contributors have the right to license it. Vendor firmware and third-party components keep their own terms. See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
