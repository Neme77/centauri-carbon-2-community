# V4.1 R8 builder source manifest

The following source and configuration groups are present in the published R8 builder archive.

## Top-level build scripts

- `prepare_v4_1.py`
- `prepare_v4_1.ps1`
- `build_stock_v4_1.ps1`
- `build_community_v4_1.ps1`
- `launch_helpers_v4_1/`

## Firmware builder

- `cc2_builder_v4_1/cc2_firmware_builder_v4.1.py`
- `cc2_builder_v4_1/tests/`
- `cc2_builder_v4_1/patches/`
- `cc2_builder_v4_1/tools/`

## Dual Trust

- `cc2_builder_v4_1/dualtrust/dual_verify.S`
- `cc2_builder_v4_1/dualtrust/assemble_payload.py`
- `cc2_builder_v4_1/dualtrust/apply_dualtrust.py`
- `cc2_builder_v4_1/dualtrust/v2_reference.json`
- community and stock public keys

## CC2 Control integration

- runtime startup scripts
- init script
- configuration helper
- CC2 Control 1.1.25 R8 source snapshot

## Pinned binary inputs in the historical package

These are preserved inside the exact historical builder archive and are not duplicated as loose repository files:

- `cc2_builder_v4_1/components/ssh/sshd`
  - SHA-256: `7746b7085a1539f0a8db1aea89a97ea0ed7e8e5e722fecd476fdedfa1e7a002c`
- `cc2_builder_v4_1/components/printer/elegoo_printer_v3_8`
  - SHA-256: `c21126e78a63ac9140e7347c56f363d5e513495c58c06fc17e8ef97846f3c0a3`
- `cc2_builder_v4_1/dualtrust/dual_verify.bin`
  - SHA-256: `33d013ca4a63334fdbbd0adb77bd10f81e2e21c3ac41229e216eb17e5949d06f`
- `cc2_builder_v4_1/components/cc2-control/source/CC2-Control-v1.1.25-R8-Source.zip`
  - SHA-256: `ce5bf09785f309f5f91242bbc3c3a2e5badff6401ea4c98e434f56b53fd5432d`

## Builder source identity

- `cc2_builder_v4_1/cc2_firmware_builder_v4.1.py`
  - SHA-256: `57c32c3f8d855ad126039e5f1cc40160d2403ce6d5f513aa576e020a2e9e57f1`

## Complete archive identity

`CC2_BUILDER_V4_1_CC2_CONTROL_1.1.25_R8_PERSISTENT_MOUNT_FIX.zip`

SHA-256:

`c94de033abea04bfe6b3788e131a0a9fdc6d9bfa6ec90a40288ba242750211e9`
