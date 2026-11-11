# Revision 3000 preliminary ABI probe

GPL-2.0, derived from the maintainer's existing R1 probe supplied with the
published R5h runtime. The expected USB device revision changes from `31fd` to
`3000`; messages and the module name distinguish the two. All net_device,
sk_buff, module size/offset, allocation and 112 symbol checks are retained.
There is no driver registration, hardware write, module parameter or additional
parameter-handler dependency. The original 31fd probe and Ethernet driver are
reused unchanged. The candidate build compares both probes' undefined symbols.

`build.py OUTPUT` verifies and uses the same public kernel and GCC SDK inputs:

- OpenCentauri/oc-linux at `cd4ee0cf38af4f4b21f2918e2ad9f0627fdfde57`.
- OpenCentauri/arm-openwrt-linux-gnueabi-gcc at `f4210c9d613a5e1cb4fc07a1b03bc35ab332f596` (GCC 6.4.1).

The source kernel originally has suffix `-ab1175`, unlike the deployed 5.4.61
kernel. As in R1, the build targets `EXTRAVERSION=` and preserves the independent
layout/dependency checks. A matching release string is not proof of ABI safety.
Only debug information is stripped from the new output; no module is loaded by
the build. This preliminary probe is not full driver or revision qualification.
