# Changelog

This file records user-visible project changes. Signed artifacts and exact
checksums remain attached to each GitHub release.

## Unreleased

- PID calibration and printer error reports are translated into Russian and Chinese; they were shown in English.
- Bed Levelling has a build-plate library: save each plate's mesh and Z offset once and mount the plate again later. A plate whose mesh is not in its side's slot is written there and the printer restarts.
- Bed mesh calibration from Bed Levelling homes X, Y and Z first when the printer is not homed, instead of being refused by the console.
- An OrcaSlicer upload whose name already exists is saved as `name (1).gcode` and so on instead of being refused with HTTP 409; nothing is overwritten.
- CC2 Control registers with the printer even when another app's Canvas reply arrives first after it connects. Before, it then never registered, so print and Canvas commands were refused and the updater rolled back.
- The mainboard fan no longer runs on forever after homing or calibration. The firmware never releases idle motors, and that fan runs while they are powered, although every fan in the UI reads 0 %. CC2 Control now sends `M84` after ten idle minutes with the heaters off, as stock Klipper does.
- The printer no longer runs out of memory about 11 hours after each boot. Its firmware keeps every request received on its local socket in memory, and CC2 Control asked it for `info` every two seconds. Now it asks only when the telemetry stream has been silent for three seconds; a working printer sends about two updates per second.
- The Job page no longer queries the printer every two seconds for print objects: the excluded and current objects come from the telemetry stream, the object list is asked once per job, and an idle Job page asks for nothing. Before a print defines its objects, the page says no objects are reported instead of "Object status unavailable".

## CC2 Control 1.1.31fix — 2026-10-04

- Restore and stabilize 3D mesh viewing, including meshes with large height offsets and camera rotation.
- Bound automatic Canvas discovery across MQTT reconnects and expose diagnostic counters; explicit Sync remains available.
- Identify G-code tools by colour and material during Canvas print setup.
- Fix intermittent uploads when headers and file data arrive together; report duplicate-file refusals clearly.
- List the newest 128 G-code files and show when older files are hidden; move upload controls above the file list.
- Windows launcher includes explicit SSH host-key recovery and forwards command-line arguments.
- Printer-tested installer includes all merged fixes; the thumbnail-priority experiment was discarded.

Internal service version remains 1.1.31. This standalone update does not replace the V4.2-R5 firmware release.

## Earlier integrated changes

- Files lists the 128 newest G-code files of a storage instead of an arbitrary 128, and says when older files are hidden.
- A browser upload refused by the printer, for example because the file already exists, reports that error instead of a network error.
- Show the G-code upload card above the Files list (above the file details on wide layouts), so a long list no longer pushes it out of reach.
- Frame the 3D bed mesh around its actual height range so meshes with large positive or negative offsets remain visible.
- Bound automatic Canvas discovery across MQTT reconnects, allow explicit Sync recovery and expose discovery counters in MQTT diagnostics.

- Print setup shows each G-code tool’s filament colour and material beside its Canvas slot selector, with an explicit fallback when metadata is missing.

- Select timelapse recording in the print popup, including calibrated starts.
- Delete individual or loaded completed print-history entries with confirmation and Idle guards.
- Coordinate live camera viewing across CC2 Control windows; failed ownership requests do not open a stream.
- Follow native timelapse rendering until machine state 12 ends before refreshing history.

- Keep object-query UDS sessions open, match response IDs and reject incomplete
  replies to reduce connection churn when opening Job. One supervised print
  completed without recurrence of the observed vendor dispatcher crash; its
  underlying cause remains unconfirmed.
- Show the real internal-light state in Dashboard Quick Actions.
- Require fresh idle telemetry for all-heaters-off commands and disable their
  buttons during printing.
- Let the standalone updater wait up to 240 seconds for startup and registration.

- Delay CC2 Control startup for 60 seconds after detecting `elegoo_printer`
  to give vendor hardware initialization more time to settle.

### Features

- Firmware builder: pin the complete printer-validated CC2 Control integration
  from commit `00f1f89`; run all component host tests and reject stale prepared
  manifests using the source commit and archive checksum. Full OTA validation
  remains separate from the successful CC2 Control beta tests.

- Translations are keyed by identifier (`files.upload_file`) instead of by the English sentence, so the English text can be reworded without touching the other languages.
- The interface follows the browser language (English, Italian, French, Chinese or Russian) until a language is saved on the printer.
- Added a Simplified Chinese (`zh`) interface translation, selectable in **Settings → Appearance**.
- Added a Russian (`ru`) interface translation, selectable in **Settings → Appearance**.
- Redesigned web interface: full-width header with the emergency stop, a side
  menu that collapses to icons, a link for every section (`#files`, `#bed`…) and
  Lucide icons throughout.
- One printer-link indicator (connected, waiting for the printer, reconnecting,
  unreachable) instead of two always-green badges.
- In-page confirmations, red dismissible error messages, print progress in the
  browser tab title, drag-and-drop G-code upload and command history in the
  console.
- Every message, machine state and accessible name is translated in Italian,
  French, Chinese and Russian.
- Four more colour themes (Dracula, Nord, Monokai, Solarized Light) next to Light and Dark, chosen in **Settings → Appearance** and saved on the printer like the language; `/api/preferences` now accepts any lowercase-hyphenated theme identifier.
- The machine state is followed by what the printer is doing within it (**Printing · Heating bed**, **Manual homing · Failed**…), using the vendor sub-state codes.
- Printer refusals of MQTT requests are shown with the vendor meaning of their code (busy, print file missing, no bed levelling data…) instead of passing silently after `202 Accepted`; `/api/printer` reports the latest one as `printer_error`.
- Invalidate the Canvas auto-refill readback on MQTT disconnect and request it
  again after registration, so external changes made while offline are shown.
- **Canvas → Auto refill** shows and changes the Canvas setting that continues from another slot with the same filament when a spool runs out.
- New **History** page: the print jobs recorded by the printer with start time, duration and result; ready time-lapse videos can be downloaded and recorded frames rendered into a video while the printer is Idle. The LAN access code stays on the printer.

### Removed

- Interface elements that showed nothing real: the printer-name field, the
  storage summary computed from the file list, the invented expert-console
  timeout and duplicated cards on the dashboard, job and control pages.

## CC2 Control 1.1.31 — 2026-09-27

### Features

- Added OrcaSlicer Moonraker-agent Canvas filament synchronization through
  read-only `/server/info` and `/server/database/item?namespace=lane_data`
  compatibility endpoints.
- Added LAN access-code replacement and revalidation from **Settings →
  Connection**.

### Fixes

- LAN-code changes are written atomically and restart only CC2 Control, leaving
  printer services and an active print untouched.
- Canvas tray material, colour and nozzle temperature are exposed to OrcaSlicer
  without adding a new polling loop or MQTT subscription.

Thanks to **@efiten** for the original OrcaSlicer compatibility proposal,
investigation and printer-side validation.

## V4.2 — CC2 Control 1.1.30 — 2026-09-27

### Features

- Unified Bed Levelling workflow.
- Added Side A and Side B saved-mesh visibility.
- Added screw corrections in microns and guarded reference optimisation.
- Added operational dashboard Quick Actions and global emergency stop.
- Added compact settings panels and persistent dark/light themes.
- Simplified print preparation to Side A/Side B plus one calibration option.
- Automatically uses the selected side's saved mesh, adaptive G-code probing or
  full-bed calibration as appropriate.

### Fixes

- Fixed file-manager, upload, Canvas, thumbnail and print handling.
- Removed remaining demonstration values from live job and object statistics.
- Completed elapsed, remaining and total-layer status handling.
- Added first-run service realignment after LAN-code registration, removing the
  old post-install manual reboot/power-cycle requirement.
- Rejected implausible G-code temperature metadata instead of displaying values
  such as 6211 °C in the file preview.
- Restored fast thumbnail display by loading the image before full metadata and
  caching metadata by storage, path, size and modification time.
- Requires full-bed calibration when the selected side has no saved mesh.

## V4.1 — CC2 Control 1.1.25 — 2026-09-26

### Features

- Integrated the local CC2 Control service.
- Established the V4.1/R8 reproducible builder snapshot.

### Fixes

- Fixed persistent-storage startup ordering.

Earlier release details remain available through GitHub Releases, tags and Git
history.
