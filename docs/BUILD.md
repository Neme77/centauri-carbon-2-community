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

The canonical builder source is [`../builder/current/`](../builder/current/).
The validated release environment is Windows 10/11 with Ubuntu under WSL.

Inside Ubuntu:

```sh
sudo apt update
sudo apt install python3 gcc-arm-linux-gnueabihf squashfs-tools make openssl
```

The builder requires local inputs which are not redistributed in Git. See the
[builder README](../builder/current/README.md) and place each input at the path
expected by the launcher. Every security-sensitive input is hash checked.

Typical Windows/WSL workflow:

```powershell
.\prepare.ps1
.\build_stock.ps1 -CheckKey
.\build_stock.ps1 -Preflight
.\build_stock.ps1
```

Run each stage separately and stop on the first failure. Community signing uses
`build_community.ps1` with its corresponding local key.

## Reproducibility record

For every release retain:

- source commit and tag;
- stock firmware name and SHA-256;
- compiler and SquashFS tool versions;
- local component hashes;
- generated firmware, RootFS, SWU and signature hashes;
- the real-printer validation record.

Do not commit stock firmware, vendor executables, generated images, credentials
or private signing keys.
