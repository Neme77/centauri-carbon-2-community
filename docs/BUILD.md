# Reproducible build guide

This repository is intended to keep the Community Firmware build process and CC2 Control source auditable and reproducible.

## Supported target

- Printer: ELEGOO Centauri Carbon 2
- Stock firmware base: 02.01.00.00
- Community firmware: V4.1
- Integrated CC2 Control baseline: 1.1.25
- Current CC2 Control hotfix: 1.1.26

## Build environment

The validated environment is Windows 10/11 with Ubuntu under WSL.

Required tools inside Ubuntu:

```sh
sudo apt update
sudo apt install python3 gcc-arm-linux-gnueabihf squashfs-tools make
```

SquashFS compatibility should be checked against the version expected by the builder before producing a release package.

## External input

The firmware builder requires a legally obtained ELEGOO Centauri Carbon 2 stock firmware package.

The project does not need to redistribute the stock firmware image in the source tree.

The official ELEGOO Centauri Carbon 2 repository also contains the signing-tool material used by ELEGOO under:

```text
elegoo/lib/signtools/key/
```

This includes the public key, private key and AES key published by ELEGOO. They are not duplicated in this repository; obtain them from the official ELEGOO source when required by the selected build workflow.

Official repository:

https://github.com/elegooofficial/CentauriCarbon2

## CC2 Control

CC2 Control is built independently from the firmware image.

From the CC2 Control source directory:

```sh
make clean
make
```

The expected output is:

```text
build/cc2-control
```

Verify the binary:

```sh
file build/cc2-control
```

It must be an ARM 32-bit EABI5 statically linked executable.

## Firmware build

The exact historical V4.1 R8 builder is pinned by SHA-256:

```text
c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9
```

Builder archive:

```text
CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip
```

See [../builder/v4.1-r8/README.md](../builder/v4.1-r8/README.md) for the historical snapshot and source manifest.

Use the V4.1 builder package/source and run preparation first. The stock-signed release workflow is:

```powershell
.\prepare_v4_1.ps1
.\build_stock_v4_1.ps1 -CheckKey
.\build_stock_v4_1.ps1 -Preflight
.\build_stock_v4_1.ps1
```

Run each stage separately and continue only after the previous stage succeeds.

The builder verifies the input package, assembles the filesystem overlay, integrates CC2 Control, rebuilds the firmware package and verifies the resulting signing/package chain.

## Reproducibility

For a release build, record:

- input stock firmware filename and SHA-256;
- builder commit/tag;
- CC2 Control commit/tag;
- compiler/tool versions;
- generated firmware SHA-256;
- generated RootFS, SWU and signature hashes when available.

The published V4.1 release contains the release firmware, checksum and the validated R8 builder package. The CC2 Control 1.1.26 release contains a complete source snapshot with backend source, web UI, tests, scripts, tools and firmware-integration files.

For the currently published 1.1.26 snapshot, the release source archive is the versioned source artifact and is pinned by SHA-256. The repository documentation describes how to rebuild and verify it. Future development should keep the browsable source tree and release source snapshot in sync before publication.

## Clean-room rule

Do not commit:

- personal printer credentials;
- LAN access codes;
- generated firmware images;
- local build output;
- private configuration files;
- third-party binaries unless their redistribution terms are known and documented.

Vendor material should be referenced from its authoritative source whenever possible.
