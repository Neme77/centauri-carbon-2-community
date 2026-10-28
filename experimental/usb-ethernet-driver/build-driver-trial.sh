#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
TASK=${1:?isolated build directory required}
bash "$HERE/../usb-ethernet-offline/build-offline.sh" "$TASK"
TASK=$(cd "$TASK" && pwd)
export PATH="$TASK/toolchain/bin:$PATH"
export STAGING_DIR="$TASK/toolchain"
DRIVERS="$TASK/trial-drivers"
OUT="$TASK/trial-report"
mkdir -p "$DRIVERS" "$OUT"
cp "$TASK/kernel/drivers/net/mii.c" "$DRIVERS/cc2_ue300_mii.c"
cp "$TASK/kernel/drivers/net/usb/r8152.c" "$DRIVERS/cc2_ue300_r8152.c"
python3 - "$DRIVERS" "$OUT" <<'PY'
from pathlib import Path
import difflib,sys
root,out=map(Path,sys.argv[1:])
p=root/'cc2_ue300_r8152.c';original=p.read_text();s=original
needle='\tSET_NETDEV_DEV(netdev, &intf->dev);'
assert s.count(needle)==1
s=s.replace(needle,'\t/* Keep the trial outside TinaLinux eth0 auto-configuration. */\n\tstrlcpy(netdev->name, "cc2lan%d", IFNAMSIZ);\n\n'+needle)
assert s.count('#define MODULENAME "r8152"')==1
s=s.replace('#define MODULENAME "r8152"','#define MODULENAME "cc2_ue300_r8152"')
start=s.index('static const struct usb_device_id rtl8152_table[] = {')
end=s.index('MODULE_DEVICE_TABLE(usb, rtl8152_table);',start)
s=s[:start]+'''static const struct usb_device_id rtl8152_table[] = {
\t{REALTEK_USB_DEVICE(VENDOR_ID_REALTEK, 0x8153)},
\t{}
};

'''+s[end:]
assert s.count('MODULE_VERSION(DRIVER_VERSION);')==1
s=s.replace('MODULE_VERSION(DRIVER_VERSION);','MODULE_VERSION(DRIVER_VERSION "-cc2-ue300-r1");')
p.write_text(s)
(out/'r8152-trial.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),s.splitlines(True),fromfile='a/drivers/net/usb/r8152.c',tofile='b/drivers/net/usb/r8152.c')))
p=root/'cc2_ue300_mii.c';s=p.read_text();assert 'MODULE_VERSION' not in s;p.write_text(s+'\nMODULE_VERSION("cc2-ue300-r1");\n')
(root/'Makefile').write_text('obj-m += cc2_ue300_mii.o cc2_ue300_r8152.o\n')
PY
make -C "$TASK/kernel" ARCH=arm CROSS_COMPILE=arm-openwrt-linux-gnueabi- HOSTCFLAGS='-O2 -fcommon' EXTRAVERSION= modules_prepare
make -C "$TASK/kernel" ARCH=arm CROSS_COMPILE=arm-openwrt-linux-gnueabi- HOSTCFLAGS='-O2 -fcommon' EXTRAVERSION= M="$DRIVERS" -j2 modules
cp "$DRIVERS"/{cc2_ue300_mii.ko,cc2_ue300_r8152.ko,cc2_ue300_mii.c,cc2_ue300_r8152.c,Makefile,Module.symvers} "$OUT/"
for module in cc2_ue300_mii cc2_ue300_r8152; do
    modinfo "$OUT/$module.ko" > "$OUT/$module.modinfo.txt"
    arm-openwrt-linux-gnueabi-readelf -h "$OUT/$module.ko" > "$OUT/$module.elf.txt"
    arm-openwrt-linux-gnueabi-nm -u "$OUT/$module.ko" > "$OUT/$module.undefined.txt"
done
python3 - "$OUT" <<'PY'
from pathlib import Path
import re,subprocess,sys
p=Path(sys.argv[1]);vermagic='5.4.61 SMP preempt mod_unload ARMv7 p2v8'
for module in ['cc2_ue300_mii','cc2_ue300_r8152']:
    assert subprocess.check_output(['modinfo','-F','vermagic',str(p/(module+'.ko'))],text=True).strip()==vermagic
    assert subprocess.check_output(['modinfo','-F','name',str(p/(module+'.ko'))],text=True).strip()==module
driver=str(p/'cc2_ue300_r8152.ko')
assert subprocess.check_output(['modinfo','-F','depends',driver],text=True).strip()=='cc2_ue300_mii'
aliases=subprocess.check_output(['modinfo','-F','alias',driver],text=True)
assert set((v.lower(),prod.lower()) for v,prod in re.findall(r'v([0-9A-F]{4})p([0-9A-F]{4})',aliases))=={('0bda','8153')}
assert '"cc2lan%d"' in (p/'cc2_ue300_r8152.c').read_text()
print('PASS target ARM module identities/dependency and UE300-only USB aliases; custom interface name')
PY
(cd "$OUT" && sha256sum *.ko > module-SHA256SUMS)
cp "$HERE/build-driver-trial.sh" "$OUT/"
cp "$TASK/report/config.diff" "$OUT/"
echo 'DRIVER TRIAL BUILD COMPLETE. No printer contacted or modules loaded.'
