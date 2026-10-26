# Contributors and acknowledgements

[Homepage](../README.md) · [Project history](../CHANGELOG.md)

Thank you to everyone who has contributed code, ideas, reviews, translations and printer time. Community Firmware V4.4 builds on work from across the project's history, not only the latest pull requests.

The credits below follow original authorship. A feature may have been integrated through a maintainer PR after review or corrections; that does not replace the original contributor's credit. References were checked against the repository's pull requests on **14 October 2026**.

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

## Retired experimental filament ejection — excluded from the application update

We also thank **[@sauadhi](https://github.com/sauadhi)** for the full Canvas filament-ejection proposal, touchscreen/web integration and validation work in [#103](https://github.com/Neme77/centauri-carbon-2-community/pull/103), following [#102](https://github.com/Neme77/centauri-carbon-2-community/pull/102).

The advanced temperature/nozzle mesh and soaked calibration by **[@beanbo](https://github.com/beanbo)** in [#104](https://github.com/Neme77/centauri-carbon-2-community/pull/104) is now merged and included in the separate application update below. It remains absent from the original complete V4.4 firmware assets.

Full filament ejection, including PR103/114, was investigated on the printer and retired without merging: filament can leave the rollers while remaining inside the inlet sensor. The sensor-present state must not be treated as an empty slot or a successful complete ejection. The original proposal and its author remain credited; it is excluded from the release. PR104 is included in the application update; original V4.4 firmware artifacts are unchanged. The four-slot material-data reset included in V4.4 is a different operation from full physical filament ejection.

## CC2 Control 1.2.10 application update

Thank you to **[@pezso92](https://github.com/pezso92)** for the complete **Hungarian (Magyar)** translation in [#115](https://github.com/Neme77/centauri-carbon-2-community/pull/115): 879 entries, matching source keys and dynamic placeholders. The maintainer’s packaging/test follow-up preserves the original translation commits and author credit. This translation is included in the V4.4-only application updater, while the original complete V4.4 firmware assets retain their original seven languages. The same updater includes beanbo’s advanced temperature/nozzle mesh and soaked calibration from #104, with his original authorship retained.

| Merged PR | Included improvement | Original contribution / integration |
| --- | --- | --- |
| [#104](https://github.com/Neme77/centauri-carbon-2-community/pull/104) | Temperature/nozzle mesh measurements and soaked calibration | beanbo |
| [#115](https://github.com/Neme77/centauri-carbon-2-community/pull/115), [#116](https://github.com/Neme77/centauri-carbon-2-community/pull/116) | Hungarian translation and packaging/browser integration | pezso92; Neme77 integration |
| [#117](https://github.com/Neme77/centauri-carbon-2-community/pull/117) | V4.4-only application installer, compatibility checks, backup and restore | Neme77 |
| [#118](https://github.com/Neme77/centauri-carbon-2-community/pull/118) | Available storage space and 15-item File Manager/History pages | Neme77 |
| [#119](https://github.com/Neme77/centauri-carbon-2-community/pull/119) | External-feed Canvas table clear and complete calibrated multicolour mapping | beanbo |
| [#120](https://github.com/Neme77/centauri-carbon-2-community/pull/120) | Correct Orca G-code bed temperatures and multi-tool filament estimates | Neme77, with beta-tester reports |
| [#121](https://github.com/Neme77/centauri-carbon-2-community/pull/121) | Optional persistent camera across navigation and hidden/minimized pages | Neme77, retaining beanbo's viewer coordination |
| [#124](https://github.com/Neme77/centauri-carbon-2-community/pull/124) | Signed GitHub updates in Settings and cleared Canvas-analysis notification | Neme77 |

The 1.2.10 updater and Canvas-dialog follow-up were tested on the maintainer's V4.4 printer. Integration preserves original contributor commits and attribution.

## CC2 Control 1.2.14 language and interface contributions

Spanish: **[asiersan](https://github.com/asiersan)**, original #130, integration #133. Hungarian refinements: **[pezso92](https://github.com/pezso92)**, original #129, integration #132. Localized update punctuation: **[Surfoo](https://github.com/Surfoo)**, original #128, integration #135. These original contributions retain exclusive author credit. The plate workflow extends the advanced mesh system by **beanbo** in #104. Thank you to the beta testers who reported the camera, fan, object-layout and plate-workflow issues and validated the combined application on a real printer.

## CC2 Control 1.2.15

Hungarian completion (20 remaining strings): **[pezso92](https://github.com/pezso92)**, original #138 and integration #141, with original author commits retained. Verified library mesh activation extends **[beanbo](https://github.com/beanbo)**'s plate/mesh library. Update notification and mesh changes are qualified together in #142, including #139/#140. Existing Spanish, Hungarian and punctuation credits above remain unchanged.

## Beta testers

Special thanks to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/)**, our very first beta tester, **our ultimate beta tester**, for being reliable, impartial and always available throughout development. He was the first to report error 803 and continues to investigate, reproduce and validate fixes with exceptional persistence, offering his time and his printer.

Our sincere thanks also go to **Barry Green**, **Michael Husbyn**, **[Jiří Heider](https://www.facebook.com/heiderakcze)**, **James Morriss Hill** and **Brian Mcmillion**, and to **all the other beta testers not named here**. By offering their time and their printers, running long printing sessions and providing patient, precise feedback, they helped identify issues and improve the stability of this release.

## Open-source and vendor acknowledgements

This project also relies on upstream tools and third-party components. Their provenance, licenses and notices remain in [NOTICE.md](../NOTICE.md) and [Source documentation](SOURCE.md); these community credits do not imply ownership of vendor firmware.

## CC2 Control 1.2.16

**pezso92**: Hungarian refinements and preference-save feedback (#144/#145, integrated #148), and three-state print mesh readiness/manual recheck (#150, integrated #151). Original commits and credit are retained.

**[DamiBFryta](https://www.reddit.com/user/DamiBFryta/)** suggested external backup and saved-language startup, and continued real-printer testing as our ultimate beta tester. Earlier Spanish, Hungarian, mesh and punctuation contributors remain credited above.

## CC2 Control 1.2.17

**[pezso92](https://github.com/pezso92)**: original Orca endpoint diagnostics (#155), manual allowlisted diagnostics export (#156) and bounded observed Canvas slot timeline (#157), integrated in #158 with original feature commits and attribution retained.

Thanks to **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, for continued field feedback; earlier contributors and language credits remain above.

## Additional localization contributions

**[pezso92](https://github.com/pezso92)** completed 88 missing Spanish, Russian and Simplified Chinese interface strings ([#164](https://github.com/Neme77/centauri-carbon-2-community/pull/164)) and clarified the updater's Community Firmware V4.4/V4.4.1 requirements in all nine languages ([#163](https://github.com/Neme77/centauri-carbon-2-community/pull/163)). The original contribution commits are preserved. Existing Spanish, Russian, Chinese and other translator credits above remain unchanged.

## Canvas pull-back configuration

**[Noahwhite2014](https://github.com/Noahwhite2014)** contributed the Canvas post-cut pull-back setting ([#168](https://github.com/Neme77/centauri-carbon-2-community/pull/168)), including the configuration integration, translated interface, tests and real-printer validation. Original contribution commit and feature attribution are preserved.

## Panda Breath integration

**[Noahwhite2014](https://github.com/Noahwhite2014)** contributed the optional Panda Breath / Dragon Breath interface ([#166](https://github.com/Neme77/centauri-carbon-2-community/pull/166), integrated in [#167](https://github.com/Neme77/centauri-carbon-2-community/pull/167)). Original contribution history and feature attribution are preserved. Thanks to Barry for real-device feedback.

## CC2 Control 1.2.18

**[Noahwhite2014](https://github.com/Noahwhite2014)**: original Panda Breath / Dragon Breath interface (#166/#167) and native Canvas post-cut pull-back setting (#168), integrated and validated through #169. **[pezso92](https://github.com/pezso92)**: localization and update-requirement refinements (#163/#164/#165). Original commits and existing language credits are retained.

Thanks to **Barry** for Panda Breath field feedback and **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, for persistent field testing and precise reports. Thanks to all contributors, translators and testers named above.

## CC2 Control 1.2.19

- **[tharje](https://github.com/tharje)**: camera failure investigation, traces, crash reproduction and analysis in #160, informing the thread-reclamation and readiness recovery fixes (#183/#186).
- **Nick Plateringen-Paul**: original interactive bed-map idea and JavaScript prototype, integrated in #188 with original-ID exclusion and existing data/polling protections.
- **[pezso92](https://github.com/pezso92)**: Canvas save/restart guards, observed-timeline context and mobile header actions (#175/#176/#177, integrated in #180). Original commits and author credit retained.
- **[Noahwhite2014](https://github.com/Noahwhite2014)**: Panda/DragonBreath filament metadata bridge and empty WebSocket ping fix (#181, integrated in #182). Original commits and feature credit retained.

Thanks to **Barry** for accessory feedback, **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/), our ultimate beta tester**, and all community testers for sustained testing and detailed reports. Earlier contributors, including Spanish/Hungarian translation authors, remain credited above.

## CC2 Control 1.2.22

- **tharje**: Spoolman integration (#208), server sync and usage delivery guards.
- **pezso92**: Dashboard artwork/design inspiration (#187/#223), Control inspiration (#226), version report (#221/#222), drying documentation (#235).
- **Noahwhite2014**: extended pull-back patch and V4.4.1 kit testing (#172/#202/#228), drying diagnosis (#230), completion/two-colour history validation.
- **romeopuch**: timed heating request (#198/#218).
- **DamiBFryta, our ultimate beta tester**: spool history and continuing feedback (#220).

Neme77 developed and printer-tested the integrated layouts, external-spool/material controls (#233), LAN remembering (#225), completion notice (#219), Spoolman switch (#227) and experiment refinements (#232). All earlier contributors retain their credits above and in the changelog.
