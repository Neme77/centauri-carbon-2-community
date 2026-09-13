# Centauri Carbon 2 Community

Independent project maintained by Neme77; not affiliated with or supported by ELEGOO.

Tested firmware base: **02.01.00.00**, `cc2_eeb001_02.01.00.00_20260707170825.zip.sig`. Compatibility with other models, versions and hardware revisions has not been established.

Features: restored root SSH access, GUI Z-offset patch, Dual Trust v2 for stock/community signatures, local HTTP on port 80 in WAN mode, and upload v1 while printing.

The maintainer reports successful stock-to-custom installation, restoration of stock firmware and a community-signed update. OrcaSlicer in LAN mode successfully uploaded a file during printing. Elegoo Slicer may block uploads on the client side. Select upload only; automatic job queuing is not provided.

This repository preparation includes sources and documentation, **not downloadable firmware**. Tested firmware assets and their checksums are pending. The builder also needs external components; see [build requirements](docs/BUILD.md).

Private signing keys, the AES key and credentials are excluded. Community updates require the matching Dual Trust firmware to be running. Installing official firmware removes the custom features in the updated system.

[USB installation (Italian)](docs/INSTALLAZIONE.md) · [Test evidence](docs/TEST.md)

A project license remains to be selected by the maintainer. No rights to redistribute third-party firmware or components are asserted by this draft.
