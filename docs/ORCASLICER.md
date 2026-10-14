# OrcaSlicer setup and Canvas filament sync

[Italiano](ORCASLICER.it.md) · [First installation](FIRST_RUN.md) · [Homepage](../README.md)

This guide uses the **CC2 Control connection** on Community Firmware V4.4 / CC2 Control 1.2.3. Complete [first setup](FIRST_RUN.md) and check **Printer connected** before configuring OrcaSlicer.

## 1. Configure the printer connection

Use your working **Centauri Carbon 2** printer profile with the correct nozzle and Canvas configuration. Open its physical-printer / network connection settings. Field positions and translations vary between OrcaSlicer versions; use a version that provides the **Moonraker printer agent** for filament synchronization.

Example printer address: `192.168.1.50`. Replace it with your printer's actual IP.

| Field | Value for CC2 Control |
| --- | --- |
| Host Type | **Octo/Klipper** |
| Hostname, IP or URL | `http://192.168.1.50:8081` |
| Device UI | `http://192.168.1.50:8081` |
| Printer Agent | **Moonraker**, where your OrcaSlicer version exposes this setting |
| API Key / Password | Leave empty for the CC2 Control endpoint; its printer LAN credential was entered in CC2 Control setup. |

Save the connection, select **Test**, and open the **Device** tab. You should see CC2 Control with the same printer information as in a browser.

**Use port 8081 for upload, Device UI and Canvas sync.** Port **7125** is the separate read-only Panda Breath compatibility bridge; it does not serve Canvas filament data or G-code uploads. The native ELEGOO Link connection is a different route and is not the configuration described here.

## 2. Synchronize the Canvas filaments

1. On the printer or the CC2 Control **Canvas** page, check the four slots' saved material and colour information.
2. In OrcaSlicer, select this configured printer and use the **Sync / Synchronize filaments** control next to the filament list.
3. If OrcaSlicer offers a source or agent choice, use **Moonraker**. If the option is unavailable, check the installed OrcaSlicer version and the printer-agent setting.
4. Compare the imported filament order, material types and colours with the Canvas page before slicing.
5. Select or correct the matching OrcaSlicer filament presets for your actual materials and printer.

CC2 Control supplies the **slot identity, saved material, colour and maximum nozzle temperature**. OrcaSlicer decides which of its filament presets matches that data. Sync does not copy every custom CC2 material-profile setting into an OrcaSlicer preset, and a custom name such as `PLA MATTE` may still need manual preset selection. Bed temperature is not supplied as a usable target by this compatibility response; choose it in your slicer filament preset.

Saved material data and physically loaded filament are different things. An empty slot can retain a saved material until reset: CC2 Control dims empty slots using the printer's status, while Orca's sync uses the saved material fields. Check the real slots and the print mapping rather than treating an imported colour as proof that filament is loaded.

After changing material information on the printer or in CC2 Control, sync again in OrcaSlicer. This is a read from the printer: it does not load filament or rewrite all OrcaSlicer profiles automatically.

## 3. Upload and print

1. Slice with the correct CC2 printer/nozzle profile and the filament presets you checked.
2. Use OrcaSlicer's upload or upload-and-print action.
3. Open the **Device** tab. An upload-and-print is held for CC2 Control's confirmation; check the pending print dialog.
4. Confirm which Canvas slot supplies each sliced filament, the bed side and the other print options. Then start the print.

Sync imports slot information for slicing; the CC2 print dialog confirms the physical slot mapping for that job. Uploading alone does not start a print. If an uploaded filename already exists, CC2 Control stores a numbered copy; verify the file shown in the dialog.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Device page does not open | First try `http://PRINTER-IP:8081` in a browser. Check the IP, port and local network. |
| Test succeeds but sync is empty | Check **Moonraker** agent and port **8081**, then open Canvas and wait for its first snapshot. |
| Agent / sync control is absent | Check your OrcaSlicer version; basic upload support alone does not establish filament-sync support. |
| Materials or colours are old | Compare the Canvas page with the printer display, refresh it and request sync again. |
| A custom material becomes a generic preset | Select the correct OrcaSlicer material preset manually and review its print temperatures. |
| Upload-and-print does not begin immediately | Open Device and confirm the CC2 Control pending-print dialog and slot mapping. |

For advanced, read-only checks, open these URLs in a browser, replacing the IP:

```text
http://192.168.1.50:8081/server/info
http://192.168.1.50:8081/server/database/item?namespace=lane_data
```

The first should report `klippy_connected: true` after setup; the second should contain the Canvas `lane_data` once its snapshot is available. An empty result is not a reason to reset the firmware.

## Sources and acknowledgement

The connection field names and Moonraker synchronization behavior were checked against OrcaSlicer's [configuration definitions](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/src/libslic3r/PrintConfig.cpp) and [Moonraker printer agent](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/src/slic3r/Utils/MoonrakerPrinterAgent.cpp). CC2 endpoint details are in [the project integration guide](CC2_CONTROL.md#orcaslicer-canvas-filament-synchronization) and [API documentation](../cc2-control/docs/API.md).

Special thanks to **[@efiten](https://github.com/efiten)** for the original integration proposal, analysis and printer-side validation.
