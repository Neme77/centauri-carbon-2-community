# CC2 Control 1.2.7 — combined V4.4 test candidate

This is an application-only candidate for Community Firmware V4.4 based on
ELEGOO 02.01.01.07. It does not install or replace printer firmware, the vendor
printer process, the callback library or firmware patches. The installer checks
the actual firmware and running printer fingerprints before changing files.

Included pull requests:

- #117: V4.4-only application installer, with backup and rollback; Hungarian
  by @pezso92 and advanced bed-mesh features by @beanbo already in its base.
- #118: available storage space, 15-item file/history pages, and the optional
  persistent live camera setting.
- #119 by @beanbo: clear stale Canvas tool maps before external-feed starts,
  requiring an acknowledgement; send calibrated multicolour mappings together.
- #120: correct OrcaSlicer file temperatures and sum every tool's filament mass.

The #103/#114 ejection experiments are excluded. Continuous Canvas UDS telemetry
and the unfinished Settings updater are excluded. Canvas telemetry retains the
current MQTT path; #119 uses one acknowledged UDS command per external start.
The ordinary installer preserves LAN credentials, preferences and saved data.

## Printer validation before merge

Use the installation instructions in `docs/UPDATE_CC2.it.md` (or the English
`docs/UPDATE_CC2.md`), with the 1.2.7 candidate ZIP. Do not reset printer or CC2
configuration for this test. The printer must be idle, both targets zero and
both heaters at or below 50 °C. Keep the previous package and installer backup.

Confirm version 1.2.7, connection and Canvas after installation and after a
reboot. Check Hungarian, advanced mesh and the existing quick actions. Check
internal/USB capacity, the second file/history page and the persistent camera
setting while changing pages; disabling it must restore the previous behaviour.

The supplied OrcaSlicer regression file should show a 55 °C bed, 220 °C nozzle
and approximately 21.2 g. These are file estimates, separate from measured spool
consumption during printing.

For #119, test external-feed startup after a previous mapped Canvas job, then
a calibrated multicolour start with every selected tool mapped. Follow the
printer's normal external-feed setup. The author's external-feed test passed
on V4.2-R5; neither V4.4 scenario is yet qualified. A failed clear must refuse
the start. Do not merge these PRs until the user's V4.4 trial is confirmed.

## Attribution and release status

Retain the original #119 commits and credit @beanbo. Thanks to the beta tester
who supplied the OrcaSlicer export for #120. This candidate is not an official
release, and the published V4.4 firmware and previous 1.2.5 artifact are unchanged.
