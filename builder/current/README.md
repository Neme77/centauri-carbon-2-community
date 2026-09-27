# Current firmware builder

This directory contains the current firmware assembly logic and its host-side
tests. Working filenames are stable; release numbers are recorded in Git tags,
the changelog and source constants.

## Inputs not stored in Git

The builder fails closed when an input is absent or does not match its pinned
hash. Supply these locally before building:

- a legally obtained Centauri Carbon 2 stock firmware package;
- the pinned vendor printer reference binary;
- the SSH executable required by the image;
- SquashFS 4.6.1 tools built from the pinned upstream source;
- the appropriate private signing key;
- the generated/prepared CC2 Control component files.

Vendor binaries, private keys and generated firmware are intentionally excluded
from the repository. Their expected hashes and validation logic are in
`core/firmware_builder.py`.

The exact CC2 Control source snapshot used by the preparation step is committed
as `components/cc2-control/source/source.zip`; the unpacked canonical source is
available at the repository root under `cc2-control/`.

## Layout

- `core/firmware_builder.py`: fail-closed image builder;
- `prepare.py`: validates and stages CC2 Control;
- `build_stock.ps1`, `build_community.ps1`: Windows/WSL entry points;
- `launch_helpers/`: argument, logging and output handling;
- `dualtrust/`, `patches/`, `tools/`: source-level build helpers;
- `tests/`: host-side validation.

See [`../../docs/BUILD.md`](../../docs/BUILD.md) for the full workflow.
