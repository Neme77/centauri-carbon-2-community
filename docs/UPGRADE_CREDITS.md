# Community Firmware 4.4.4 credits

Thank you for the code, ideas, translations, reviews and printer time behind
this release. Original authorship remains in CHANGELOG.md and the repository's
full contributor history.

## Upgrade investigation and testing — issue #327

These contributions helped identify the different historical installation
states and improve the preparation/recovery instructions. They are credited
for the work actually reported, not labelled as tests of the final RC4.

| Contributor | Reported contribution |
| --- | --- |
| [Eldurinn](https://github.com/Eldurinn) | Non-Combo/no-Connections diagnostics and a successful app-only installation trial. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6724025057). |
| [kalross](https://github.com/kalross) | Reproductions across native startup, camera and Connections compositions, with checksums and retained failure evidence. [Investigation](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6718815579). |
| [jgj1107](https://github.com/jgj1107) | Manifest/camera-profile diagnostics and a successful repaired 1.2.27 installation, retaining network and camera service. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6729090223). |
| [matefon](https://github.com/matefon) | Repeated installer traces and feedback from the private 1.2.28 qualification package. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6754515474). |
| [SVLoneStar](https://github.com/SVLoneStar) | Reproductions of the blocked installer and a successful private 1.2.28 update report. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6754206218). |
| [fechy](https://github.com/fechy) | Diagnostics from a 1.2.25 installation and confirmation that the recovery sequence reached 1.2.28. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6792076342). |
| [RoisinDubh79](https://github.com/RoisinDubh79) | Canvas restoration, preview-upgrade recovery and confirmation of .13 / Canvas .44 / Control 1.2.29 after recovery. This exposed the need to make preparation-before-flash unmistakable. [Confirmation](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6784297442). |
| [lpgamerzockt-ops](https://github.com/lpgamerzockt-ops) | Independent preview tests from an experimental overlay, manual quarantine, printing/camera/Canvas checks and repeated boots. RFID activation and Ethernet traffic were explicitly untested in that report. [Report](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6797350301). |
| [Noahwhite2014](https://github.com/Noahwhite2014) | Source-level diagnosis of component ordering and native-profile contradictions, with proposed recovery approaches. [Analysis](https://github.com/Neme77/centauri-carbon-2-community/issues/327#issuecomment-6716994916). |

## Features, foundations and qualification

- **Surfoo:** interface architecture, usability, localisation and test foundations.
- **beanbo:** print queue, spool tracking, plate/mesh workflows and integrations.
- **Noahwhite2014:** calibration, mesh, filament and pullback work.
- **lpgamerzockt-ops:** layer paths, screw tramming, USB responsiveness, theme
  contrast, German translation and independent printer testing.
- **pezso92**, **Skycos**, and all translation contributors: language coverage
  and refinements. Historical translation credits remain preserved.
- **kalross**, **Sceletor**, **computerkoenig**, **sametsoyoz**, **SVLoneStar**,
  and the wider community: feature requests, reproductions and focused reports.
- **OpenRFID / DnG-Crafts:** attribution for the experimental RFID decoder layout.
- **Neme77:** firmware integration, migration/recovery work, repeated physical
  qualification and maintenance of the release.

The broader project history includes many other contributors and beta testers,
including **DamiBFryta**. This release does not replace or reduce their credits.
See the repository's [full contributor history](https://github.com/Neme77/centauri-carbon-2-community/blob/develop/docs/CONTRIBUTORS.md).
