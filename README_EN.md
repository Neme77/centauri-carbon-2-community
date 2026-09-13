# Centauri Carbon 2 Community

Independent project maintained by Neme77; not affiliated with or supported by ELEGOO.

Tested firmware base: **02.01.00.00**, `cc2_eeb001_02.01.00.00_20260707170825.zip.sig`. Compatibility with other models, versions and hardware revisions has not been established.

Features: restored root SSH access, GUI Z-offset patch, Dual Trust v2 for stock/community signatures, local HTTP on port 80 in WAN mode, and upload v1 while printing.

The maintainer reports successful stock-to-custom installation, restoration of stock firmware and a community-signed update. OrcaSlicer in LAN mode successfully uploaded a file during printing. Elegoo Slicer may block uploads on the client side. Select upload only; automatic job queuing is not provided.

## Firmware download

The planned **[v3.7 release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/v3.7)** provides one asset: `CC2_FULL_V3_7_STOCK_BOOTSTRAP.zip.sig`. The link becomes available to readers once the release is published.

This is **modified, stock-signed firmware that already includes Dual Trust v2**. It was tested installing from stock 02.01.00.00 and can also be verified by our Dual Trust. No community-signed asset is distributed in this release.

SHA-256:

```text
149be825757593323aeca8ffd8e8bb3a86354dbedfab7c14e60298b3bb942f7f
```

The repository holds sources and documentation; firmware belongs in Releases. Building requires external components. End users do not need private keys to install a signed release.

Private signing keys, the AES key and credentials are excluded. Community updates require the matching Dual Trust firmware to be running. Installing official firmware removes the custom features in the updated system.

[USB installation (Italian)](docs/INSTALLAZIONE.md) · [Test evidence](docs/TEST.md)

Original project code contributions are distributed under **GNU GPL version 3**, subject to the scope in [NOTICE.md](NOTICE.md); see [LICENSE](LICENSE). Existing third-party licenses and notices remain applicable. This does not relicense the complete ELEGOO firmware image.

[OpenCentauri Firmware Tools](https://github.com/OpenCentauri/cc-fw-tools) is the organizational reference for this publication. This CC2 project is independent; no affiliation, code derivation or CC1/CC2 firmware compatibility is implied.
