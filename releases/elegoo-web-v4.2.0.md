# ELEGOO-Web v4.2.0 Portable for Windows

A direct C# desktop interface for local Centauri Carbon 2 access, tested by the maintainer.

**Publication status: asset upload pending.**

## Download and use

Download **ELEGOO-Web-v4.2.0-Portable.zip** from this release's Assets and verify it against **SHA256SUMS.txt**. Extract the whole archive and run **ElegooWeb.exe**. Compilation is not required.

Requires Windows 10/11 x64, .NET Framework 4.8 and local network access to the printer. Stop the previous server first. Copy only your private config.json if you want to preserve settings.

## Changes

- Replaces the Go/VBS/PowerShell launcher chain with a direct C# Windows Forms GUI.
- Preserves server controls, printer settings, selectable web language and mobile QR access.
- Network checks and server/QR waits run outside the GUI thread.
- Includes full C# source and optional BUILD.cmd; no runtime compilation.
- The server and QR helper are unchanged. Automatic serial discovery is not included.

The executable is the exact maintainer-supplied build; its window title still contains "Test". It is unsigned. Some engines flagged it in the maintainer's scan; Google Drive accepted it. This is not a malware-free certification. Report detections with the SHA-256 rather than disabling protection.

Closing the window leaves the server available to mobile devices. Keep the PC on, restrict access to a trusted LAN, and never share config.json.

See [full documentation](https://github.com/Neme77/centauri-carbon-2-community/blob/main/elegoo-web/README.md).
