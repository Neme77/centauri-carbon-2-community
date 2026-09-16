# ELEGOO-Web v4.4.0

ELEGOO-Web v4.4.0 is the stable bilingual release of the portable Windows controller for the ELEGOO Centauri Carbon 2 local web interface.

## New in v4.4.0

- Complete Italian and English localization of the desktop interface.
- Immediate language switching without restarting the application.
- Localized buttons, labels, status messages, printer discovery results, network errors, QR messages and configuration warnings.
- The selected language is saved in `config.json` and is also used by the printer Web UI.
- Automatic serial-number, hostname and model detection introduced in v4.3.0 is retained.
- Manual serial-number entry remains available as a fallback.

## Requirements

- Windows 10/11 x64 with .NET Framework 4.8.
- PC and printer reachable on the same local network.
- TCP port 8888 available on the PC.

Extract the complete ZIP and run `ElegooWeb.exe`. No Python, WSL, PowerShell, administrator privileges or compilation are required for normal use.

Do not distribute `config.json`: it contains the printer Access Code in plain text.

SHA-256: `704ea08d745ecb16d193852382037a01f6f813b32a3e9422030d2b8fa8097d2f`
