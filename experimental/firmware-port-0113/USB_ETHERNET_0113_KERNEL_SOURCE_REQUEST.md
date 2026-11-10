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

## Request template

Fill in `[Your name]` / `[Your contact details / order number]` and send via
ELEGOO's official support or open-source contact channel (the address is on
ELEGOO's support / open-source page). Do not invent a recipient address.

> Subject: GPLv2 source code request — Centauri Carbon 2 firmware 02.01.01.13 (Linux kernel)
>
> Hello ELEGOO Open Source / Support Team,
>
> I own an ELEGOO Centauri Carbon 2 3D printer running firmware version
> **02.01.01.13**. This firmware includes the Linux kernel (version **5.4.61**,
> Allwinner TinaLinux) and other components licensed under the GNU General Public
> License, version 2. Under GPLv2 section 3, the distribution of these binaries
> carries an obligation to provide the complete corresponding source code on
> request. I am writing to request that source.
>
> Specifically, for firmware **02.01.01.13** as shipped on the Centauri Carbon 2,
> please provide:
>
> 1. The complete corresponding source code of the Linux kernel **5.4.61** as
>    built for this firmware, including all ELEGOO/Allwinner patches applied on
>    top of the upstream kernel.
> 2. The exact kernel configuration (`.config`) used to build it.
> 3. The build scripts and the toolchain details (compiler and version) needed to
>    build the kernel and **loadable kernel modules** that are binary-compatible
>    with the deployed kernel.
> 4. The source code of any other GPL- or LGPL-licensed components distributed in
>    this firmware.
>
> Context (to help you locate the exact build): the deployed kernel in this
> firmware differs at the binary/ABI level from the publicly available 5.4.61
> tree for earlier firmware, so the earlier source release does not correspond to
> 02.01.01.13. A download link to an archive, or a written offer per GPLv2
> section 3(b), would both be fine.
>
> Thank you for your help and for honoring the open-source licenses in your
> products. Please let me know if you need the printer serial number, order
> number, or any additional detail to identify the exact firmware build.
>
> Best regards,
> [Your name]
> [Your contact details / order number]

## When the source arrives

Build the four modules from the matching tree with the captured `.config` and the
GCC 6.4.1 SDK (`experimental/network-connections/abi-probe/`), re-run `abi_probe`
against the 0113 kernel, pin the rebuilt `.ko` in `connections_component.py`
(`BINARY_PINS`), and flip `components.connections_usb_ethernet` to `included:
true` (or remove the entry) in the 0113 profile.
