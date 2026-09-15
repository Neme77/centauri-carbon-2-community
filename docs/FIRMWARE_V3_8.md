# Centauri Carbon 2 firmware v3.8

## Scope

Version 3.8 targets only the **ELEGOO Centauri Carbon 2** running the verified **02.01.00.00** firmware base. Compatibility with the Centauri Carbon 1, other firmware versions, or every hardware revision has not been established.

## What changed in v3.8

Version 3.8 retains all v3.7 modifications:

- root SSH service;
- GUI Z-offset patch;
- Dual Trust v2;
- local HTTP in WAN/cloud mode;
- HTTP upload v1, including upload during an active print when allowed by the client.

It adds the final local-connectivity work:

- MQTT/local status remains available in WAN/cloud mode;
- OrcaSlicer receives live temperatures without a page refresh;
- Matrix/WAN operation continues at the same time;
- the local webcam is available in WAN/cloud mode;
- local and Matrix video were tested simultaneously;
- WAN -> LAN -> WAN switching was tested;
- the local service on port 8080 remains available in WAN mode.

The proposed increase from four to five simultaneous video clients was deliberately not included. The original four-client limit remains unchanged to avoid unnecessary load on the printer hardware.

## Validation status

The modified printer component was tested on real hardware. The maintainer also completed a stock-signed v3.8 build and reported successful installation and operation.

The firmware was tested after restoring official firmware on both update partitions and then installing the modified build again. The verified functions continued to operate.

See [TESTING.md](TESTING.md) for the exact validation boundary.

## Client behavior

OrcaSlicer can use the restored local services in LAN-only and WAN/cloud modes. ELEGOO Slicer may still refuse an upload while the printer is busy because that restriction is enforced by the client application.

Uploading a file during printing does not queue or automatically start another print. Use **Upload only**.

## External binary reference

The builder validates the tested `elegoo_printer` reference by size and hashes:

- Size: `18,099,452` bytes
- MD5: `06071f7b3ca6f809d5a329e1a9466f4c`
- SHA-256: `c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3`

That binary is not included in the public source repository. The builder rejects a different, truncated, extended, or corrupted reference.

