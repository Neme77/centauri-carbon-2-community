# Firmware v3.8 installation

## Read this first

This firmware is intended only for the **ELEGOO Centauri Carbon 2** with firmware base **02.01.00.00**. Installing modified firmware carries a real risk of an incomplete boot or an unusable printer.

Before installing:

1. Keep a verified copy of the official ELEGOO recovery firmware.
2. Record the printer's network settings and access code.
3. Do not interrupt power during an update.
4. Do not use a package whose checksum does not match the published release checksum.

## USB installation media

The firmware is installed from a USB storage device, not through the ELEGOO-Web application.

1. Format a reliable USB drive as FAT32.
2. Copy only the verified release package and its required installation files to the root of the drive.
3. Safely eject the drive from the computer.
4. Insert it into the printer and start the local update procedure from the touchscreen.
5. Leave the printer powered until the update and restart are complete.
6. Remove the drive after the printer has restarted.

Exact filenames and checksums must be taken from the matching GitHub Release. Do not rename or mix files from different releases.

## After installation

Verify at minimum:

- normal boot and touchscreen operation;
- SSH access, if required;
- LAN-only mode and WAN/cloud mode;
- Matrix connection;
- OrcaSlicer status and live temperatures;
- local webcam and Matrix video;
- upload-only behavior during a running print.

## Returning to official firmware

Installing official firmware removes the custom functions from the updated system. Do not assume that SSH, Dual Trust, or other modifications survive a stock restoration.

Always use an official package matching the printer model and follow ELEGOO's recovery instructions.

