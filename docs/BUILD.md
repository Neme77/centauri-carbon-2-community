# Firmware v3.8 build guide

## Overview

The v3.8 builder uses the same trusted build environment and signing layout as v3.7. It verifies the official input, applies the source-controlled patches, verifies the v3.7 intermediate state, and installs the separately supplied tested v3.8 printer component.

The public repository is intentionally not a self-contained firmware distribution.

## Required external material

The following files are not included:

- legally obtained ELEGOO 02.01.00.00 stock firmware;
- `keys/cc2_aes_key_v1.bin`;
- the appropriate private signing key for the selected build mode;
- the tested `components/printer/elegoo_printer_v3_8` reference;
- `sshd` and the compiled `dual_verify.bin` from the established v3.7 build environment;
- SquashFS Tools 4.6.1.

Never commit private keys, the AES key, credentials, build output, or vendor firmware to the repository.

## Windows and WSL preparation

The established environment uses `C:\CC2_BUILD` and Ubuntu under WSL.

Place the v3.8 files beside the existing v3.7 environment, then run:

```powershell
cd C:\CC2_BUILD
wsl -d Ubuntu -u root --exec python3 /mnt/c/CC2_BUILD/prepare_v3_8.py
```

The preparation script copies only the verified reusable `sshd` and `dual_verify.bin` components. It does not copy or alter signing keys.

## Stock-signed build

Run every step separately and continue only after success:

```powershell
.\build_stock_v3_8.ps1 -CheckKey
.\build_stock_v3_8.ps1 -Preflight
.\build_stock_v3_8.ps1
```

## Community-signed build

For an installation where the matching Dual Trust configuration is already active:

```powershell
.\build_community_v3_8.ps1 -CheckKey
.\build_community_v3_8.ps1 -Preflight
.\build_community_v3_8.ps1
```

Output and logs are written under `output/` and `logs/` with names beginning with `CC2_V3_8_`.

## Trust model

Dual Trust v2 preserves the official ELEGOO trust path and adds the project's community public key. ELEGOO private keys are never distributed, embedded, or required by the community signing workflow.

The first Dual Trust installation must follow the established stock-trusted installation path. Later community updates require the matching community trust configuration to remain installed.

## Reproducibility boundary

The source tree contains the builder, patch descriptions, validation logic, public keys, and tests. The MQTT v1 intermediate source was not recovered, so the final tested printer executable cannot currently be reconstructed from this repository alone. This limitation is documented explicitly and the reference binary is pinned by hash.

Some validation scripts require their external firmware inputs as command-line arguments. `test_dual_emulation.py` additionally requires the Python `unicorn` package, while `test_v38_reference.py` requires the excluded pinned printer reference.
