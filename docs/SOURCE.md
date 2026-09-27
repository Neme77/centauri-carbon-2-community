# Source code and reproducibility

This project aims to keep released community components auditable and reproducible.

## CC2 Control 1.1.26

The complete source snapshot for CC2 Control 1.1.26 is published as a GitHub Release asset:

```text
CC2-Control-1.1.26-Source.zip
```

SHA-256:

```text
e11200d256c81e3be2fac9d1ae91cf06af7cb51c178fbabec9b1a7ef6004556e
```

Direct source download:

https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.26/CC2-Control-1.1.26-Source.zip

Checksum file:

https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.1.26/CC2-Control-1.1.26-Source-SHA256.txt

The source archive contains the backend source, dashboard/web UI, tests, startup scripts, configuration example, development tools and firmware-integration material used for the 1.1.26 release.

## Community Firmware V4.1

The V4.1 release publishes the validated R8 firmware builder package alongside the signed firmware and checksums. The retained R8 package has been verified against the release asset and is byte-identical.

Builder:

```text
CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip
```

Archive SHA-256:

```text
c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9
```

Release:

https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.1

Historical builder index:

../builder/v4.1-r8/README.md

The builder requires a legally obtained ELEGOO stock firmware input. ELEGOO's official Centauri Carbon 2 repository also publishes its signing-tool material under `elegoo/lib/signtools/key/`; this project references that authoritative source rather than duplicating those keys.

Official ELEGOO repository:

https://github.com/elegooofficial/CentauriCarbon2

## Verification

Before building, verify downloaded source and builder archives against their published SHA-256 values.

For build steps and the validated environment, see [BUILD.md](BUILD.md).

## Repository policy

- project documentation is written in English;
- generated firmware images and local credentials are not committed;
- unrelated tools are kept outside the firmware repository;
- release artifacts are pinned by hashes;
- incomplete source trees are not presented as complete source;
- historical material remains recoverable through Git history, release assets and the pre-cleanup archive branch.
