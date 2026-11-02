# Native display refresh

Optional presentation changes for the exact supported CC2 native display.
The native printing, Canvas, motion, calibration, account and camera handlers
remain responsible for their functions. This is not a replacement printer UI.

## What changes

- Obsidian surfaces, cyan interface accents, native-size icons and a reorganized
  Home with the original printer artwork and its native light-state updates.
- A larger CC2 Community startup mark with a loading bar. The native readiness
  and Canvas subscription callbacks are retained.
- Native language-dependent labels; the CC2 Community name stays unchanged.
- Read-only LAN IPv4 and link speed in the existing WLAN information panel.
  The row refreshes when the native page is opened, without a popup or an added
  polling timer, worker thread, Wi-Fi request or network configuration action.
- Green Wi-Fi, fan selection/checkmarks and print-history accents become cyan.
  Original printer/Canvas illustrations, filament colours and warning/error
  resources are not recoloured.

## Source and provenance boundary

No ELEGOO executable, original illustration, native font, or recoloured vendor
glyph is distributed here. Supply your own legally obtained exact native
executable and stock theme resources. Build output is vendor-derived and stays
private pending a separate distribution/licensing decision.

`patch-plan.json` is the bounded presentation patch. `display_payload.S` contains
the authored fixed-address ARM extension, with literal pools and strings kept as
explicit words; `home-proof.json` and `network-proof.json` identify native calls,
hooks and addresses. The patcher first inserts an empty RX extension so the
shifted native RW data is **not** copied into the patch. It verifies the complete
input, expansion, each changed span and final output hashes.

`assets/svg` and `assets/png` contain newly authored interface art. Lucide-derived
icons retain the accompanying ISC notice. No font files are included. PNGs are
the pinned installation assets; SVG editing/rasterization must regenerate their
manifest before use. `png_palette.py` derives 50 selected cyan glyphs from exact
locally supplied stock PNGs, preserving their dimensions, alpha and non-green
pixels. It uses a lossless standard-library PNG encoder; its compression differs
from the earlier private preview's renderer.

## Private build

```sh
python3 package.py --native /private/ec-eeb001-gui \
  --stock-images /private/theme_default --baseline clean \
  --output /private/native-display-component.tar.gz
```

Baselines are explicit: `clean`, the accepted prior preview `r4`, or
`reflash-r4` (restored stock executable and the exact retained R4 resource set).
They never accept arbitrary mixtures. The printer installer checks every target
hash before replacement, keeps rollback files, verifies copied files, and changes
disk files only. No process is stopped or restarted. A complete manual reboot
of the idle printer is required to load the new display.

Use the cumulative installer after qualification; do not manually extract this
component into system directories. Keep SSH access and rollback backups.

## Validation and limits

```sh
python3 test_sources.py
python3 test_native_installer.py
```

The public tests use authored or synthetic inputs and fake local APIs. Installer
tests exercise dash and BusyBox, cold/Idle guards, transactions, damaged or
foreign files, symlinks, copy failure and exact recovery; they never execute a
vendor program. Private replay evidence records 114 bounded ARM scenarios with
native calls stubbed. Replay does not prove physical-device behaviour.

The preceding full-display preview was accepted on the owner's CC2, including
LAN display information. The latest cyan/inline-LAN refinement passed its private
offline installer gates; physical acceptance of the cumulative candidate remains
pending. Do not infer complete hardware certification from these checks.
