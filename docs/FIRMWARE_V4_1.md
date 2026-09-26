# Centauri Carbon 2 Community Firmware V4.1

Community Firmware **V4.1** integrates **CC2 Control 1.1.25** into the ELEGOO Centauri Carbon 2 firmware while keeping the stock `elegoo_printer` binary unchanged.

## Important post-install step

> [!IMPORTANT]
> After installation has completed and the printer has booted normally, **reboot the printer once more** before normal use.

The additional reboot lets CC2 Control, Canvas discovery/state, MQTT registration and the compatibility services initialize against a fully settled printer runtime. V4.1 was validated with two complete firmware installation cycles using this procedure.

## CC2 Control 1.1.25

The integrated web interface is available at:

```text
http://PRINTER-IP:8081
```

The same interface can be loaded in the **OrcaSlicer Device tab**, so the main CC2 Control workflow is available without leaving the slicer.

### Dashboard and controls

- live camera
- printer status and progress
- nozzle and bed temperatures
- fan controls
- thermal history
- position and motion controls
- speed and flow controls
- light, heaters and emergency actions
- memory, uptime, system load and service state

Protected controls reject actions that are unsafe for the current printer state.

### OrcaSlicer integration

V4.1 improves the workflow between OrcaSlicer and CC2 Control:

- CC2 Control can be used directly from the OrcaSlicer Device view
- upload from OrcaSlicer is supported
- upload-and-print is supported
- Canvas spool selection can appear before print start
- the Canvas popup can appear directly inside the Device tab without requiring a manual refresh

### G-code file manager

The file manager supports internal storage and USB folders, including nested directories and thumbnails where available. A selected file can be validated, imported when required, and started using the printer's native print-start workflow.

### Canvas

CC2 Control exposes the four Canvas slots, including:

- Canvas detection
- load and unload workflows
- material editing
- built-in and custom material presets
- spool/material mapping
- multicolour spool-selection popup before print

Persistent material presets are stored under `/opt/usr/cc2-control`.

### Bed Mesh and screw tramming

V4.1 retains the full 11×11 Bed Mesh visualization in 2D and 3D.

Four-screw load-cell tramming is kept isolated from the stored Bed Mesh. Screw measurements therefore cannot replace a valid mesh with a four-point result.

### Object Exclusion

For labelled multi-object G-code, CC2 Control can issue Klipper `EXCLUDE_OBJECT` for a selected object after explicit confirmation.

### Protected expert console

The expert console uses an explicit unlock confirmation and remains unavailable while printing.

Safety rules include:

- `G28` is allowed before homing
- `G0/G1` movement is rejected until X/Y/Z are homed
- tracked `G90/G91` positioning mode
- configured motion bounds checks
- dangerous low-level movement/stepper commands blocked by the backend
- Klipper remains the final authority for command execution

### Panda / Moonraker-like compatibility

CC2 Control exposes a compatibility service on TCP port **7125** for integrations such as BTT Panda Breath.

Validated endpoints include:

```text
/server/info
/printer/info
/printer/objects/list
/printer/objects/query
/websocket
```

The service is intentionally limited compared with a full Moonraker installation.

### Discovery API

V4.1 adds a stable system/capability discovery API so external projects can detect Community Firmware and CC2 Control without relying on UI scraping or implementation details.

The API identifies the platform, firmware/control versions, service ports and supported capabilities without exposing credentials.

## Runtime layout

Firmware payload:

```text
/opt/inst/cc2-control
```

Persistent data:

```text
/opt/usr/cc2-control
```

Main services:

```text
8081  CC2 Control web/API
7125  Panda / Moonraker-like compatibility
```

## Firmware identity

Firmware:

```text
CC2_V4_1_STOCK_20260926_015216_446f1846.zip.sig
```

SHA-256:

```text
868814647d3835c193f0f0625e9037545a5a5128ee504ce74c62fbc4c4f41e19
```

Signing mode: **stock**

Base firmware: **ELEGOO 02.01.00.00**

CC2 Control: **1.1.25**
