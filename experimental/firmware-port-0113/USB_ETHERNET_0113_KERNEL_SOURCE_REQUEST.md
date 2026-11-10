# USB-Ethernet on 02.01.01.13 — kernel source request (GPLv2)

## Why this is needed

The Connections USB-Ethernet support ships four out-of-tree kernel modules
(`cc2_ue300_abi_probe.ko`, `cc2_ue300_abi_probe_3000.ko`, `cc2_ue300_mii.ko`,
`cc2_ue300_r8152.ko`). They are built against the vendor kernel ABI. On base
02.01.01.13 the deployed kernel is Linux **5.4.61, Allwinner TinaLinux build
`#40`** (firmware image `bf965943…`), which differs at the binary/ABI level from
the public 07-era tree (OpenCentauri/oc-linux `cd4ee0cf`, kernel suffix
`-ab1175`). `abi_probe` reports a `struct usb_device` layout mismatch, so the
07-built modules cannot load on 0113. A matching kernel-release string is **not**
proof of ABI safety.

Because the exact 0113 kernel source is not publicly available, USB-Ethernet is
declared **not-included** for 02.01.01.13 (`base_profiles/02.01.01.13.json`,
`components.connections_usb_ethernet`; see `PIPELINE_MULTI_BASE_DESIGN.md`
section 3.5). The builder skips the modules and asserts their absence; native
Wi-Fi Connections (the stock `aic8800_fdrv.ko`) ships normally. This keeps
USB-Ethernet off the release's critical path — it is a later addition, not a
blocker.

To build and qualify the four modules for 0113 we need the **complete
corresponding source** of the deployed kernel, which ELEGOO is obligated to
provide under GPLv2 section 3. The device kernel `.config` is already captured at
`experimental/network-connections/abi-probe/printer.config`.

## What to request

- The complete corresponding source of Linux **5.4.61** as built for firmware
  02.01.01.13, including all ELEGOO/Allwinner patches.
- The exact kernel configuration (`.config`) used for that build.
- The build scripts and toolchain (compiler + version) needed to build the kernel
  and **loadable modules** binary-compatible with the deployed kernel.
- Source for any other GPL/LGPL components shipped in the firmware.

`CONFIG_MODVERSIONS` is off in the captured config, so no `Module.symvers` is
required for module builds — a matching source tree plus the `.config` is enough.

## Where to request

ELEGOO's open-source materials for this printer live in the official
[`elegooofficial/CentauriCarbon2`](https://github.com/elegooofficial/CentauriCarbon2)
repository, which currently publishes the printer application layer and the MCU
firmware — not the Linux kernel or the rest of the root filesystem. GPLv3
compliance for the CC2 firmware is tracked there in issue
[#8 "Out of compliance with GPLv3"](https://github.com/elegooofficial/CentauriCarbon2/issues/8),
where an ELEGOO maintainer has acknowledged the request. The concrete,
version-pinned ask above — kernel 5.4.61 source + `.config` + toolchain for
firmware 02.01.01.13, with the ABI-mismatch rationale — has been raised in that
issue. Follow up in that thread rather than by email; do not invent a contact
address.

## When the source arrives

Build the four modules from the matching tree with the captured `.config` and the
GCC 6.4.1 SDK (`experimental/network-connections/abi-probe/`), re-run `abi_probe`
against the 0113 kernel, pin the rebuilt `.ko` in `connections_component.py`
(`BINARY_PINS`), and flip `components.connections_usb_ethernet` to `included:
true` (or remove the entry) in the 0113 profile.
