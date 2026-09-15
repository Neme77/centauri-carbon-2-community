# ELEGOO-Web v4.1.1 Portable

ELEGOO-Web is a small Windows launcher for the Centauri Carbon 2 local web interface. It packages the connection settings and local server controls into a simple desktop GUI.

## Connection model

The application uses the **local network**:

1. The Windows PC connects to the printer by IPv4 address.
2. A local HTTP server listens on port `8888` on the PC.
3. The browser opens the web UI served by that PC.
4. A phone or tablet on the same LAN can open the displayed address or scan the QR code.

**It does not communicate with the printer over USB.** USB storage is used only when installing firmware from the printer touchscreen.

The PC running ELEGOO-Web must remain powered on while other devices use its local server.

## Requirements

- Windows 10 or Windows 11 x64.
- Windows PowerShell 5.1.
- The .NET Framework components included with Windows.
- PC and printer connected to reachable local networks.

Python, WSL, OrcaSlicer, and additional installers are not required by the portable build.

## Configuration

The GUI requests:

- printer IPv4 address;
- username, normally `elegoo`;
- printer Access Code;
- printer serial number;
- Italian or English web-interface language.

Settings are stored in `config.json` beside the program. The Access Code is stored as plain text, so never publish, share, or commit that file.

## Normal use

1. Extract the complete portable archive to a new folder.
2. Start `ElegooWeb.exe`.
3. Enter the printer details and select **Save and start**.
4. Use **Open Web UI** to reopen the browser later.
5. Use the mobile link or QR code only from a device that can reach the PC over the same LAN.
6. Use **Stop server** before closing the application when the local service is no longer needed.

Closing only the GUI can leave the server available for mobile devices.

## Changes in v4.1.1

- Network probes run outside the GUI thread.
- QR generation and server restart no longer block the window.
- Stop targets only the server executable in the current application folder.
- A clear warning is shown when port 8888 is already in use.
- Mobile address selection prefers the PC route used to reach the printer.
- Configuration writes use a temporary file and atomic replacement.
- Required fields are validated and save failures are reported.
- The project icon is embedded in the launcher and displayed in the window.

The maintainer tested the final v4.1.1 portable package and reported that it operates correctly.

## Status indicators

- **Server active** means the server belonging to the current application folder is running and its local HTTP endpoint responds.
- **Port 9001 OK** means a TCP connection to the printer succeeded. It does not validate the Access Code or serial number.
- Status checks refresh approximately every five seconds.

VPNs, guest Wi-Fi isolation, firewall rules, or unusual routing can prevent a phone from reaching the PC even when the printer itself is reachable.

## Source and binary package

The `source/` directory contains the original PowerShell GUI, asynchronous network helper, launcher, and diagnostic entry point developed for this project.

The public source tree does not include `ElegooWebServer.exe`, `QRHelper.exe`, the vendor-derived `index.html`, or the packaged executables. Those components are kept outside Git and must be distributed only when their respective licensing and redistribution terms permit it.

## Security notes

- The local HTTP server adds no authentication layer of its own.
- Anyone who can reach the service may be able to open the interface.
- Keep port 8888 limited to trusted local networks.
- Do not forward port 8888 directly to the Internet.
- Do not include `config.json` in screenshots, archives, or bug reports.

