# ELEGOO-Web v4.4.0

Portable Windows controller for the Centauri Carbon 2 local web interface.

## New in v4.4.0

- Complete Italian and English localization of the desktop interface.
- Immediate desktop-language switching from the language selector.
- Localized buttons, labels, live status messages, discovery results, network
  errors, QR messages and configuration warnings.
- The selected language is stored in the existing `config.json` and is also
  used by the printer Web UI after **SAVE AND START / SALVA E AVVIA**.
- Automatic serial-number discovery from v4.3.0 remains unchanged.

## Installation and use

1. Stop the previous ELEGOO-Web server and close the previous app.
2. Extract the complete archive into a new writable folder.
3. Start `ElegooWeb.exe`; normal use does not require compilation.
4. Choose **Italiano** or **English**.
5. Enter the printer IP and Access Code, then select **DETECT PRINTER / RILEVA STAMPANTE**.
6. Select **SAVE AND START / SALVA E AVVIA**.
7. Use **OPEN WEB UI / APRI INTERFACCIA WEB** or the QR code from another device on the same LAN.

Manual serial-number entry remains available as a fallback. Never distribute
`config.json`, because it stores the printer Access Code in plain text.

## Requirements

- Windows 10/11 x64 with .NET Framework 4.8.
- PC and printer on the same reachable network.
- TCP port 8888 free on the PC.

The portable package includes the compiled GUI and the complete C# source.
No Python, WSL, PowerShell or administrator privileges are required at runtime.
