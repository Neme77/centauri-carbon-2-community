# CC2 Control 1.1.26 — Multiplatform Hotfix

CC2 Control **1.1.26** is a targeted dashboard and workflow update for **Community Firmware V4.1**.

It updates CC2 Control only. A complete firmware reflash is **not required**.

## Fixes and improvements

### Temperature controls

- nozzle and bed temperature changes are now applied atomically;
- Control page live readings are aligned with the current printer telemetry;
- temperature workflows are more consistent between dashboard controls and printer state.

### G-code file handling

- uploads now correctly support filenames containing spaces;
- UTF-8 G-code filenames are supported;
- file-manager multi-selection is available;
- bulk deletion is performed sequentially for safer operation;
- file copy between internal storage and USB is streamed instead of being fully buffered in memory;
- the demo/example file is no longer used when a real file has not been selected.

### Print workflow

- Side A / Side B selection is available in the print dialog where applicable;
- the dashboard better distinguishes the selected print source and stored files.

### Bed Mesh

- stored `default` and `default1` meshes are recognized correctly;
- Side A, Side B, active and adaptive mesh states are represented separately;
- saved, adaptive and full-bed mesh workflows are differentiated more clearly in the UI;
- Bed Mesh presentation and graphics have been cleaned up.

### Dashboard and UI

- multiple visual inconsistencies and dashboard layout issues have been corrected;
- controls and live values are presented more consistently;
- general graphics and workflow clarity have been improved.

### Persistent settings

The installer preserves:

- LAN code / CC2 Control configuration;
- UI preferences;
- material presets.

### Safer hotfix installation

- the existing CC2 Control installation is backed up before replacement;
- the new service must pass a versioned health check;
- if the health check fails, the updater automatically restores the previous installation;
- rollback copy: `/opt/usr/cc2-control-rollback-1.1.25`.

## Multiplatform installer

One release package supports:

- Windows 10 / 11 through PowerShell;
- Linux through the shell installer;
- macOS through the same shell installer.

Package:

```text
CC2-Control-1.1.26-Multiplatform-Hotfix.zip
```

SHA-256:

```text
3b73afaed88752d42240442e1ed3bcd9417e95583eaa77878548a8de2b93c15e
```

Both installation methods use standard `ssh` and `scp`.

The default printer root password is:

```text
MTY4ODE2
```

If the password has been changed by the user, the custom password must be used instead.

For step-by-step instructions, including first-time SSH prompts and Windows OpenSSH requirements, see [Installing CC2 Control 1.1.26](../docs/INSTALL_CC2_CONTROL_1_1_26.md).

## Source code

The complete CC2 Control 1.1.26 source snapshot is published with the GitHub release.

Package:

```text
CC2-Control-1.1.26-Source.zip
```

SHA-256:

```text
e11200d256c81e3be2fac9d1ae91cf06af7cb51c178fbabec9b1a7ef6004556e
```

The archive includes the C backend source, web interface, tests, startup scripts, configuration examples, development tools and firmware-integration files.

- [Download source](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.26/CC2-Control-1.1.26-Source.zip)
- [Download source checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.26/CC2-Control-1.1.26-Source-SHA256.txt)
- [Build and reproducibility guide](../docs/BUILD.md)

## Compatibility

- Printer: **ELEGOO Centauri Carbon 2**
- Base: **Community Firmware V4.1**
- Previous integrated CC2 Control: **1.1.25**
- Updated CC2 Control: **1.1.26**

This is a CC2 Control hotfix, not a new Community Firmware release.
