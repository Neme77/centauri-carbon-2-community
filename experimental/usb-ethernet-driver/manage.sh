#!/bin/sh
set -eu
DIR=$(CDPATH='' cd "$(dirname "$0")" && pwd)
NET=/sys/class/net/cc2lan0
MII=cc2_ue300_mii
DRIVER=cc2_ue300_r8152
fail() { echo "STOP: $*" >&2; exit 1; }
routes() {
    awk 'NR>1 {print $1,$2,$3,$4,$7,$8,$9,$10,$11}' /proc/net/route | LC_ALL=C sort
}
owned() {
    [ ! -d "/sys/module/$1" ] || [ "$(cat "/sys/module/$1/version")" = "$2" ]
}
status() {
    grep -E '^cc2_ue300_(mii|r8152) ' /proc/modules || true
    if [ -d "$NET" ]; then
        ip link show dev cc2lan0
        ip -4 addr show dev cc2lan0
        printf 'operstate='; cat "$NET/operstate"
        printf 'flags='; cat "$NET/flags"
    else
        echo 'UE300 trial interface absent'
    fi
    echo 'IPv4 routes:'
    routes
    grep -E '^MemAvailable:' /proc/meminfo || true
}
stop_trial() {
    owned "$DRIVER" 'v1.10.11-cc2-ue300-r1' || fail 'Different driver version; refusing to unload'
    owned "$MII" 'cc2-ue300-r1' || fail 'Different MII version; refusing to unload'
    if [ -d "/sys/module/$DRIVER" ]; then rmmod "$DRIVER" || fail 'Driver unload failed; reboot manually while Idle'; fi
    if [ -d "/sys/module/$MII" ]; then rmmod "$MII" || fail 'MII unload failed; reboot manually while Idle'; fi
    echo 'TRIAL STOPPED: custom modules removed. No network configuration or startup file changed.'
}
check() {
    command -v ip >/dev/null || fail 'ip command unavailable'
    command -v awk >/dev/null || fail 'awk command unavailable'
    command -v cmp >/dev/null || fail 'cmp command unavailable'
    command -v sort >/dev/null || fail 'sort command unavailable'
    command -v insmod >/dev/null || fail 'insmod unavailable'
    command -v rmmod >/dev/null || fail 'rmmod unavailable'
    [ ! -d /sys/module/mii ] || fail 'A different MII module is already loaded'
    [ ! -d "/sys/module/$MII" ] || fail 'Trial MII already loaded; use status or stop'
    [ ! -d "/sys/module/$DRIVER" ] || fail 'Trial driver already loaded; use status or stop'
    [ ! -d "$NET" ] || fail 'cc2lan0 already exists'
    sh "$DIR/abi-probe.sh" check
    (cd "$DIR" && sha256sum -c network.sha256) || fail 'Network configuration/hooks differ from reviewed files'
    for path in /etc/hotplug.d/net/*; do
        [ ! -f "$path" ] || [ "$path" = /etc/hotplug.d/net/00-sysctl ] || fail 'Unreviewed net hotplug script'
    done
    for path in /etc/hotplug.d/iface/*; do
        [ ! -f "$path" ] || [ "$path" = /etc/hotplug.d/iface/00-netstate ] || fail 'Unreviewed iface hotplug script'
    done
    if grep -q cc2lan /etc/config/network; then fail 'cc2lan is already configured'; fi
    for path in /etc/sysctl.conf /etc/sysctl.d/*.conf; do
        if [ -f "$path" ] && grep -q cc2lan "$path"; then fail 'cc2lan has custom sysctl rules'; fi
    done
    ip -4 addr show dev wlan0 >/dev/null || fail 'Cannot inspect current Wi-Fi addresses'
    echo 'NETWORK CHECK PASS: reviewed configuration/hooks, unconfigured cc2lan name. No modules loaded or network changes.'
}
case "${1:-check}" in
    check) check; exit 0;;
    status) status; exit 0;;
    stop) stop_trial; exit 0;;
    start) ;;
    *) fail 'Use check, start, status or stop';;
esac
check
# Verify the running kernel before binding any hardware.
sh "$DIR/abi-probe.sh" probe || fail 'Preliminary ABI probe failed'
routes > "$DIR/.routes-before"
own_mii=0
own_driver=0
keep=0
cleanup() {
    if [ "$keep" = 0 ]; then
        if [ "$own_driver" = 1 ]; then rmmod "$DRIVER" || echo 'Driver cleanup failed; reboot manually while Idle' >&2; fi
        if [ "$own_mii" = 1 ]; then rmmod "$MII" || echo 'MII cleanup failed; reboot manually while Idle' >&2; fi
    fi
}
trap cleanup 0
trap 'exit 1' 1 2 15
echo 'EXPERIMENTAL: bind only the qualified UE300 as cc2lan0. No IP assignment or interface-up command.'
insmod "$DIR/$MII.ko" || fail 'Trial MII load failed'
own_mii=1
if insmod "$DIR/$DRIVER.ko"; then own_driver=1; else dmesg | tail -n 25; fail 'Trial driver load failed'; fi
attempt=0
while [ ! -d "$NET" ] && [ "$attempt" -lt 10 ]; do
    sleep 1
    attempt=$((attempt + 1))
done
if [ ! -d "$NET" ]; then dmesg | tail -n 30; fail 'UE300 did not create cc2lan0; removing trial modules'; fi
bound=$(readlink "$NET/device/driver/module") || fail 'Cannot identify interface driver'
[ "${bound##*/}" = "$DRIVER" ] || fail 'Interface belongs to another driver'
flags=$(cat "$NET/flags")
[ $((flags & 1)) -eq 0 ] || fail 'Unexpected interface activation; removing trial modules'
addresses=$(ip -4 addr show dev cc2lan0) || fail 'Cannot inspect interface addresses'
if printf '%s\n' "$addresses" | grep -q 'inet '; then fail 'Unexpected IPv4 assignment; removing trial modules'; fi
routes > "$DIR/.routes-after"
cmp -s "$DIR/.routes-before" "$DIR/.routes-after" || fail 'Routes changed during binding; removing trial modules'
(cd "$DIR" && sha256sum -c network.sha256) || fail 'Network files changed during binding; removing trial modules'
status
keep=1
echo 'TRIAL STARTED: cc2lan0 exists and remains DOWN without IPv4. Existing IPv4 routes unchanged.'
echo 'Modules are temporary. Stop: sh /tmp/cc2-usb-lan-ue300-r1/manage.sh stop'
