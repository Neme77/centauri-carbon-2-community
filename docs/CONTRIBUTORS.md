# Contributors and acknowledgements

[Homepage](../README.md) · [Project history](../CHANGELOG.md)

Thank you to everyone who has contributed code, ideas, reviews, translations and printer time. Community Firmware V4.4 builds on work from across the project's history, not only the latest pull requests.

The credits below follow original authorship. A feature may have been integrated through a maintainer PR after review or corrections; that does not replace the original contributor's credit. References were checked against the repository's pull requests on **13 October 2026**.

## Contributions included in the project and retained in V4.4

### Surfoo

Special recognition to **[@Surfoo](https://github.com/Surfoo)** for the CC2 Control interface redesign and the foundations that make it maintainable, localized and testable.

| Contribution | Pull requests |
| --- | --- |
| Vite/Preact/Tailwind interface rewrite, Control-page consistency and mobile/tablet usability | [#24](https://github.com/Neme77/centauri-carbon-2-community/pull/24), [#44](https://github.com/Neme77/centauri-carbon-2-community/pull/44), [#50](https://github.com/Neme77/centauri-carbon-2-community/pull/50) |
| French translation and JSON localization | [#15](https://github.com/Neme77/centauri-carbon-2-community/pull/15) |
| GitHub Actions, the test target, warnings-as-errors, sanitizers and ARM smoke checks | [#16](https://github.com/Neme77/centauri-carbon-2-community/pull/16), [#19](https://github.com/Neme77/centauri-carbon-2-community/pull/19) |
| Byte-exact builder inputs and upload-slot concurrency fixes | [#11](https://github.com/Neme77/centauri-carbon-2-community/pull/11), [#18](https://github.com/Neme77/centauri-carbon-2-community/pull/18) |
| LAN-code visibility control, version definitions, release workflow and documentation cleanup | [#73](https://github.com/Neme77/centauri-carbon-2-community/pull/73), [#77](https://github.com/Neme77/centauri-carbon-2-community/pull/77), [#17](https://github.com/Neme77/centauri-carbon-2-community/pull/17), [#49](https://github.com/Neme77/centauri-carbon-2-community/pull/49) |

### beanbo

Special recognition to **[@beanbo](https://github.com/beanbo)** for the substantial additions to everyday printing, printer integration and telemetry, together with continued testing and refinement.

| Contribution | Pull requests |
| --- | --- |
| Spool inventory and filament tracking | [#97](https://github.com/Neme77/centauri-carbon-2-community/pull/97) |
| Original build-plate library | [#83](https://github.com/Neme77/centauri-carbon-2-community/pull/83), integrated with corrections through [#89](https://github.com/Neme77/centauri-carbon-2-community/pull/89) |
| Printer sub-states/refusals, Canvas auto-refill and print history | [#67](https://github.com/Neme77/centauri-carbon-2-community/pull/67) |
| Camera viewer ownership and handover | [#71](https://github.com/Neme77/centauri-carbon-2-community/pull/71) |
| File upload card, newest-file ordering, refusal messages and numbered Orca upload copies | [#78](https://github.com/Neme77/centauri-carbon-2-community/pull/78), [#80](https://github.com/Neme77/centauri-carbon-2-community/pull/80), [#81](https://github.com/Neme77/centauri-carbon-2-community/pull/81) |
| Canvas registration, homing before mesh calibration and idle motor release | [#84](https://github.com/Neme77/centauri-carbon-2-community/pull/84), [#85](https://github.com/Neme77/centauri-carbon-2-community/pull/85), [#86](https://github.com/Neme77/centauri-carbon-2-community/pull/86) |
| Persistent UDS subscriptions instead of repeated socket polling | [#88](https://github.com/Neme77/centauri-carbon-2-community/pull/88) |
| Russian translation, Russian/Chinese PID and printer-report strings, and restored API documentation | [#63](https://github.com/Neme77/centauri-carbon-2-community/pull/63), [#95](https://github.com/Neme77/centauri-carbon-2-community/pull/95) |

### efiten

Special thanks to **[@efiten](https://github.com/efiten)** for the original OrcaSlicer/Canvas filament-sync implementation proposal, investigation and real-printer validation: [#8](https://github.com/Neme77/centauri-carbon-2-community/pull/8). The integration was included through [#12](https://github.com/Neme77/centauri-carbon-2-community/pull/12), which explicitly preserves this attribution. The original PR being closed without a direct merge does not mean this contribution was unused.

### Skcycos

Thank you to **[@Skcycos](https://github.com/Skcycos)** for the **Simplified Chinese interface translation**, [#26](https://github.com/Neme77/centauri-carbon-2-community/pull/26), helping make CC2 Control accessible to more users.

### Project maintenance and integration

**[@Neme77](https://github.com/Neme77)** maintains the project, develops and integrates firmware/control changes, coordinates printer-side validation and prepares the release artifacts. The latest integration includes the Control panel/camera updates ([#107](https://github.com/Neme77/centauri-carbon-2-community/pull/107)), Canvas reset ([#108](https://github.com/Neme77/centauri-carbon-2-community/pull/108)), Polish/Czech interface translations ([#109](https://github.com/Neme77/centauri-carbon-2-community/pull/109)) and 4.4 firmware-base port ([#110](https://github.com/Neme77/centauri-carbon-2-community/pull/110)). Original contributor credit remains with the work listed above.

## Work under review — not included in V4.4

We also thank **[@sauadhi](https://github.com/sauadhi)** for the full Canvas filament-ejection proposal, touchscreen/web integration and validation work in [#103](https://github.com/Neme77/centauri-carbon-2-community/pull/103), following [#102](https://github.com/Neme77/centauri-carbon-2-community/pull/102).

Thank you to **[@beanbo](https://github.com/beanbo)** for the continued temperature/nozzle mesh and soaked-calibration work in [#104](https://github.com/Neme77/centauri-carbon-2-community/pull/104).

Both extensions remain under review and are **excluded from this V4.4 release candidate**. The four-slot material-data reset included in V4.4 is a different operation from full physical filament ejection.

## Translation for the next CC2 Control update

Thank you to **[@pezso92](https://github.com/pezso92)** for the complete **Hungarian (Magyar)** translation in [#115](https://github.com/Neme77/centauri-carbon-2-community/pull/115): 879 entries, matching source keys and dynamic placeholders. The maintainer’s packaging/test follow-up preserves the original translation commits and author credit. This contribution is for the next update and is not part of the already published V4.4 assets.

## Beta testers

Special thanks to **DamiBFryta**, our very first beta tester, for being reliable, impartial and always available throughout development.

Our sincere thanks also go to **Barry Green**, **Michael Husbyn**, **[Jiří Heider](https://www.facebook.com/heiderakcze)**, **James Morriss Hill** and **Brian Mcmillion**, and to **all the other beta testers not named here**. By offering their time and their printers, running long printing sessions and providing patient, precise feedback, they helped identify issues and improve the stability of this release.

## Open-source and vendor acknowledgements

This project also relies on upstream tools and third-party components. Their provenance, licenses and notices remain in [NOTICE.md](../NOTICE.md) and [Source documentation](SOURCE.md); these community credits do not imply ownership of vendor firmware.
