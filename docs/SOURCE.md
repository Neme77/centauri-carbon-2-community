# Source code and reproducibility

This project aims to keep released community components auditable and reproducible.

## Current release: Community Firmware V4.2

Community Firmware **V4.2** integrates **CC2 Control 1.1.27**.

Published release artifacts are pinned by SHA-256.

### Firmware

```text
CC2_V4_2_STOCK_20260927_021129_d31ed55e.zip.sig
4e9944dd0b3e5eaff24bdaf6ed34d32002f25a069e26ec05549b612e94c9ec6b
```

### Builder

```text
CC2_BUILDER_V4_2_CC2_CONTROL_1.1.27_RELEASE.zip
dd38b9aa4ecd779a246403aa3ba8c075f231d538b12694fa4d7303cdf01b71dd
```

The V4.2 builder is derived from the validated V4.1/R8 build chain. Some internal directory and script names remain `v4_1` to preserve continuity with the tested builder lineage.

### CC2 Control 1.1.27 source

```text
CC2-Control-1.1.27-Complete-Source-and-Builder.zip
1c3039678c27cbad6e9916dc203d4ea1b87910c7c35f7702c6a1195b33a2be63
```

The source archive contains:

- backend source
- web UI
- validation tests
- firmware integration files
- startup scripts
- configuration example
- development tools
- multiplatform package builder

### Standalone updater

```text
CC2-Control-1.1.27-Multiplatform-Update.zip
17012fc53eaca3bd3ab1a1829172c9d12e8ed56dcf4b135d9e17a8acfc6fdccc
```

## Mandatory post-install power cycle

After installing the full firmware or the standalone CC2 Control updater, **switch the printer completely off and then power it on again before doing anything else**.

This full power cycle is required to realign Canvas and the related background services.

## Historical V4.1 / R8 release

The previous V4.1/R8 release remains preserved as a reproducibility snapshot.

Builder:

```text
CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip
```

SHA-256:

```text
c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9
```

See [../builder/v4.1-r8/README.md](../builder/v4.1-r8/README.md).

## External vendor input

The firmware builder requires a legally obtained ELEGOO Centauri Carbon 2 stock firmware package.

ELEGOO's official Centauri Carbon 2 repository publishes its signing-tool material under:

```text
elegoo/lib/signtools/key/
```

Official repository:

https://github.com/elegooofficial/CentauriCarbon2

## Verification

Before building or installing, verify downloaded archives against the published SHA-256 values.

See [BUILD.md](BUILD.md) for the build workflow.

## Repository policy

- project documentation is written in English
- generated firmware images and local credentials are not committed
- unrelated tools are kept outside the firmware repository
- release artifacts are pinned by hashes
- historical material remains recoverable through Git history, release assets and archive branches
