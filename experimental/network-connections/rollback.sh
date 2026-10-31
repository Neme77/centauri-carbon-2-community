#!/bin/sh
# This script is copied into a root-owned per-installation backup directory.
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
case "$BASE" in /opt/usr/cc2-network-backup-[0-9]*-[0-9]*) ;;*) echo 'STOP: unexpected backup path';exit 1;;esac
[ -d "$BASE/files" ] && [ ! -L "$BASE/files" ] || exit 1
ROOT=/opt/usr/cc2-network-manager
if [ "${1:-}" != --installer ];then
    "$ROOT/control" --guard 8081 || exit 1
    "$ROOT/control" --busy && { echo 'STOP: confirm or cancel the pending connection first';exit 1; }
fi
for path in /etc/init.d/printer /opt/usr/wifi/udhcpc_wlan0 /etc/wifi/udhcpc_wlan0 /usr/share/udhcpc/set_lan.sh /etc/init.d/cc2-camera-launch /etc/init.d/cc2-reactor-launch /etc/init.d/cc2-network-manager /etc/rc.d/S23cc2-network-manager /etc/hotplug.d/net/90-cc2-network-manager;do
    if [ -e "$BASE/files$path" ] || [ -L "$BASE/files$path" ];then
        rm -f "$path";cp -a "$BASE/files$path" "$path"
    elif [ -f "$BASE/absent" ] && grep -Fx "$path" "$BASE/absent" >/dev/null;then rm -f "$path"
    else echo 'STOP: incomplete backup';exit 1;fi
done
if [ -d "$ROOT" ] && [ ! -L "$ROOT" ];then mv "$ROOT" "$BASE/failed-component-$$";fi
if [ -d "$BASE/component" ] && [ ! -L "$BASE/component" ];then mv "$BASE/component" "$ROOT";fi
sync
echo 'Startup restored. No service restarted. Reboot the complete idle printer to finish removing active network delegates.'
