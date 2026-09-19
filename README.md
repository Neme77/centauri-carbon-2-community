# Centauri Carbon 2 Community


Independent community project maintained by **Neme77** for the ELEGOO Centauri Carbon 2.


> This project is not affiliated with, endorsed by, or supported by ELEGOO.


## Community Firmware v3.9


Firmware v3.9 integrates **CC2 Control 1.1.16**, a lightweight on-printer web
dashboard available at `http://PRINTER-IP:8081`.


![CC2 Control](docs/images/v3.9/dashboard-overview.jpg)


Highlights include live telemetry and camera, protected printer controls,
persistent custom material presets, a protected Klipper console, 11×11 Bed Mesh
2D/3D visualisation, native four-slot ELEGOO Canvas support, and protected printing from internal memory or USB—including files in nested folders. Clean installs
are configured from the browser; existing credentials and presets survive A/B
firmware updates under `/opt/usr`.


> **Important:** after completing the first-run browser configuration on a clean
> installation, perform one complete printer reboot. This allows Canvas to
> synchronize correctly with both CC2 Control and the original printer interface.


See [CC2 Control documentation](docs/CC2_CONTROL.md), the
[v3.9 installation guide](docs/INSTALL_V3_9.md) and the
[v3.9 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V3.9).


## Downloads


| **FIRMWARE v3.9** | **ELEGOO-WEB v4.4.0** |
| :---: | :---: |
| [![Download firmware v3.9](https://img.shields.io/badge/DOWNLOAD-FIRMWARE_v3.9-1769aa?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.9/CC2_V3_9_STOCK_20260919_124557_53d24c5b.zip.sig) | [![Download ELEGOO-Web v4.4.0](https://img.shields.io/badge/DOWNLOAD-ELEGOO--WEB_v4.4.0-238636?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/elegoo-web-v4.4.0/ELEGOO-Web-v4.4.0-Portable.zip) |
| **[Download firmware (.zip.sig)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.9/CC2_V3_9_STOCK_20260919_124557_53d24c5b.zip.sig)** | **[Download portable app (.zip)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/elegoo-web-v4.4.0/ELEGOO-Web-v4.4.0-Portable.zip)** |
| Centauri Carbon 2 only · Base 02.01.00.00 · 124.13 MiB · CC2 Control 1.1.16 | Windows 10/11 x64 · .NET Framework 4.8 · 5.1 MiB |
| [Release notes](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V3.9) · [Installation](docs/INSTALL_V3_9.md) · [SHA-256](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.9/CC2_V3_9_STOCK_20260919_124557_53d24c5b.zip.sig.sha256.txt) | [Release notes](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/elegoo-web-v4.4.0) · [Setup](elegoo-web/README.md) · SHA-256: `704ea08d745ecb16d193852382037a01f6f813b32a3e9422030d2b8fa8097d2f` |


**Firmware:** copy the downloaded `.zip.sig` unchanged to USB storage; do not extract it.
**App:** extract the entire ZIP and run `ElegooWeb.exe`; no compilation required.


Previous firmware: [v3.8 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/v3.8).


### Builder and source packages


[![Download CC2 Builder v3.9](https://img.shields.io/badge/DOWNLOAD-CC2_BUILDER_v3.9-8250df?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.9/CC2_BUILDER_V3_9_UPDATE_v1.1.16.zip)


[![Download CC2 Control source](https://img.shields.io/badge/DOWNLOAD-CC2_CONTROL_1.1.16_SOURCE-2ea44f?style=for-the-badge)](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.9/CC2-Control-v1.1.16-Source.zip)


- **CC2 Builder v3.9:** `CC2_BUILDER_V3_9_UPDATE_v1.1.16.zip`  
