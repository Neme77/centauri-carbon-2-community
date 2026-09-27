# Community Firmware V4.2

Community Firmware **V4.2** for the ELEGOO Centauri Carbon 2 integrates **CC2 Control 1.1.30** while preserving the validated V4.1/R8 persistent-storage startup fix.

## Automatic post-install realignment

> [!IMPORTANT]
> No additional manual reboot or power-off/power-on cycle is required after the
> firmware update has completed normally.

After the LAN code is saved, CC2 Control restarts its own supervised service
and reconnects MQTT, Canvas and snapshot state without restarting printer
services. The page can be refreshed after roughly 30 seconds if synchronisation
takes longer than expected.

## Included software

- Community Firmware V4.2
- CC2 Control 1.1.30
- existing V4.1/R8 persistent `/opt/usr` startup fix
- original `elegoo_printer` retained unchanged
- Panda / Moonraker-like compatibility service
- OrcaSlicer Device integration
- Canvas integration
- protected expert console
- file manager, thumbnails, upload and print workflow

## CC2 Control 1.1.30

### Bed Levelling

- unified Bed Levelling navigation
- mesh, saved profiles and four-screw workflow combined
- screw corrections shown in microns
- guarded optimized-reference suggestion
- reference optimization is advisory only; CC2 Control does not turn or move screws automatically

### Quick Actions and Emergency Stop

Dashboard shortcuts are now operational Quick Actions:

- Home All
- All Heaters Off
- Fans Off
- Motors Off

Emergency Stop is available globally and requires a deliberate one-second press-and-hold before sending the native `M112` command.

### Settings and themes

Settings are split into:

- Connection
- Safety
- Integrations
- Appearance
- About

The interface includes:

- original Dark theme
- new monochrome Light theme
- persistent theme selection
- shared appearance between browser and OrcaSlicer

## Release files

Firmware:

```text
CC2_V4_2_STOCK_20260927_151944_1a06ebe1.zip.sig
```

SHA-256:

```text
1e9d7eacf3a8f55b1e019d41af3def59fd2a9a6e1eb39090ee6ae2ac05a4797a
```

Builder:

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.30_INIT_FIX_R2_RELEASE.zip
```

SHA-256:

```text
eab6bae24857d86ee8a3bc15c5002329f7b33f64464b3cfe912b9aa636237527
```

## Builder lineage

The V4.2 builder is derived from the validated V4.1/R8 builder and integrates CC2 Control 1.1.30.

For continuity and reproducibility, some internal script and directory names in the archive still use the `v4_1` naming inherited from the validated builder lineage. The **release designation is V4.2**.

See [BUILD.md](BUILD.md) and [SOURCE.md](SOURCE.md).
