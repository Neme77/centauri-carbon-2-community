# ELEGOO-Web v4.2.0 Portable

A simple Windows desktop controller for the Centauri Carbon 2 local web interface.
The maintainer reports successful compilation and operation on Windows. This
package includes that executable unchanged; users do not need to compile it.
The window title still includes "Test" because this is the exact tested build.

## Requirements

- Windows 10/11 x64 and .NET Framework 4.8.
- PC and printer reachable on the local network; TCP port 8888 free on the PC.
- Printer IP address, username (normally elegoo), Access Code and serial number.
- For mobile access, allow the server through the firewall on your trusted LAN.

No Python, WSL, PowerShell, OrcaSlicer or administrator rights are needed to run
this application. Network access is over LAN, not USB.

## Start

1. Stop the server in the previous app and close its window.
2. Extract this entire archive into a new writable folder.
3. Optionally copy only config.json from the old folder. Never share this file:
   it contains your printer Access Code in plain text.
4. Run ElegooWeb.exe. Enter the printer details and select SALVA E AVVIA
   (Save and start). Italiano/English selects the web-interface language;
   desktop controls are in Italian. Serial-number entry is still manual.
5. APRI WEB UI opens the local page. The original server opens the browser
   itself on its first start.
6. Use the displayed URL or QR code from your phone/tablet on the same LAN.
7. ARRESTA SERVER stops this copy's server. Closing only the window leaves
   the server running; keep the PC powered on for mobile access.

## What's new

- Direct C# Windows Forms GUI replaces the Go/VBS/PowerShell launcher chain.
- No ExecutionPolicy Bypass and no source compilation when the app starts.
- Asynchronous network probes, server stop waits and QR generation.
- Server stop identifies the executable by its full path.
- Compatible config.json keys and atomic configuration writes.
- Configuration fields remain visible. Application and window icon included.

PORTA 9001 OK confirms only TCP reachability, not successful authentication.
The mobile link depends on LAN routing; VPNs and guest Wi-Fi isolation can affect it.

## Files and optional rebuilding

Keep ElegooWeb.exe, ElegooWebServer.exe, index.html, QRHelper.exe and favicon.ico
together. config.json is created when you save. BUILD.cmd and source/ are optional
for normal use. To rebuild, run BUILD.cmd on Windows with .NET Framework installed.
The C# 5 compatibility fix moves await outside finally.

The server, QR helper, web page and icon are unchanged from v4.1.1.
Only the desktop GUI was rewritten; this is not a new printer firmware.
Original project source is GPL-3.0-only; third-party terms continue to apply.

## Validation and security

The supplied executable is unsigned. The maintainer reported five antivirus
flags in a multi-engine scan and successful acceptance by Google Drive. These
reports are not a security certification, and detection results may change.
Do not disable protection or add exclusions to resolve a detection; report the
exact file hash and detection name for review. The earlier Go launcher is absent.

The packaging checks verify ZIP integrity, checksums, no personal configuration,
and byte identity of the supplied executable and retained components. No new
Windows runtime test or malware scan was performed during packaging.
The local server adds no authentication layer: use trusted networks and do not
forward port 8888 directly to the Internet.

## Repository layout

The current C# sources are in `source-v4.2/`. The older `source/` directory is retained as historical v4.1.1 material and is not used by v4.2.0. For the complete rebuild inputs, use the portable package's source folder and BUILD.cmd.
