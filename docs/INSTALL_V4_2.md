# Installing Community Firmware V4.2

## Before installation

This firmware is for the **ELEGOO Centauri Carbon 2**.

Verify the firmware archive checksum before copying it to USB.

Firmware:

```text
CC2_V4_2_STOCK_20260927_151944_1a06ebe1.zip.sig
```

SHA-256:

```text
1e9d7eacf3a8f55b1e019d41af3def59fd2a9a6e1eb39090ee6ae2ac05a4797a
```

## Installation

1. Copy the firmware package to the USB drive as required by the normal Centauri Carbon 2 local-update process.
2. Start the local firmware update.
3. Wait for the update to complete fully.
4. Allow the printer to reach its normal post-update state.
5. Wait for the touchscreen and printer services to finish initialising.

## No additional restart required

> [!IMPORTANT]
> V4.2 with CC2 Control 1.1.30 no longer requires an additional manual
> reboot or power-off/power-on cycle after the normal firmware update completes.

First-run LAN-code registration restarts only CC2 Control and automatically
reconnects MQTT, Canvas and snapshot state. Keep the printer powered and allow
the page up to roughly 30 seconds to reconnect; refresh it once if requested.

## Verify CC2 Control

Open:

```text
http://PRINTER-IP:8081
```

The interface should report CC2 Control:

```text
1.1.30
```

You can also verify locally over SSH:

```sh
wget -qO- http://127.0.0.1:8081/api/health
```

## If synchronisation takes longer than expected

Wait approximately 30 seconds and refresh the CC2 Control page. A full printer
restart is not part of the normal V4.2 installation procedure.

## Existing V4.1 users

If you do not want to reflash the whole firmware, CC2 Control 1.1.30 can also
be installed using the multiplatform package supplied with the release.
