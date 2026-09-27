# Reproducible build guide

This repository keeps the Community Firmware build process and CC2 Control source auditable and reproducible.

## Current target

- Printer: ELEGOO Centauri Carbon 2
- Stock firmware base: 02.01.00.00
- Community firmware release: V4.2
- Integrated CC2 Control: 1.1.27
- Builder lineage: V4.1/R8 with the 1.1.27 integration update

## Validated build environment

The validated environment is Windows 10/11 with Ubuntu under WSL.

Required tools inside Ubuntu:

```sh
sudo apt update
sudo apt install python3 gcc-arm-linux-gnueabihf squashfs-tools make
```

SquashFS compatibility should match the version expected by the builder before producing a release package.

## External input

The firmware builder requires a legally obtained ELEGOO Centauri Carbon 2 stock firmware package.

ELEGOO's official Centauri Carbon 2 repository also publishes signing-tool material under:

```text
elegoo/lib/signtools/key/
```

Official repository:

https://github.com/elegooofficial/CentauriCarbon2

## CC2 Control 1.1.27

The complete source and package-builder snapshot is distributed as:

```text
CC2-Control-1.1.27-Complete-Source-and-Builder.zip
```

SHA-256:

```text
1c3039678c27cbad6e9916dc203d4ea1b87910c7c35f7702c6a1195b33a2be63
```

From the release source directory, the provided PowerShell release builder compiles the ARM executable and assembles the multiplatform updater.

The resulting updater is:

```text
CC2-Control-1.1.27-Multiplatform-Update.zip
```

SHA-256:

```text
17012fc53eaca3bd3ab1a1829172c9d12e8ed56dcf4b135d9e17a8acfc6fdccc
```

## Firmware V4.2 builder

Release builder:

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip
```

SHA-256:

```text
dd38b9aa4ecd779a246403aa3ba8c075f231d538b12694fa4d7303cdf01b71dd
```

The builder retains some internal `v4_1` directory and script names because it is derived directly from the validated V4.1/R8 line.

The stock-signed workflow remains:

```powershell
.\prepare_v4_1.ps1
.\build_stock_v4_1.ps1 -CheckKey
.\build_stock_v4_1.ps1 -Preflight
.\build_stock_v4_1.ps1
```

Run every stage separately and continue only after the previous stage succeeds.

The builder validates the source snapshot, runs host-side tests, compiles the ARM executable, assembles the firmware overlay, rebuilds the package and validates the resulting build chain.

## Published V4.2 firmware

```text
CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig
```

SHA-256:

```text
4e9944dd0b3e5eaff24bdaf6ed34d32002f25a069e26ec05549b612e94c9ec6b
```

## Mandatory post-install power cycle

After installing firmware produced by this release, **switch the printer completely off and then power it on again before doing anything else**.

This full power cycle is required to realign Canvas and the related background services. Wait approximately **30–60 seconds** after power-on before normal use.

## Reproducibility record

For each release build, retain:

- input stock firmware filename and SHA-256
- builder archive and SHA-256
- CC2 Control source archive and SHA-256
- compiler/tool versions
- generated firmware SHA-256
- generated RootFS, SWU and signature hashes when available

## Historical V4.1/R8 builder

The exact V4.1/R8 builder remains pinned at:

```text
c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9
```

See [../builder/v4.1-r8/README.md](../builder/v4.1-r8/README.md).

## Clean-room rule

Do not commit:

- personal printer credentials
- LAN access codes
- generated firmware images
- local build output
- private configuration files
- third-party binaries unless their redistribution terms are known and documented

Vendor material should be referenced from its authoritative source whenever possible.
