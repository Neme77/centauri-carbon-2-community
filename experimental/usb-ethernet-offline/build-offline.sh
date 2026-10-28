#!/usr/bin/env bash
set -euo pipefail
# Offline analysis only: retain candidate release suffix and never load modules.
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
TASK=${1:-"$ROOT/offline-build"}
mkdir -p "$TASK"
TASK=$(cd "$TASK" && pwd)
KERNEL=cd4ee0cf38af4f4b21f2918e2ad9f0627fdfde57
TOOLCHAIN=f4210c9d613a5e1cb4fc07a1b03bc35ab332f596
mkdir -p "$TASK/kernel" "$TASK/toolchain" "$TASK/report"
for pair in "oc-linux:$KERNEL:kernel" "arm-openwrt-linux-gnueabi-gcc:$TOOLCHAIN:toolchain"; do
  IFS=: read -r repo commit target <<< "$pair"
  curl --fail --location --retry 3 "https://codeload.github.com/OpenCentauri/$repo/tar.gz/$commit" -o "$TASK/$target.tar.gz"
  sha256sum "$TASK/$target.tar.gz" >> "$TASK/report/download-SHA256SUMS"
  tar -xzf "$TASK/$target.tar.gz" -C "$TASK/$target" --strip-components=1
 done
cd "$TASK/toolchain"
bash ./00_repair_symlink
bash ./01_make_executable > "$TASK/report/toolchain-permissions.txt"
export PATH="$TASK/toolchain/bin:$PATH"
export STAGING_DIR="$TASK/toolchain"
CROSS=arm-openwrt-linux-gnueabi-
"${CROSS}gcc" --version | tee "$TASK/report/compiler.txt"
test "$("${CROSS}gcc" -dumpversion)" = 6.4.1
cd "$TASK/kernel"
cp "$ROOT/printer.config" .config
scripts/config --enable USB_NET_DRIVERS --module MII --module USB_RTL8152
make ARCH=arm CROSS_COMPILE="$CROSS" HOSTCFLAGS='-O2 -fcommon' olddefconfig
cp .config "$TASK/report/build.config"
cp "$ROOT/printer.config" "$TASK/report/printer.config"
scripts/diffconfig "$ROOT/printer.config" .config > "$TASK/report/config.diff"
make ARCH=arm CROSS_COMPILE="$CROSS" HOSTCFLAGS='-O2 -fcommon' -j2 modules_prepare
make -s ARCH=arm CROSS_COMPILE="$CROSS" kernelrelease | tee "$TASK/report/kernelrelease.txt"
# Do not overwrite the release string just to make the printer accept a module.
grep -qx '5.4.61-ab1175' "$TASK/report/kernelrelease.txt"
make ARCH=arm CROSS_COMPILE="$CROSS" HOSTCFLAGS='-O2 -fcommon' -j2 drivers/net/mii.ko drivers/net/usb/r8152.ko
for module in drivers/net/mii.ko drivers/net/usb/r8152.ko; do
  name=$(basename "$module")
  cp "$module" "$TASK/report/$name.UNQUALIFIED"
  "${CROSS}readelf" -h "$module" > "$TASK/report/$name.elf.txt"
  "${CROSS}readelf" -p .modinfo "$module" > "$TASK/report/$name.modinfo.txt"
  "${CROSS}nm" -u "$module" | awk '{print $NF}' | sort -u > "$TASK/report/$name.undefined.txt"
  "${CROSS}readelf" --debug-dump=info "$module" > "$TASK/report/$name.dwarf.txt"
  modinfo "$module" > "$TASK/report/$name.modinfo-full.txt"
done
cp drivers/net/usb/r8152.c drivers/net/mii.c "$TASK/report/"
cp COPYING "$TASK/report/KERNEL-COPYING"
cp "$ROOT/build-offline.sh" "$TASK/report/"
printf '%s\n' 'OFFLINE ANALYSIS ONLY. NOT QUALIFIED FOR THE PRINTER.' 'Candidate source release: 5.4.61-ab1175; printer: 5.4.61.' 'Matching version text or symbol names does not establish ABI compatibility.' 'No load/install script is provided. Do not insmod these files.' 'Source https://github.com/OpenCentauri/oc-linux/tree/'"$KERNEL" 'Toolchain https://github.com/OpenCentauri/arm-openwrt-linux-gnueabi-gcc/tree/'"$TOOLCHAIN" > "$TASK/report/README.txt"
(cd "$TASK/report" && sha256sum *.UNQUALIFIED > module-SHA256SUMS)
echo 'OFFLINE BUILD COMPLETE. No module loaded or printer contacted.'
