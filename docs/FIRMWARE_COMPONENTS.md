# Integrated firmware components

This document tracks the development integration for the next complete CC2 firmware. It does not announce a release or authorize installation of an unqualified OTA.

The builder keeps the exact qualified stock kernel. The reviewed RTL8153 modules are supplied in the firmware filesystem, with user opt-in and existing USB/ABI checks before loading. Native Wi-Fi remains the default on a fresh configuration.

The Connections runtime is seeded on persistent storage before native printer, camera and display processes start. Known retained components and DHCP entries can be migrated with backups; saved connection preferences are retained. Unknown or damaged inputs are refused. No bootstrap step loads a module, commands a heater or motion, stops a service or restarts a native process.

The native display executable and its resources are integrated together. The original printer startup script remains responsible for synchronizing resources before starting the GUI. Original printer and Canvas artwork, callbacks and language behaviour are retained.

The cumulative application also keeps the Obsidian background continuous on short Appearance and About pages. The same application change is intended for both complete firmware and the separate CC2 Control updater.

Validation must distinguish automated shell/filesystem checks, complete signed-OTA assembly and testing on a real printer. A stable release or catalogue change follows hardware acceptance, not fixture results alone.

See [the current builder](../builder/current/README.md), [build instructions](BUILD.md) and [source provenance](SOURCE.md). Restricted firmware inputs and signing material remain local and must not be committed.
