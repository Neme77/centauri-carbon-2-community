# Community Firmware V4.1 — R8 builder snapshot

This directory documents the exact builder used for the published **Community Firmware V4.1 R8** release.

## Release identity

- Firmware: Community Firmware V4.1
- Integration revision: R8 persistent-mount fix
- Integrated CC2 Control: 1.1.25
- Builder archive: `CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip`
- Archive SHA-256: `c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9`

The archive currently attached to the V4.1 GitHub release has been verified byte-for-byte against the retained R8 build package.

## What the archive contains

The published archive contains the V4.1 builder source, launcher scripts, preparation scripts, tests, firmware patches, Dual Trust source, CC2 Control integration scripts, public keys, reference manifests, and the validated binary inputs needed by that historical build.

The package also contains the CC2 Control 1.1.25 R8 source snapshot used by `prepare_v4_1.py`.

## Why the historical builder remains a release asset

The V4.1 R8 package is preserved as the exact historical build snapshot. Rewriting its source merely to modernize wording would change the source used for the released package and weaken reproducibility.

Current repository documentation is English-only. The historical release archive is therefore kept immutable and hash-pinned, while new builder revisions will be published with English source and documentation from the start.

## Download

https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.1/CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip

## Reproducing V4.1

See [../../docs/BUILD.md](../../docs/BUILD.md) for the build environment and workflow.

The builder expects a legally obtained ELEGOO Centauri Carbon 2 stock firmware input and verifies fixed reference hashes before building.
