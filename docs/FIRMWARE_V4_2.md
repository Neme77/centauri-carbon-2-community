# Community Firmware V4.2

Community Firmware **V4.2** for the ELEGOO Centauri Carbon 2 integrates **CC2 Control 1.1.27** while preserving the validated V4.1/R8 persistent-storage startup fix.

## Mandatory power cycle

> [!IMPORTANT]
> After installation, **do not use the printer immediately**.
>
> **Switch the printer completely off and then power it on again before doing anything else.**
>
> This full power cycle is required to realign and reinitialize **Canvas** and the related background services.

A software-only reboot is not the wording used for this release procedure: perform a full power-off / power-on cycle.

## Included software

- Community Firmware V4.2
- CC2 Control 1.1.27
- existing V4.1/R8 persistent `/opt/usr` startup fix
- original `elegoo_printer` retained unchanged
- Panda / Moonraker-like compatibility service
- OrcaSlicer Device integration
- Canvas integration
- protected expert console
- file manager, thumbnails, upload and print workflow

## CC2 Control 1.1.27

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
CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig
```

SHA-256:

```text
4e9944dd0b3e5eaff24bdaf6ed34d32002f25a069e26ec05549b612e94c9ec6b
```

Builder:

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip
```

SHA-256:

```text
dd38b9aa4ecd779a246403aa3ba8c075f231d538b12694fa4d7303cdf01b71dd
```

## Builder lineage

The V4.2 builder is derived from the validated V4.1/R8 builder and integrates CC2 Control 1.1.27.

For continuity and reproducibility, some internal script and directory names in the archive still use the `v4_1` naming inherited from the validated builder lineage. The **release designation is V4.2**.

See [BUILD.md](BUILD.md) and [SOURCE.md](SOURCE.md).
