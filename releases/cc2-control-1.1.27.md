# CC2 Control 1.1.27

CC2 Control **1.1.27** refines the daily workflow and interface while preserving the existing printer protocol and safety model.

## Bed Levelling

- unified Bed Levelling sidebar entry
- mesh, saved Side A / Side B profiles, active mesh and screw measurement in one workflow
- screw corrections displayed in microns
- guarded Optimized reference adjustment suggestion
- optimization remains advisory; CC2 Control never alters screws automatically

## Controls and safety

- operational Quick Actions: Home All, All Heaters Off, Fans Off and Motors Off
- global Emergency Stop on every page
- one-second press-and-hold protection
- printer-state guards remain active

## Settings and appearance

- Connection, Safety, Integrations, Appearance and About are separate panels
- compact layout
- persistent Dark theme
- new monochrome Light theme
- shared theme state between browser and OrcaSlicer

## Preserved settings

The updater preserves:

- LAN code / CC2 Control configuration
- UI preferences
- material presets

Rollback directory:

```text
/opt/usr/cc2-control-rollback-1.1.26
```

## Package

```text
CC2-Control-1.1.27-Multiplatform-Update.zip
```

SHA-256:

```text
17012fc53eaca3bd3ab1a1829172c9d12e8ed56dcf4b135d9e17a8acfc6fdccc
```

## Source

```text
CC2-Control-1.1.27-Complete-Source-and-Builder.zip
```

SHA-256:

```text
1c3039678c27cbad6e9916dc203d4ea1b87910c7c35f7702c6a1195b33a2be63
```

The source archive contains the CC2 Control 1.1.27 source, web UI, validation tests, firmware-integration files and the multiplatform package builder.

## Mandatory power cycle

> [!IMPORTANT]
> After installation, **switch the printer completely off and then power it on again before doing anything else**.
>
> The full power cycle is required to realign Canvas and the related background services.

Wait approximately **30–60 seconds** after power-on before normal use.
