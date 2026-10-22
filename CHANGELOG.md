# Changelog

All notable user-visible changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Releases are named after the CC2 Control version and, when the complete firmware changes, the Community Firmware version. Signed artifacts and exact checksums are attached to each [GitHub release](https://github.com/Neme77/centauri-carbon-2-community/releases). Installation guides: [English](docs/UPDATE_CC2.md), [Italiano](docs/UPDATE_CC2.it.md). Contributor credits: [docs/CONTRIBUTORS.md](docs/CONTRIBUTORS.md).

## [Unreleased]

### Added

- Canvas: an explicit action to recover an unassigned filament head by loading any present slot (#190).
- Files: older G-code files stay reachable, and uploads stream up to 512 MiB (#197).
- Object exclusion: a single refined 2D bed map replaces the previous views (#199).

### Changed

- Web UI: shared design system (page, card and dialog titles, spacing, radius), documented in `AGENTS.md`, with keyboard focus, accessible names and landmarks checked with axe (#196).
- The demo (`npm run preview:ui`) starts in English.
- Homepage: simplified, with the firmware and CC2 Control downloads highlighted (#201).

## [CC2 Control 1.2.19] - 2026-10-21

For Community Firmware V4.4/V4.4.1 (ELEGOO base 02.01.01.07). Settings and libraries are retained. **A full printer reboot is required after installation**: the camera fix activates at the next complete printer start. The firmware image and builder downloads are unchanged and will be updated separately.

### Added

- Interactive object map in Job → Object exclusion → Bed map: 256 × 256 mm view, numbered markers and outlines, linked list. Exclusion keeps its confirmation and original G-code ID; no extra polling. Idea and prototype by Nick Plateringen-Paul (#188).
- Panda/DragonBreath: a read-only port 7125 bridge reports the filename and available filament/usage metadata for DragonBreath AUTO zones. It is not direct MQTT heater control. By Noahwhite2014 (#181, #182).

### Changed

- Files with invalid or epoch dates show the date as unavailable, and undated files sort after valid dates. G-code and original timestamps are untouched (#185).
- Canvas and mobile refinements by pezso92: no save on an unchanged Enter, no duplicate restart submission, slot observations annotated with recent printer context, header actions and the heater link kept accessible on phones (#175, #176, #177, #180).

### Fixed

- Camera: reclaim the stacks of finished video-client threads and restore the readiness notifications that local/Matrix video needs after a camera-only restart through the patched launcher. The original camera executable is retained; there is no watchdog, automatic restart, extra stream slot or Wi-Fi tuning. Diagnosis and reproduction by tharje (#160, #183, #186).

## [CC2 Control 1.2.18] - 2026-10-19

Only Community Firmware V4.4/V4.4.1. Update directly from an earlier compatible version; preferences and libraries are retained.

### Added

- Canvas pull-back after the cut: set the native 60–350 mm distance, with backup, restore-default and restart now or later. Needs an idle printer and applies after restart. By Noahwhite2014 (#168).
- Panda Breath / Dragon Breath page in Settings → Integrations, embedding the heater's own web page. It does not use MQTT. By Noahwhite2014 (#166, #167).
- Canvas recovery guidance: explains the native display recovery when a reboot leaves filament in the head without an assigned slot. No automatic slot selection, heating or feeding (#162).

### Changed

- 88 Spanish, Russian and Simplified Chinese strings completed, and updater compatibility wording clarified in nine languages. By pezso92 (#163, #164, #165).
- Stricter configuration parsing, restart-tracking and write-failure guards, and consistent Panda Breath / Dragon Breath names (#169, #170).

## [CC2 Control 1.2.17] - 2026-10-18

Only Community Firmware V4.4/V4.4.1. Application update; firmware and callback unchanged. Features by pezso92 (#155, #156, #157, #158).

### Added

- Manual diagnostics export from Settings → About, with an explicit allowlist (version, connection and memory status, temperatures, Canvas counts). No credentials, addresses, filenames or raw API responses; no automatic upload.
- Observed Canvas timeline: up to 24 recent slot-state changes kept in browser memory, with a clear button and no additional periodic requests. It resets on reload and is not a physical-movement log.

### Changed

- Orca connection checks test the real Moonraker and Canvas lane endpoints and distinguish a disconnected printer, incomplete lane data and endpoint errors. This helps diagnosis; it does not guarantee that every OrcaSlicer version synchronises.

### Fixed

- Diagnostic Canvas envelope, ambiguous timeline rows rejected, repeated Orca tests guarded, and the timeline icon registered to prevent a rendering failure.

## [CC2 Control 1.2.16] - 2026-10-18

Only Community Firmware V4.4/V4.4.1. Firmware, callback and configuration are retained.

### Added

- Backup and selective restore of preferences, material presets, saved plate/nozzle meshes and spool inventory/history, with a preview and category selection. Credentials, firmware, G-code/video and official A/B meshes outside the library are excluded; spool tracking starts disabled after a restore (#144, #145, #148, #147).
- Optional Console monitor: bounded passive G-code reports over the existing connection. It is not the full system log (#149).
- Print mesh status (saved, confirmed missing, unknown) with a read-only Refresh, by pezso92 (#150, #151).

### Changed

- The saved language loads before the meaningful interface render, with bounded requests and a fallback. No offline cache was added.
- Hungarian: 42 refinements and failed-save feedback by pezso92. The language menu is ordered alphabetically.
- All six Settings tabs stay readable and wrap on narrow screens.

### Fixed

- A failed mesh read no longer silently selects calibration, and A/B status is independent.

## [CC2 Control 1.2.15] - 2026-10-17

Only Community Firmware V4.4/V4.4.1. Application update; installed firmware, callback patch, preferences and profiles are retained. V4.3 and stock firmware are unsupported. Integrated in #142 (#138, #139, #140, #141).

### Added

- Update notification bell beside Emergency Stop for a newer compatible update from the signed catalog. It opens Settings → Updates and never downloads or installs automatically.
- Library mesh selector: choose a saved measurement by plate, side, temperature and timestamp. **Mount and activate** confirms the physical plate and verifies the active profile, matrix and bounds before reporting success.
- Selectable calibration hold from 0 to 60 minutes after the target temperature (2 minutes by default).

### Changed

- 20 remaining Canvas, object-position and plate/calibration Hungarian translations by pezso92.
- The selected calibration destination and timing are kept when changing pages. Restart is requested only when needed, with separate consent.

## [CC2 Control 1.2.14] - 2026-10-16

Only Community Firmware V4.4/V4.4.1; do not install on V4.3 or stock ELEGOO firmware. Preferences and profiles are retained.

### Added

- Plates can be created before their first mesh and selected for calibration. Calibration works from 30 to 110 °C, and Side A/B labels are shown for default/default1.
- Measurement date with exact time and timezone in mesh details.
- Spanish by asiersan (#130, #133), and Hungarian refinements by pezso92 (#129, #132). Nine languages are now supported.

### Changed

- Object Exclusion lists objects from front to back and left to right with readable names and numbered copies, keeping original G-code identifiers separate. It handles ELEGOO's string-encoded coordinates and clears completed-print objects.
- Canvas state distinguishes empty slots, filament present but unloaded from the head, loaded filament and transitions. Actions are enabled only when applicable.
- Localised punctuation by Surfoo (#128, #135).
- Advanced mesh builds on beanbo's #104.

### Fixed

- Persistent live camera keeps its original size. Fan sliders drag correctly and numeric values still apply with Enter.
- Uncalibrated profiles cannot be mounted.

### Notes

- Library compatibility: older applications enforcing a 40 °C minimum cannot read measurements created at 30–39 °C and may discard uncalibrated profiles when saving. Back up the plate library before reverting.
- Experimental Canvas ejection, continuous Canvas UDS and webcam-service restart are excluded.

## [CC2 Control 1.2.11] - 2026-10-14

Stable application update (#127). Signed package SHA-256: `2d08659473b4245bf6f43434857a143338af4342dfd6c40da19b8d26faa23074`.

### Added

- A signed stable catalog for discovery, download/verification and separately confirmed installation from the panel, with the configured LAN code.

### Changed

- Localised descriptions come from the signed catalog in all eight app languages without another catalog fetch, with an explicit English fallback label.
- Settings → Updates hides the descriptions of installed and older releases.

## [Community Firmware V4.4.1] - 2026-10-14

Includes CC2 Control 1.2.10 (#126). Firmware artifact SHA-256: `61356c3a83be374988d6154d429c27c403985974663d3780b194164e774a22c1`.

### Added

- Settings → Updates works from the first installation, without a separate computer-assisted bootstrap: the verifier, HTTPS downloader, CA bundle, public key, runner and notices are staged in the immutable rootfs.
- The advanced plate/nozzle library is explicitly persisted under `/opt/usr/cc2-control/bed-plates.json`.
- First-installation and update guides in English and Italian, explaining the separate LAN-code authorisation.

### Changed

- ELEGOO base 02.01.01.07, the native callback and firmware patches, and compatibility family 4.4 are preserved.

## [CC2 Control 1.2.10] - 2026-10-14

Only V4.4. Merged scope: #104, #115, #116, #117, #118, #119, #120, #121, #124.

### Added

- Hungarian by pezso92; advanced temperature/nozzle mesh with soaked calibration by beanbo; V4.4-only installer with preservation and backup checks.
- Available internal and mounted USB space is shown; Files and History are paginated at 15 items without extra page-driven requests.
- Persistent camera in Settings → Appearance: one original stream across navigation and hidden pages, with manual Start/Pause, disable, close and viewer handover.
- Signed GitHub update discovery, bounded HTTPS download and verification, separate installation confirmation, signed local upload, real firmware fingerprints and configuration-preserving installation.

### Changed

- Old Canvas mappings are cleared before external-feed starts, and calibrated multicolour mappings are sent together (beanbo). Canvas telemetry stays on MQTT.
- Orca bed-temperature estimates are corrected and all filament tool masses are summed. Unknown estimates stay unavailable.

### Fixed

- The empty file-analysis notification and close icon no longer appear when the Canvas print dialog opens.

## CC2 Control 1.2.5 - 2026-10-13

Application update for Community Firmware V4.4.

### Added

- Complete Hungarian interface by pezso92 (#115): 879 entries and placeholders, included in the pinned builder snapshot, with tests for selection, saved preference, hu-HU detection and mobile layouts.
- Bed mesh calibration with a bed temperature (40–110 °C, 60 by default), a soak time (10 minutes by default) and a nozzle, saved to a build plate. It runs on CC2 Control, so the page can be closed, and it can be stopped until probing starts. A plate keeps one measurement per temperature and nozzle, and a print uses the one nearest to the file's bed temperature (or the one chosen in the print dialog). A nozzle list adds each nozzle's Z correction to the plate's Z offset. By beanbo.
- The installer checks the installed and running fingerprints before stopping the service and refuses V4.3, original firmware and unknown bases. It needs a connected, fresh Idle state and cold heaters with zero targets.

### Fixed

- The printer's Canvas colour table is cleared before a print started on the external feed, as the touchscreen does. Before, a directly fed print (for example TPU with the Canvas cable unplugged) could pause at layer 0 with error 1210 or 1220. The print does not start unless the printer acknowledges the clear.
- The whole Canvas map of a multicolour print started with bed calibration is sent in one colour-table command. Before, only the last tool stayed mapped.

## [Community Firmware V4.4 / CC2 Control 1.2.3] - 2026-10-13

Built on official 02.01.01.07 only. Complete signed firmware, public source builder and SHA-256 checksums are in the [V4.4 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4). CC2 Control 1.2.3 is included in the firmware; do not install this application separately on V4.3.

### Added

- Quick Actions for PID and Input Shaper, restored Input Shaper tools, and guarded hotend/bed PID calibration.
- Fan percentage inputs (0–100) applied with Enter beside the sliders, with validation and readback.
- Confirmed reset of material, colour and temperature records for all four Canvas slots while idle. It does not unload filament or delete the spool inventory.
- Complete Polish and Czech translations. The interface supports English, Italian, French, Simplified Chinese, Russian, Polish and Czech, with six themes.
- English and Italian first-installation and OrcaSlicer/Canvas guides, linked from the homepage.

### Changed

- Firmware patches ported to the new base, keeping the qualified O0 callback runtime, authentication, native camera integration and protected live Z offset.
- Material profile names accept internal spaces, including in Canvas commands, with the 16-character limit and command-character guards kept. The preheat button row is replaced by a full-width themed selector.
- Live camera streaming continues while the page scrolls; manual stop, hidden-tab suspension and viewer handover are retained.
- Better G-code filament colour parsing and Canvas print mapping explanations. Empty slot icons are dimmed and neutral according to printer status.
- Control panel cards are aligned.

### Fixed

- LAN/WAN transitions and registration recovery. Native light commands receive correlated acknowledgements and synchronise the printer display, and own-printer PONG freshness keeps the connection indicator aligned with the link.

### Notes

- Beta validation reported 40-hour active printing sessions without reported problems and more than 20 confirmed long tests. These reports cover the beta cycle, not an exact published artifact.
- Excluded: PR103/104 and the experimental full Canvas UDS 1.2.4 variant. The existing build-plate library is included; the temperature/nozzle mesh extension of PR104 is not.

## [Community Firmware V4.3 / CC2 Control 1.2.0] - 2026-10-09

Pre-release: signed firmware, standalone callback updater, updated source builder and checksums. The source includes the effective-Z-offset wording clarification, which the attached tested binaries predate.

### Added

- Fingerprint-gated experimental O0 callback runtime for vendor memory retention, with native preflight, fallback and a disable switch. The request log remains unbounded, and encouraging hardware reports do not prove every OOM/803 cause is resolved.
- Standalone CC2 + callback updater without reflashing, preserving the spool inventory during rollback.
- Hotend/bed PID calibration, printing temperature changes, and printer error popups that do not recur solely from historical events. PID and error reports are translated into Russian and Chinese.
- Spools page tracking filament spools in the Canvas slots and on the external holder, charged with what the printer really extrudes (purges and cancelled prints included). New filament in a slot asks which spool it is, a spool that runs out is set to zero, weigh-ins correct the count, and the print dialog warns when a spool holds less than the file needs. Off until switched on.
- Build-plate library in Bed Levelling: save each plate's mesh and Z offset once and mount the plate later. A plate whose mesh is not in its side's slot is written there and the printer restarts.
- Large G-code files are analysed asynchronously with a bounded queue and cache.

### Changed

- Bed mesh calibration homes X, Y and Z first when the printer is not homed, instead of being refused by the console.
- An OrcaSlicer upload whose name already exists is saved as `name (1).gcode` instead of failing with HTTP 409; nothing is overwritten.
- Fewer requests that feed vendor memory retention: `info` is requested only after three seconds of telemetry silence, instead of every two seconds. The Job page takes the excluded and current objects from the telemetry stream, asks for the object list once per job, and an idle Job page asks for nothing.
- CC2 Control starts 30 seconds after the vendor process appears.

### Fixed

- CC2 Control registers with the printer even when another app's Canvas reply arrives first after it connects. Before, print and Canvas commands were refused and the updater rolled back.
- The mainboard fan no longer runs on after homing or calibration: `M84` is sent after ten idle minutes with the heaters off, as stock Klipper does.
- Expiry of queued complete HTTP requests.

## CC2 Control 1.1.31fix - 2026-10-04

Standalone update. The internal service version remains 1.1.31, and it does not replace the V4.2-R5 firmware release.

### Added

- Windows launcher with explicit SSH host-key recovery, forwarding command-line arguments.
- Print setup shows each G-code tool's filament colour and material beside its Canvas slot selector, with a fallback when metadata is missing.
- Timelapse recording selectable in the print popup, including calibrated starts.
- Deletion of individual or loaded completed print-history entries, with confirmation and Idle guards.
- Live camera viewing coordinated across CC2 Control windows; failed ownership requests do not open a stream.

### Changed

- Files lists the 128 newest G-code files of a storage, says when older files are hidden, and shows the upload card above the list.
- Automatic Canvas discovery is bounded across MQTT reconnects, with diagnostic counters; explicit Sync remains available.
- Object-query UDS sessions stay open and incomplete replies are rejected, reducing connection churn when opening Job.
- Dashboard Quick Actions show the real internal-light state. All-heaters-off commands need fresh idle telemetry and are disabled while printing.
- The updater waits up to 240 seconds for startup and registration, and CC2 Control starts 60 seconds after `elegoo_printer` is detected.
- Native timelapse rendering is followed until machine state 12 ends before the history refreshes.

### Fixed

- Intermittent uploads when headers and file data arrive together; duplicate-file refusals and printer-refused browser uploads are reported clearly instead of as a network error.
- 3D mesh viewing, including meshes with large height offsets and camera rotation.

## CC2 Control 1.1.31 - 2026-09-27

### Added

- OrcaSlicer Moonraker-agent Canvas filament synchronisation through the read-only `/server/info` and `/server/database/item?namespace=lane_data` endpoints. Thanks to @efiten for the original proposal, investigation and printer-side validation.
- LAN access-code replacement and revalidation from Settings → Connection.

### Fixed

- LAN-code changes are written atomically and restart only CC2 Control, leaving printer services and an active print untouched.
- Canvas tray material, colour and nozzle temperature are exposed to OrcaSlicer without a new polling loop or MQTT subscription.

## [V4.2 / CC2 Control 1.1.30] - 2026-09-27

### Added

- Unified Bed Levelling workflow, Side A and Side B saved-mesh visibility, and screw corrections in microns with a guarded reference optimisation.
- Operational dashboard Quick Actions and a global emergency stop.
- Compact settings panels and persistent dark/light themes.
- Four more colour themes (Dracula, Nord, Monokai, Solarized Light), chosen in Settings → Appearance and saved on the printer; `/api/preferences` accepts any lowercase-hyphenated theme identifier.
- Redesigned web interface: full-width header with the emergency stop, a side menu that collapses to icons, a link for every section and Lucide icons throughout; one printer-link indicator instead of two always-green badges.
- In-page confirmations, red dismissible errors, print progress in the browser tab title, drag-and-drop G-code upload and command history in the console.
- The machine state is followed by what the printer is doing within it (for example "Printing · Heating bed"), using the vendor sub-state codes. Printer refusals of MQTT requests are shown with the vendor meaning of their code, and `/api/printer` reports the latest one as `printer_error`.
- Canvas → Auto refill shows and changes the setting that continues from another slot with the same filament when a spool runs out. Its readback is invalidated on MQTT disconnect and requested again after registration.
- New History page: print jobs recorded by the printer with start time, duration and result; ready time-lapse videos can be downloaded and recorded frames rendered while the printer is Idle.
- Translations keyed by identifier (`files.upload_file`) instead of the English sentence. The interface follows the browser language until one is saved on the printer, and Simplified Chinese and Russian were added.

### Changed

- Print preparation is simplified to Side A/Side B plus one calibration option, automatically using the selected side's saved mesh, adaptive G-code probing or full-bed calibration, and requiring full-bed calibration when the side has no saved mesh.
- Fast thumbnail display by loading the image before the full metadata and caching metadata by storage, path, size and modification time.
- Every message, machine state and accessible name is translated in Italian, French, Chinese and Russian.

### Fixed

- File-manager, upload, Canvas, thumbnail and print handling; elapsed, remaining and total-layer status.
- First-run service realignment after LAN-code registration, removing the post-install manual reboot.
- Implausible G-code temperature metadata (such as 6211 °C) is rejected in the file preview.
- Vendor dispatcher crash risk and connection churn reduced (one supervised print completed without recurrence; the underlying cause remains unconfirmed).

### Removed

- Interface elements that showed nothing real: the printer-name field, the storage summary computed from the file list, the invented expert-console timeout, duplicated dashboard/job/control cards and the remaining demonstration values.

## [V4.1 / CC2 Control 1.1.25] - 2026-09-26

### Added

- The local CC2 Control service, and the V4.1/R8 reproducible builder snapshot.

### Fixed

- Persistent-storage startup ordering.

Earlier release details remain available through GitHub Releases, tags and Git history.

[Unreleased]: https://github.com/Neme77/centauri-carbon-2-community/compare/CC2-Control-1.2.19-V4.4...HEAD
[CC2 Control 1.2.19]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.19-V4.4
[CC2 Control 1.2.18]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.18-V4.4
[CC2 Control 1.2.17]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.17-V4.4
[CC2 Control 1.2.16]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.16-V4.4
[CC2 Control 1.2.15]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.15-V4.4
[CC2 Control 1.2.14]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.14-V4.4
[CC2 Control 1.2.11]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.11-V4.4
[Community Firmware V4.4.1]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.1
[CC2 Control 1.2.10]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.10-V4.4
[Community Firmware V4.4 / CC2 Control 1.2.3]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4
[Community Firmware V4.3 / CC2 Control 1.2.0]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.3
[V4.2 / CC2 Control 1.1.30]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.2
[V4.1 / CC2 Control 1.1.25]: https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.1
