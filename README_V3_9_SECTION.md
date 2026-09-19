## Community Firmware v3.9

Firmware v3.9 integrates **CC2 Control 1.1.16**, a lightweight on-printer web dashboard available at `http://PRINTER-IP:8081`.

![CC2 Control](docs/images/v3.9/dashboard-overview.jpg)

Highlights include live telemetry and camera, protected printer controls, persistent custom material presets, a protected Klipper console, 11×11 Bed Mesh 2D/3D visualisation, native four-slot ELEGOO Canvas support and protected printing from internal or USB storage. USB files in the drive root and nested folders are safely imported before printing.

Clean installations are configured from the browser; existing credentials and presets survive A/B firmware updates under `/opt/usr`.

> **Important:** after completing first-run browser configuration, perform one complete printer reboot and wait 30–60 seconds. This allows Canvas to synchronize correctly with both CC2 Control and the original printer interface.

See [CC2 Control documentation](docs/CC2_CONTROL.md), the [v3.9 installation guide](docs/INSTALL_V3_9.md) and the [v3.9 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V3.9).
