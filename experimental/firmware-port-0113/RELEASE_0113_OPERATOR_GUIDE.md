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
| ~~Decrypted 02.01.01.13 stock rootfs~~ | two connections stock pins | **done** — both match (step 3) |
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

## 3. Connections stock pins — DONE (verified)

All `NATIVE` stock pins are confirmed for 0113:
- device (2026-11-10): `bin/wifi_daemon` `8e19a620…`,
  `lib/modules/5.4.61/aic8800_fdrv.ko` `2973d7d7…`, kernel `5.4.61`, final init;
- decrypted stock rootfs (2026-11-10), on the machine holding the stock OTA and
  AES key: `etc/wifi/udhcpc_wlan0` `b76f68e3…`, `usr/share/udhcpc/set_lan.sh`
  `53cf6af5…`, control `opt/inst/daemon-000/daemon-000` `6fed6657…` (= profile
  `daemon_stock`).

Both overwritten-on-device files match their pins (base-independent, like the
Wi-Fi files), so **no change to `connections_component.py` is needed**. How it
was measured, for reproduction where the inputs live:

```sh
python3 builder/current/tools/inspect_base.py \
  builder/current/original_firmware/cc2_eeb001_02.01.01.13_20261020214142.zip.sig \
  --aes-key builder/current/keys/cc2_aes_key_v1.bin --unsquashfs $(command -v unsquashfs) --out <scratch outside the repo>
unsquashfs -d <scratch>/rx <scratch>/cpio/rootfs \
  etc/wifi/udhcpc_wlan0 usr/share/udhcpc/set_lan.sh opt/inst/daemon-000/daemon-000
sha256sum <scratch>/rx/etc/wifi/udhcpc_wlan0 <scratch>/rx/usr/share/udhcpc/set_lan.sh <scratch>/rx/opt/inst/daemon-000/daemon-000
```
The decrypted rootfs is vendor material — keep it outside the repo, never commit.

Not a hard blocker: the likely outcome is "already correct".

## 4. USB-Ethernet kernel modules (hard blocker — rebuild confirmed required)

`connections_component.py` `BINARY_PINS` pins four `cc2_ue300_*.ko` built for the
07 kernel ABI. **Device-verified 2026-11-10** that a rebuild is required: the 07
`.ko` do load on the 0113 kernel (vermagic `5.4.61 SMP preempt mod_unload ARMv7
p2v8` matches, CRC pass), but the purpose-built probe fails —
`insmod /lib/modules/5.4.61/cc2/cc2_ue300_abi_probe.ko` prints
`CC2_UE300_ABI_R1 FAIL usb_device layout/revision` and auto-unloads (`insmod
rc=255`, nothing left in `lsmod`). The 0113 kernel's `struct usb_device` layout
differs, so the modules cannot be reused as-is.

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

The cheap "do the 07 modules just work?" test is already done (above): they do
not — `abi_probe` reports the `usb_device` layout mismatch. So this is a true
blocker until the 0113 kernel headers are available; vermagic matching is not
enough.

Narrowed 2026-11-10 from the device `/proc/config.gz`:
- `# CONFIG_MODVERSIONS is not set` → **no `Module.symvers` is needed** (the
  kernel checks no symbol CRCs; loading depends on vermagic only). The failure is
  a compile-time `struct usb_device` layout difference, not a CRC mismatch.
- The exact kernel `.config` is available from the device
  (`ssh root@192.168.2.220 'zcat /proc/config.gz'`), so it is no longer an
  unknown.
- `# CONFIG_USB_NET_DRIVERS is not set` → the `cc2_ue300_*` are out-of-tree
  modules, built separately.

So the only remaining external input is the **Allwinner TinaLinux 5.4.61 kernel
source tree** matching build `#40` (`Linux version 5.4.61 … OpenWrt/Linaro GCC
6.4-2017.11 … #40 Tue Sep 22 2026`) plus the `cc2_ue300` module source.
`Module.symvers` is not required.

### Source availability (verified 2026-11-10) — why this stays blocked

- **The matching 0113 kernel source is not public.** The existing probe harness
  `experimental/network-connections/abi-probe/build.py` pins the only public
  TinaLinux tree, `OpenCentauri/oc-linux` commit `cd4ee0cf` (18 Feb 2026) — the
  **07-era** kernel used to build the 07 modules. That tree is **older** than the
  0113 image (`#40`, 22 Sep 2026), and no newer public TinaLinux commit exists.
  The 0113 kernel is known only by its package hash `bf965943…`. Headers matching
  it are not publicly findable, so the `struct usb_device` layout change cannot be
  reproduced from public source — it needs ELEGOO's 0113 kernel source (GPL).
- **Three of the four module sources are missing.** Only the 3000 probe has source
  in the repo (`experimental/network-connections/abi-probe/probe.c`, Makefile
  target `cc2_ue300_abi_probe_3000`). `cc2_ue300_mii.ko`, `cc2_ue300_r8152.ko` and
  the on-device-failing probe `cc2_ue300_abi_probe.ko` (`24c75778…`) are hash-only
  (no `.c`, no `.ko` in the checkout) — also needed from ELEGOO.
- `printer.config` already has `CONFIG_PM`/`CONFIG_PM_SLEEP`/
  `CONFIG_USB_DEFAULT_PERSIST` on and `CONFIG_MODVERSIONS` off and the 07 modules
  were built with it, so those switches alone do **not** explain the layout break;
  it comes from a kernel-source difference in the 0113 build, not the config.

Build recipe once the kernel source (`$KSRC`) is available (via the harness in
`experimental/network-connections/abi-probe/`, repointed at the 0113 source). The device `.config`
sets `CONFIG_PM=y`/`CONFIG_PM_SLEEP=y`/`CONFIG_USB=y`/`CONFIG_USB_DEFAULT_PERSIST=y`
(the fields that make `struct usb_device` differ from the 07 headers), and the
toolchain is `arm-openwrt-linux-gnueabi-gcc 6.4.1` (`CONFIG_CC_VERSION_TEXT`):

```sh
ssh root@192.168.2.220 'zcat /proc/config.gz' > "$KSRC/.config"
make -C "$KSRC" ARCH=arm CROSS_COMPILE=arm-openwrt-linux-gnueabi- olddefconfig modules_prepare
make -C "$KSRC" M=<cc2_ue300 module source> ARCH=arm CROSS_COMPILE=arm-openwrt-linux-gnueabi- modules
# confirm on the device (idle, reversible): insmod cc2_ue300_abi_probe.ko must NOT print
# "CC2_UE300_ABI_R1 FAIL usb_device layout/revision"; rmmod afterwards.
```
Then repackage `runtime.tar.gz`, stage it, and update the four `BINARY_PINS`.

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
