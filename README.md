# Centauri Carbon 2 Community Firmware

A complete community firmware and local control experience for the **ELEGOO
Centauri Carbon 2**, maintained by **Neme77** with contributions and printer
testing from the community.

[Discord](https://discord.gg/FGfCaFpm38) ·
[Facebook group](https://www.facebook.com/groups/1658610779139107) ·
[Report a problem](https://github.com/Neme77/centauri-carbon-2-community/issues) ·
[Contributors](docs/CONTRIBUTORS.md)

## Community Firmware 4.4.4 · ELEGOO base 02.01.01.13 · CC2 Control 1.2.29

**A new base on ELEGOO 02.01.01.13, with the Community touchscreen, local web
dashboard and native integrations brought forward together.** This release
combines the .13 port with the recent print, calibration, camera, network and
interface work. The final firmware is the verified RC5: only five translated
locale files differ from the physically accepted RC4. Backend and native
components remain identical.

**First-ever Community installation from original ELEGOO firmware? Do not run
the probe or Prepare-Upgrade. Follow the stock-firmware offline installation
route in the guide.** Reflashing stock on a previously modified printer does
not make it a first installation: persistent Community files may remain.

**Upgrading from Community 4.4.3? Restore modified Canvas firmware first, save
your backups, and complete the supplied preparation before flashing. Do not
skip these steps.** You do not need an intermediate 1.2.27 or 1.2.28 panel
installation. Private previews and unknown bases need a reviewed route.

**With modified Canvas, finish both the Canvas firmware restoration and the
printer startup restoration, including the requested reboots, before running
preparation. Do not flash the signed firmware file first.**

| Download | What it contains |
| --- | --- |
| [4.4.4 upgrade kit](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2-4.4.4-Upgrade-Kit.zip) | Signed complete firmware, mandatory instructions, PC backup, native-console inspection and verified quarantine tools. Start here. |
| [Release notes and SHA-256 checksums](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4) | Changes, qualification scope, sources and exact download identities. |
| [Installation guide](docs/FIRMWARE_UPGRADE.md) | Choose the correct route and follow every step in order. |

This is a **complete firmware upgrade**, not an application-only package for
the old base. The existing panel updater remains a separate mechanism for
compatible application releases. Historical downloads stay in
[GitHub Releases](https://github.com/Neme77/centauri-carbon-2-community/releases).

## What is new on the .13 base

- **Native touchscreen:** Community Home, Network, colour palette and Z-offset
  integration, retaining the original printer controls.
- **Camera and connectivity:** ported Camera Recovery and native LAN/WAN
  compatibility; local video, Matrix, ELEGOO Slicer and OrcaSlicer received
  printer trials. Optional qualified RTL8153 USB Ethernet works alongside Wi-Fi.
- **Native features:** Reactor, EXHAUST minimum, flap controls and extended
  Canvas pullback are integrated into the new base. The original ELEGOO pullback
  default is retained until the operator changes it; the supported range now
  extends to 1000 mm.
- **First-layer dashboard:** effective Z offset, session-only indication, active
  mesh and plate side in one card. Heater indicators show whether heating is
  active, rather than inferring it from measured temperature.
- **Print preparation:** a print queue, live G-code layer paths, interactive
  screw tramming and history, and responsive background USB file import.
- **Maintenance:** update progress and installation logs, clearer Canvas
  restart validation, revised drying shutdown recovery and Quiet motors fixes.
- **Readability and languages:** improved themes and eleven interface languages,
  with German, Turkish and recent Hungarian/Chinese refinements included.

The upgrade preparation reads supported state instead of assuming that every
printer with the same panel version has the same native installation. It uses
the **native console** for installation readiness, without waiting for MQTT or
UDS freshness. Unknown or incomplete recovery state is refused, not erased.

## Open, extensible RFID — with validation still to do

Use the Canvas reader beyond the original producer filter:
**scan → review material, colour and temperatures → choose a slot → confirm**.
Material assignment and physical filament presence remain separate.

Activation is **optional and requested from Settings → Integrations**. Follow
the restart and validation prompts; the original Canvas firmware can be
restored. A Centauri Carbon 2 without Canvas needs no RFID activation, and
external-spool printing remains available.

The decoder and integration are open to new layouts and implementations.
Tested tag families do **not** establish universal compatibility with every
brand. Reports and documented tag-layout contributions will help expand that
coverage. Please include reproducible evidence; avoid publishing personal tag
data or credentials. Decoder-layout attribution remains with
**OpenRFID / DnG-Crafts**.

## The CC2 Control experience

| Area | Available workflows |
| --- | --- |
| Dashboard and printing | Printer/job state, progress, temperatures, fans, live Z offset, camera, quick actions and emergency stop. |
| Files and slicers | Internal/USB G-code management, previews, upload, print setup, calibrated starts, print history and OrcaSlicer integration. |
| Queue | Prepare jobs and copies in advance. The normal continuation waits for the operator; automatic continuation is an explicit choice for an appropriate self-clearing-bed workflow. |
| Job map | Objects, exclusion and a thinned extrusion path for the selected layer. Enable object labelling in OrcaSlicer to access the current expanded map; path parsing itself does not require labels. |
| Plates and calibration | Build-plate library, Side A/B, stored meshes, temperature/nozzle-aware workflows, screw levelling/tramming, PID, Pressure Advance, Input Shaper and advanced calibration tools. |
| Filament | Canvas slots, external spool/TPU, local spool inventory, Spoolman connection, material profiles and reviewed RFID assignment where supported. |
| Thermal and fans | Nozzle/bed targets, chamber heating and filament drying, native fans, optional EXHAUST minimum and flap controls. Accessory integrations depend on the connected hardware. |
| Connections | Wi-Fi profiles, DHCP/static configuration and qualified USB Ethernet with confirmation/recovery. Unsupported adapters are not silently enabled. |
| Settings and upkeep | Themes and eleven languages, LAN-code configuration, selective app backups, updates with progress/logs, integrations and diagnostics. |

Features and controls depend on the hardware and current printer state. The
presence of an integration does not mean every accessory or combination has
been physically qualified. See the [changelog](CHANGELOG.md),
[testing guide](docs/TESTING.md) and [API documentation](cc2-control/docs/API.md).

## Tested work, stated limits

The .13 cycle included repeated printer installations, a stock-firmware
bootstrap, 4.4.3 preparation and migration, exact native file/module inspection,
and an active-RFID helper upgrade on its qualified predecessor. The owner
confirmed successful RFID reads and the core printing/network/camera workflows.
Independent testing and issue reports also informed the recovery work.

The revised preparation passed **84 evidence-inspector checks** on host, ARM
and sanitizers and **36 shell checks**, including the original ARM BusyBox.
Offline fixtures are kept distinct from actual printer results. This does not
claim every historical installation, tag brand or accessory was tested.

## Clearer updates from this base onward

Compatible **panel features** continue through the normal application-update
path, with their supported base declared. **Firmware/native changes with
uncertain compatibility** become a new complete firmware base with a documented
migration. This separation is intended to make future updates simpler and
their requirements easier to understand.

Contributors must document and test their own PRs using the reference
[contribution guidelines](CONTRIBUTING.md), [working rules](AGENTS.md),
[testing guide](docs/TESTING.md) and [release policy](RELEASING.md). CI supports
that evidence; it does not replace hardware testing or author responsibility.

## Built with the community

Thanks to **Surfoo**, **beanbo**, **Noahwhite2014**, **lpgamerzockt-ops**,
**pezso92**, **Skycos**, the translators, reviewers and everyone who contributed
ideas, logs and printer time. Special thanks to the people who reproduced the
upgrade failures and helped verify the recovery paths. Firmware integration
and the extended physical qualification were carried out by **Neme77**.
Original authorship and the wider project history remain in the
[full credits](docs/CONTRIBUTORS.md) and [changelog](CHANGELOG.md).

Special thanks to the **#327** testers and investigators: **Eldurinn**,
**kalross**, **jgj1107**, **matefon**, **SVLoneStar**, **fechy**,
**RoisinDubh79**, **lpgamerzockt-ops** and **Noahwhite2014**. Their different
installation states, trial reports and recovery confirmations made the upgrade
work more complete. [Individual contributions and report scope](docs/UPGRADE_CREDITS.md).

## Buy me a coffee

If you enjoy my work and would like to buy me a coffee, your optional support
helps me keep developing, testing and exploring support for additional printer
models. The project remains open; a contribution is not required to use it and
does not promise support for an untested printer.

[Support the project with PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=gino.nencioni%40gmail.com&item_name=CC2%20Community%20Firmware&currency_code=EUR)

## Sources and notices

[Source/build information](docs/SOURCE.md) · [License](LICENSE) ·
[Third-party notices](NOTICE.md) · [Release process](RELEASING.md)

Community code and vendor firmware have distinct licensing scopes. Obtain
required vendor/build inputs separately; private signing material and printer
credentials are not included in the source repository.
