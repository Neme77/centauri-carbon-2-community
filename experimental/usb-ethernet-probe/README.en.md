# UE300 preliminary ABI probe R1

This is a kernel probe, not an Ethernet driver. No mii/r8152 driver is included.

The probe resolves the core symbol dependencies, temporarily allocates/frees network objects and checks selected net_device, sk_buff and existing USB descriptor fields. It registers no network interface, sends no USB/network commands and changes no addresses, routes, services or startup files. The wrapper unloads the probe after the test.

Candidate source OpenCentauri/oc-linux cd4ee0cf38af4f4b21f2918e2ad9f0627fdfde57 reports 5.4.61-ab1175; this is not an exact ELEGOO source match. The probe targets observed printer release 5.4.61 with an empty EXTRAVERSION build override. Matching release text does not prove ABI compatibility. No forced loading or binary version editing is used.

Observed original Wi-Fi module offsets are preliminary evidence only. An incompatible kernel probe can still hang the system. Run only while Idle, with both heater targets at zero and Wi-Fi retained. Manual reboot may be necessary; no automatic reboot is performed.

Extract into /tmp/cc2-usb-lan-abi-probe-r1. Run `sh /tmp/cc2-usb-lan-abi-probe-r1/probe.sh check` first (no module loaded). After checking the result, `sh /tmp/cc2-usb-lan-abi-probe-r1/probe.sh probe` loads only this probe and unloads it afterwards.

PROBE PASS covers only symbol resolution and the selected checked fields. It does not qualify every ABI detail, the r8152 driver or the adapter. Do not load earlier analysis-only modules.

Toolchain: OpenCentauri/arm-openwrt-linux-gnueabi-gcc f4210c9d613a5e1cb4fc07a1b03bc35ab332f596, GCC 6.4.1. Reproducible build source: Neme77/centauri-carbon-2-community branch test/usb-ethernet-ue300-probe-r1. The user-provided vendor module and kallsyms stay local.

Probe license GPL-2.0. Source and Makefile included; the upstream kernel/toolchain retain their authors and licenses.
