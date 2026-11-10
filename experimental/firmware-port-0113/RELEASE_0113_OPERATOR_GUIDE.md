# 02.01.01.13 release — operator / maintainer guide for the remaining steps

Everything that can be done on the host without vendor material, the toolchain,
the device or keys is committed and host-tested; 02.01.01.13 is refused by the
build gate until a maintainer opens it. This guide is the exact sequence for the
steps that need those external inputs. Each says: who, exact command, inputs,
verification, and whether it is a hard blocker.

Device is `root@192.168.2.220` (public community SSH password, in
`docs/INSTALL.md`). Never commit any `.so`, `.ko`, vendor binary, key or build
output.

## Inputs you must supply

| Input | For | Note |
|---|---|---|
| Qualified cross toolchain `/opt/cc2-cross/…` (GCC 6.5.0, glibc 2.23) | reactor `.so`, dual wrapper | `docs/BUILD.md` |
| R2 camera runtime libs in `builder/current/components/camera/runtime/` | camera component | base-independent (pins in `cc2-control/installer/camera_component.py`) |
| `libcolib.so` link stub (`14288a4a…`) | reactor build | `experimental/firmware-port/reactor/vendor-link/` |
| Decrypted 02.01.01.13 stock rootfs (stock OTA + AES key) | two connections stock pins | needed only to confirm/measure |
| 02.01.01.13 kernel headers/sources | USB-Ethernet `.ko` | **hard blocker** if ELEGOO does not publish them |
| Private community release key + AES key v1 + stock OTA | signed build | maintainer only |

## 1. Camera component (operator, host)

The runtime libs are base-independent; only the identity pin changes.

```sh
python3 builder/current/prepare_camera.py --base-version 02.01.01.13
python3 - <<'PY'
import json
m=json.load(open('builder/current/components/camera/prepared-manifest.json'))
assert m['camera_sha256']=='90da7634c21b74b3bf943d68129da78668b47f9706620e158283697ab95553a7', m['camera_sha256']
print('camera component OK for 0113')
PY
```
Not a blocker. `prepared/` and the manifest are gitignored (operator-local).

## 2. Reactor component + active validation (operator, toolchain + device)

Follow `REACTOR_0113_BUILD_AND_VALIDATE.md` in full:
`python3 builder/current/prepare_reactor.py --base-version 02.01.01.13`, then the
safe preflight and the active test whose `activation.marker` proves the
corrected fingerprints pass. Not a release blocker only once the active test
passes on hardware.

## 3. Connections stock pins (needs the decrypted 0113 stock rootfs)

Already confirmed for 0113 (device, 2026-11-10): `bin/wifi_daemon` `8e19a620…`,
`lib/modules/5.4.61/aic8800_fdrv.ko` `2973d7d7…`, kernel `5.4.61`, and the final
init pin. Only two stock files are still unverified because the Community patch
overwrites them on the device:

- `etc/wifi/udhcpc_wlan0` (pin `b76f68e3…`)
- `usr/share/udhcpc/set_lan.sh` (pin `53cf6af5…`)

Measure them from the **decrypted** 0113 stock rootfs (the builder can decode and
extract it with the AES key; see `tools/inspect_base.py` and
`firmware_builder.py --base-version 02.01.01.13 --preflight-only --keep-work`,
then `unsquashfs` the `rootfs`):

```sh
# inside the extracted stock rootfs
sha256sum etc/wifi/udhcpc_wlan0 usr/share/udhcpc/set_lan.sh
```
- Match `b76f68e3…` / `53cf6af5…` → the `NATIVE` pins are already correct
  (these stock files are base-independent, like the Wi-Fi ones), nothing to do.
- Differ → update the two `NATIVE` entries in
  `builder/current/core/connections_component.py` (a reviewed code change) to the
  measured 0113 values.

Not a hard blocker: the likely outcome is "already correct".

## 4. USB-Ethernet kernel modules (hard blocker)

`connections_component.py` `BINARY_PINS` pins four `cc2_ue300_*.ko` built for the
07 kernel ABI. The 0113 kernel keeps the version string `5.4.61` but the kernel
binary differs (profile `kernel` hash), so ABI compatibility is not guaranteed.

To port:
1. Rebuild the four `cc2_ue300_{abi_probe,abi_probe_3000,mii,r8152}.ko` against
   the **02.01.01.13 kernel headers/sources** (same build as the 07 modules; see
   `prepare_connections.py` and the Connections runtime provenance).
2. Repackage the Connections runtime archive
   (`builder/current/components/connections/runtime.tar.gz`, operator-supplied,
   never committed) with the rebuilt modules; stage it:
   `python3 builder/current/prepare_connections.py --runtime <archive>`.
3. Update the four `.ko` hashes in `BINARY_PINS`
   (`connections_component.py`) to the rebuilt values (reviewed code change).

If the 0113 kernel sources are unavailable, first test whether the existing 07
`.ko` load on the 0113 kernel (`insmod` on the device, idle, reversible) — the
shared `5.4.61` version string may mean the ABI matches. If they load cleanly,
the modules can stay as-is and only a re-measure is needed; if not, this stays
blocked until the sources are available.

## 5. CC2 Control compatibility (runtime, separate PR)

For CC2 Control on the device to accept the 0113 printer, bump
`cc2-control/installer/compatibility.json` `printer_sha256` to `688b7bf8…` and
`firmware_version_file_sha256`/`vendor_base` as appropriate (`daemon_sha256`
stays, byte-identical). This is a `cc2-control/` change, so regenerate the
builder snapshot as the last commit of that PR
(`python3 experimental/pr128-integration/sync_source.py`) — one snapshot per PR,
so keep it out of this builder PR. (The camera rollback needs **no** change: its
`case` already accepts the base-independent pre-camera init `bcabb1fa…`.)

## 6. Open the gate, build, sign, hardware-test (maintainer)

Only after 1–4 are ported and emulation/hardware-checked per component:

1. In `base_profiles/02.01.01.13.json` set `ported_patch_set: true`, and add
   `'02.01.01.13': '<profile sha256>'` to `QUALIFIED_BASES` in
   `firmware_builder.py` (a reviewed code change — this is the qualification
   act). Update `test_base_profiles.py` to expect 0113 accepted.
2. Build the signed image (maintainer, with the private key, AES key v1 and the
   stock OTA under `original_firmware/`):
   ```sh
   python3 builder/current/core/firmware_builder.py --base-version 02.01.01.13 \
     --private-key <community-release-key> --require-golden
   ```
   (preflight first with `--preflight-only` to verify inputs before signing.)
3. **Hardware-test the builder-produced image** on the printer per
   `docs/TESTING.md` — not the hand-installed binaries, but the OTA the builder
   produced (§6 phase 4 of `PIPELINE_MULTI_BASE_DESIGN.md`). Record results.
4. Release only after that passes. Native display and reactor remain as declared
   (native display not-included; reactor per step 2).

## What is already done (no action)

- Builder core multi-base + native-display exclusion (§3.5); 0113 patch dispatch;
  base-aware install-surface, camera and reactor pins; corrected 0113 reactor
  sources; `prepare_camera`/`prepare_reactor` base-aware. 07 stays byte-identical
  (host tests). 0113 refused until step 6.
