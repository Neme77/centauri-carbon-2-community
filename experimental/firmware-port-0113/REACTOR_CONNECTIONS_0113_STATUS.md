# Reactor + Connections on 02.01.01.13 — port status and inputs

Stand 2026-11-10. Classifies every base-specific pin in the reactor and
connections components, what is already correct for 02.01.01.13, what is
measurable now, and what strictly needs the decrypted 0113 stock rootfs or the
0113 kernel sources. No code is changed here; this scopes the remaining work.

Gate reminder: 02.01.01.13 is refused up front (`ported_patch_set: false`, not
in `QUALIFIED_BASES`), so none of these components runs for 0113 today. They
must be ported before a maintainer opens the gate.

## Reactor — a per-base native port, not a parametrisable pin

Unlike the camera runtime (base-independent shell + one identity pin), the
reactor's **code** is base-specific: `experimental/firmware-port/reactor/`
(`reactor_module.cpp`, `fingerprints.h`, `port-manifest.json`) hard-codes the
vendor printer's function **virtual addresses**, and `prepare_reactor.py`
compiles `libcc2-reactor-v2.so` against that ABI with the qualified toolchain
(`/opt/cc2-cross/...`, GCC 6.5.0, glibc 2.23). A new base moves those addresses,
so the `.so` must be re-derived and rebuilt.

Base-specific builder pin (only one):
- `reactor_component.py` `VENDOR_SHA256` = the **patched** printer. 07
  `19712b59…`; 0113 `688b7bf8…` (already in the profile as
  `port.outputs.printer_release.sha256`).
- `LIBCO_SHA256` (`14288a4a…`) and `FIRMWARE_LAUNCHER_SHA256` are
  base-independent (libcolib is byte-identical across bases; the launcher is a
  Community artifact). The `run_printer.sh` / `etc/init.d/printer` transforms are
  text-based and base-independent.

What fork 2 already built (NOT in version control):
- A 0113 reactor: device `opt/inst/cc2-reactor/libcc2-reactor-v2.so` =
  `8f78646d…`, a `prepared-manifest.json` pinning `vendor_sha256 = 688b7bf8…`,
  and a `port-manifest.json` with the 07→0113 address `mapping`
  (`replaced_functions_comparison`, e.g. `_ZN15ReactorCallback6invokeEd`).
- This lives only in `~/cc2-backup-20261109/fork2-reactor-2.01.01.13/`
  (`reactor_module.cpp.bak`, `port-manifest.json.bak`, `prepared-manifest.json`,
  `uncommitted.patch`) and on the device. Verified absent from every branch:
  `VENDOR_SHA256 = 19712b59…` (07) on develop, update-2.03.03.13 and this
  branch, and update-2.03.03.13 does not change the reactor vs develop.
- It was **never activated or hardware-validated** on 0113 (the reactor stays
  gated off; the device ran it installed-but-disabled).

To integrate reactor for 0113 (maintainer/operator):
1. Bring fork 2's 0113 `reactor_module.cpp` / `fingerprints.h` /
   `port-manifest.json` into `experimental/firmware-port/reactor/` under review
   (they are source, not the `.so`; the `.so` is never committed).
2. Make `reactor_component.py` `VENDOR_SHA256` base-aware (take it from
   `port.outputs.printer_release.sha256`, default 07), exactly as the camera
   pins were threaded; `prepare_reactor.py` likewise `--base-version`.
3. Operator rebuilds `libcc2-reactor-v2.so` with the qualified toolchain.
4. Hardware-validate the **active** reactor on 0113 before any release decision.

Because step 4 has never happened, a first 0113 release may also keep the
reactor installed-but-disabled (as the validated device ran it) rather than
activate it.

## Connections — mixed; the init pin is already base-independent

Per-pin classification of `connections_component.py`:

| Pin | Value | Base? | Status |
|---|---|---|---|
| `NATIVE etc/init.d/printer` | `e7537298…` | **independent** | Device-verified final installed init (stock+camera+reactor). Already correct for 0113. |
| `NATIVE etc/init.d/cc2-camera-launch` | `f05accc9…` | independent | Community camera hook (from camera.install). |
| `NATIVE etc/init.d/cc2-reactor-launch` | `8d4a19c9…` | independent | Community reactor hook (= reactor `FIRMWARE_LAUNCHER_SHA256`). |
| `NATIVE bin/wifi_daemon` | `8e19a620…` | stock | **Device-verified 2026-11-10: 0113 = `8e19a620…` (match).** Base-independent. |
| `NATIVE lib/modules/5.4.61/aic8800_fdrv.ko` | `2973d7d7…` | stock | **Device-verified 2026-11-10: 0113 = `2973d7d7…` (match).** Base-independent. |
| `NATIVE etc/wifi/udhcpc_wlan0` | `b76f68e3…` | stock | **Verified 2026-11-10 against the decrypted 0113 stock rootfs: `b76f68e3…` (match).** Base-independent. |
| `NATIVE usr/share/udhcpc/set_lan.sh` | `53cf6af5…` | stock | **Verified 2026-11-10 against the decrypted 0113 stock rootfs: `53cf6af5…` (match).** Base-independent. |
| `BINARY_PINS cc2_ue300_*.ko` (4) | — | **community/kernel-ABI** | **Rebuild confirmed required (device 2026-11-10).** The 07 `.ko` load on the 0113 kernel (vermagic `5.4.61 SMP preempt mod_unload ARMv7 p2v8` + CRC pass), but `insmod cc2_ue300_abi_probe.ko` prints `CC2_UE300_ABI_R1 FAIL usb_device layout/revision` (init returns error, module auto-unloads). The 0113 kernel's `struct usb_device` layout differs, so the four modules must be rebuilt against the **0113 kernel headers/sources** — hard blocker. (Kernel: `5.4.61 #40`, OpenWrt/Linaro GCC 6.4-2017.11.) |
| `BINARY_PINS control`, `libcc2-network-compat-r1.so` | — | community | Confirm base-independent when re-pinning. |
| `MODULES` path `lib/modules/5.4.61/` | — | kernel version | **Device-verified 2026-11-10: `uname -r` = `5.4.61`, only that dir in `/lib/modules`.** Path correct for 0113. |

Device measurement 2026-11-10 (`ssh root@192.168.2.220 'uname -r; sha256sum
/bin/wifi_daemon /lib/modules/5.4.61/aic8800_fdrv.ko; ls /lib/modules'`):
`5.4.61`; `wifi_daemon 8e19a620…`; `aic8800_fdrv.ko 2973d7d7…` — both match the
`NATIVE` pins, so those three connections items are already correct for 0113.
Only `udhcpc_wlan0` / `set_lan.sh` (stock, overwritten on the device) and the
USB-Ethernet `.ko` remain.

Local device backups (`~/cc2-backup-20261109/`) do **not** contain
`wifi_daemon`, `aic8800_fdrv.ko`, `udhcpc_wlan0` or `set_lan.sh`, so the two
measurable-now pins must be read from the live device.

### Measured from the device 2026-11-10 — done

```sh
ssh root@192.168.2.220 'uname -r; sha256sum /bin/wifi_daemon /lib/modules/5.4.61/aic8800_fdrv.ko 2>/dev/null; ls /lib/modules'
# -> 5.4.61 ; 8e19a620… wifi_daemon ; 2973d7d7… aic8800_fdrv.ko ; /lib/modules = 5.4.61
```
Both hashes match the `NATIVE` pins and the kernel version is unchanged, so
`wifi_daemon`, `aic8800_fdrv.ko` and the `MODULES` path are confirmed correct
for 0113.

### Measured from the decrypted stock rootfs 2026-11-10 — done

The two stock files that connections overwrites on the device were measured from
the decrypted 0113 stock rootfs on the machine that holds the stock OTA and AES
key (`tools/inspect_base.py <stock.zip.sig> --aes-key keys/cc2_aes_key_v1.bin`,
then `unsquashfs` the `rootfs`):
`etc/wifi/udhcpc_wlan0 b76f68e3…`, `usr/share/udhcpc/set_lan.sh 53cf6af5…`, and
the control `opt/inst/daemon-000/daemon-000 6fed6657…` (= profile `daemon_stock`,
confirming the decryption). All three match, so **both connections stock pins are
already correct for 0113 — no change needed**, and the entire `NATIVE` set is now
verified. Only the USB-Ethernet `.ko` remain.

## Net remaining external inputs for a full, buildable 0113

1. ~~Decrypted 0113 stock rootfs~~ — **done** (above): `udhcpc_wlan0` /
   `set_lan.sh` verified, all `NATIVE` stock pins correct for 0113.
2. **Allwinner TinaLinux 5.4.61 kernel source** (build `#40`): rebuild the four
   `cc2_ue300_*.ko` — the only remaining connections blocker. Narrowed from the
   device `/proc/config.gz`: `CONFIG_MODVERSIONS` is **off**, so no
   `Module.symvers` is needed, and the kernel `.config` is available from the
   device; only the configured kernel source tree (for the `struct usb_device`
   headers matching build #40) plus the `ue300` module source are still needed.
3. **Reactor**: fork 2's 0113 reactor sources into version control + the
   qualified toolchain to rebuild the `.so` + **hardware validation of the
   active reactor**.
4. **Signing key** (maintainer) and a **hardware test of a builder-produced
   0113 image** before `ported_patch_set: true` / adding 0113 to
   `QUALIFIED_BASES`.

Never commit the reactor/USB `.so`/`.ko` or any vendor binary; profiles and
manifests hold hashes only (AGENTS.md).
