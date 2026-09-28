# Source and reproducibility

The preferred form for modifying CC2 Control is committed directly under [`cc2-control/`](../cc2-control/). Current firmware builder logic is committed under [`builder/current/`](../builder/current/).

Release archives are convenience snapshots, not substitutes for the repository source. A release should be traceable to a signed tag or documented commit and must publish checksums for all downloadable artifacts.

## External inputs

A complete firmware image also contains vendor and third-party material that is not owned by this project. The builder therefore requires separately supplied, legally obtained inputs and validates them against pinned hashes.

ELEGOO's public Centauri Carbon 2 repository is available at:

<https://github.com/elegooofficial/CentauriCarbon2>

Private signing keys, LAN access codes, stock firmware, generated images and vendor executables are not stored in this repository.

## Release policy

- Source changes land before or with the corresponding binary release.
- The release identifies the exact source tag or commit.
- Checksums cover firmware, updater and source archive downloads.
- Version-specific facts belong in release notes and `CHANGELOG.md`.
- Canonical development filenames remain stable.
- Old release snapshots stay recoverable from tags, release assets and Git history; published history is not rewritten for cosmetic cleanup.

See [`BUILD.md`](BUILD.md) for the build workflow and [`../NOTICE.md`](../NOTICE.md) for licensing scope and third-party limitations.
