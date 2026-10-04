# Licensing scope and third-party notices

The GNU General Public License, version 3 (SPDX: GPL-3.0-only), applies to original code contributions supplied by the Centauri Carbon 2 Community project whose rights can be licensed by their contributors. Existing license notices and third-party terms take precedence for their respective material. No claim of ownership is made over third-party contributions.

This repository-level license does not relicense ELEGOO firmware images, modified vendor executables, embedded third-party software, trademarks or signing keys. Their applicable terms must be preserved. Documentation is not assigned a separate reuse license by this code licensing notice.

The distributed firmware is derived from the ELEGOO Centauri Carbon 2 stock package 02.01.00.00. It includes vendor and third-party components, including an SSH implementation. This preparation does not yet contain a complete component license/source inventory and does not certify compliance with all redistribution requirements. Applicable source-distribution obligations cannot be replaced by this notice.

OpenCentauri/cc-fw-tools is acknowledged as the publication-structure reference, not as the asserted source of our patches. Its repository carries GPL-3.0 and its release notes identify the underlying ELEGOO firmware. No OpenCentauri code was copied as part of this documentation update.

CC2 Control interprets vendor MQTT values (machine sub-states, request `error_code` values and the auto-refill method number) according to ELEGOO's elegoo-link SDK, licensed under the Apache License 2.0: <https://github.com/elegooofficial/elegoo-link>, file `src/lan/adapters/elegoo_fdm_cc2/elegoo_fdm_cc2_message_adapter.cpp` at commit `46c7b814e055cf9675d58482d79f43d0bd2280da`. Only the meaning of these codes was transcribed into `cc2-control/web-src/src/lib/machine.ts` and `cc2-control/src/mqtt.c`; no SDK source code was copied. The print-history methods 1036 and 1051 and their reply fields are not implemented by that adapter; they were taken from MQTT traffic observed between ElegooSlicer and a CC2 printer.

References inspected for this update:
- https://github.com/OpenCentauri/cc-fw-tools
- https://github.com/OpenCentauri/cc-fw-tools/blob/main/LICENSE
- https://github.com/OpenCentauri/cc-fw-tools/releases
