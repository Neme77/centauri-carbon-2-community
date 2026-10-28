#!/bin/sh
set -eu
DIR=$(CDPATH='' cd "$(dirname "$0")" && pwd)
fail() { echo "STOP: $*" >&2; exit 1; }
case "${1:-check}" in check|probe) ;; *) fail 'Use check or probe';; esac
[ "$(uname -m)" = armv7l ] || fail 'Not the qualified ARMv7 printer'
[ "$(uname -r)" = 5.4.61 ] || fail 'Kernel release differs'
[ "$(cat /proc/sys/kernel/modules_disabled)" = 0 ] || fail 'Module loading disabled'
[ ! -d /sys/module/r8152 ] || fail 'An Ethernet driver is already loaded'
[ ! -d /sys/module/cc2_ue300_abi_probe ] || fail 'Probe module already loaded'
(cd "$DIR" && sha256sum -c SHA256SUMS) || fail 'Payload checksum mismatch'
config=$(zcat /proc/config.gz | sha256sum)
[ "${config%% *}" = 8fa0a87f203d69cb9c4e027e8db11a9a0d92c5d2ea8ae83f87b34714aae7ec4a ] || fail 'Kernel configuration differs'
wifi=$(sha256sum /lib/modules/5.4.61/aic8800_fdrv.ko)
[ "${wifi%% *}" = 2973d7d7856b612ed5880fefa5b8f339d5bd882ff93f44ca47806c83a8a8d8c7 ] || fail 'Original Wi-Fi driver differs'
found=0
for file in /sys/bus/usb/devices/*/idVendor; do
    [ -f "$file" ] || continue
    device=${file%/idVendor}
    if [ "$(cat "$file")" = 0bda ] && [ "$(cat "$device/idProduct")" = 8153 ] && [ "$(cat "$device/bcdDevice")" = 31fd ]; then found=1; fi
done
[ "$found" = 1 ] || fail 'Expected UE300 USB ID/revision not found'
printer=$(wget -T 5 -qO- http://127.0.0.1:8081/api/printer) || fail 'CC2 Control status unavailable'
printf '%s' "$printer" | grep -q '"connected":true' || fail 'Printer disconnected'
printf '%s' "$printer" | grep -q '"machine":{"status":1,' || fail 'Printer is not Idle'
age=$(printf '%s' "$printer" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\).*/\1/p')
if [ -z "$age" ] || [ "$age" -gt 15 ]; then fail 'Stale printer status'; fi
printf '%s' "$printer" | grep -Eq '"extruder":\{[^}]*"target":0(\.0+)?[,}]' || fail 'Nozzle heating target is not zero'
printf '%s' "$printer" | grep -Eq '"heater_bed":\{[^}]*"target":0(\.0+)?[,}]' || fail 'Bed heating target is not zero'
echo 'CHECK PASS: payload, kernel/config, original Wi-Fi, UE300 and fresh Idle state. No module loaded.'
[ "${1:-check}" = probe ] || exit 0
command -v insmod >/dev/null || fail 'insmod unavailable'
command -v rmmod >/dev/null || fail 'rmmod unavailable'
loaded=0
cleanup() {
    if [ "$loaded" = 1 ]; then
        if rmmod cc2_ue300_abi_probe; then loaded=0; else echo 'Probe unload failed; reboot manually while Idle.' >&2; fi
    fi
}
trap cleanup 0
trap 'exit 1' 1 2 15
echo 'EXPERIMENTAL kernel probe. Source differs; incompatibility may require a manual reboot. No automatic reboot.'
if insmod "$DIR/cc2_ue300_abi_probe.ko"; then
    loaded=1
else
    dmesg | tail -n 25
    fail 'Probe rejected or runtime layout check failed; no Ethernet driver loaded'
fi
dmesg | grep 'CC2_UE300_ABI_R1' | tail -n 5
rmmod cc2_ue300_abi_probe || fail 'Cannot unload probe; reboot manually while Idle'
loaded=0
echo 'PROBE PASS: core symbol resolution and checked allocation/USB layouts. Probe unloaded. Ethernet remains inactive.'
echo 'This does not prove full r8152 compatibility; do not load the Ethernet analysis modules.'
