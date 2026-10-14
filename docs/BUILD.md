## Community 4.4 firmware base

`builder/current/` targets only official OTA **02.01.01.07**. Its pinned patch addresses, vendor hashes and runtime bindings are incompatible with the firmware inputs used by previous builders. Use the exact official package `cc2_eeb001_02.01.01.07_20260922210235.zip.sig`; unknown or older inputs are rejected. Vendor files and signing material remain local external inputs.

The native 792-byte Dual Trust payload is generated from assembly/public-key source during reactor preparation and must reproduce the qualified SHA-256. The library and bridge keep the qualified GCC 6.5.0/glibc 2.23 requirements; emulator tests are separate from full printer validation.

# Build guide

## CC2 Control

The canonical source is [`../cc2-control/`](../cc2-control/).

Host validation:

```sh
cd cc2-control
make clean test CROSS= CC=gcc
```

Production build requirements:

- `arm-linux-gnueabihf-gcc`;
- static ARM libc development files;
- GNU Make and Python 3.

```sh
cd cc2-control
make clean all
```

The packaged tree is written to `cc2-control/dist/` and is ignored by Git.

## Firmware builder

The canonical builder source is [`../builder/current/`](../builder/current/). The validated release environment is Windows 10/11 with Ubuntu under WSL.

Inside Ubuntu:

```sh
sudo apt update
sudo apt install python3 gcc-arm-linux-gnueabihf squashfs-tools make openssl
```

The builder requires local inputs which are not redistributed in Git. See the [builder README](../builder/current/README.md) and place each input at the path expected by the launcher. Every security-sensitive input is hash checked.

Typical Windows/WSL workflow:

```powershell
.\prepare.ps1
.\build_stock.ps1 -CheckKey
.\build_stock.ps1 -Preflight
.\build_stock.ps1
```

Run each stage separately and stop on the first failure. Community signing uses `build_community.ps1` with its corresponding local key.

## Reproducibility record

For every release retain:

- source commit and tag;
- stock firmware name and SHA-256;
- compiler and SquashFS tool versions;
- local component hashes;
- generated firmware, RootFS, SWU and signature hashes;
- the real-printer validation record.

Do not commit stock firmware, vendor executables, generated images, credentials or private signing keys.
