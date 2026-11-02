#!/bin/sh
# One boot-time file migration. No insmod, heater/motion command, service stop
# or native process restart is permitted in this bootstrap.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
SOURCE=/opt/inst/cc2-connections
ROOT=/opt/usr/cc2-network-manager
DHCP=/opt/usr/wifi/udhcpc_wlan0
STAGE=/opt/usr/.cc2-firmware-connections-$$
NEW_DHCP=/opt/usr/wifi/.cc2-firmware-dhcp-$$
LOCK=/tmp/cc2-firmware-connections.lock
BACKUP=
MOVED=0
REPLACED=0
DHCP_REPLACED=0
sha(){ sha256sum "$1" | awk '{print $1}'; }
fail(){ printf 'CC2_CONNECTIONS_FIRMWARE_STOP=%s\n' "$1" >&2;exit 1; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
finish(){
    code=$?
    trap - 0 1 2 15
    if [ "$code" != 0 ] && [ -n "$BACKUP" ];then
        if [ "$DHCP_REPLACED" = 1 ];then
            cp -p "$BACKUP/dhcp" "$DHCP" || printf 'DHCP_RESTORE_FAILED\n' >&2
        fi
        if [ "$REPLACED" = 1 ];then
            mv "$ROOT" "$BACKUP/failed-component" || printf 'COMPONENT_RETAIN_FAILED\n' >&2
        fi
        if [ "$MOVED" = 1 ] && [ ! -e "$ROOT" ] && [ ! -L "$ROOT" ];then
            mv "$BACKUP/component" "$ROOT" || printf 'COMPONENT_RESTORE_FAILED\n' >&2
        fi
    fi
    rmdir "$LOCK" 2>/dev/null || true
    exit "$code"
}
[ "$(id -u)" = 0 ] || fail root_required
[ -d "$SOURCE" ] && [ ! -L "$SOURCE" ] || fail firmware_source
(cd "$SOURCE" && sha256sum -c SHA256SUMS >/dev/null) || fail firmware_checksums
WAIT=0
while ! grep -q ' /opt/usr ' /proc/mounts;do
    [ "$WAIT" -lt 30 ] || fail persistent_mount
    sleep 1;WAIT=$((WAIT+1))
done
[ -d /opt/usr ] && [ ! -L /opt/usr ] && [ -d /opt/usr/wifi ] && [ ! -L /opt/usr/wifi ] || fail persistent_directory
mkdir "$LOCK" || fail bootstrap_already_running
trap finish 0
trap 'exit 1' 1 2 15
PAYLOAD=$(sha "$SOURCE/SHA256SUMS")
if regular "$ROOT/firmware-payload.sha256" && [ "$(cat "$ROOT/firmware-payload.sha256")" = "$PAYLOAD" ];then
    [ -d "$ROOT" ] && [ ! -L "$ROOT" ] || fail component_type
    (cd "$ROOT" && sha256sum -c SHA256SUMS >/dev/null) || fail installed_checksums
    sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail changed_startup
    exit 0
fi
regular "$DHCP" || fail persistent_dhcp_type
actual=$(sha "$DHCP")
case "$actual" in
    b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df|18d530dca2e9139434a9e9ff614353b7ffed5137f59271b9712da5f91c1a1608) ;;
    *) [ "$actual" = "$(sha "$SOURCE/wifi-client.sh")" ] || fail foreign_persistent_dhcp;;
esac
if [ -e "$ROOT" ] || [ -L "$ROOT" ];then
    [ -d "$ROOT" ] && [ ! -L "$ROOT" ] || fail retained_component_type
    [ -z "$(find "$ROOT" -type l -print)" ] || fail retained_component_symlink
    regular "$ROOT/SHA256SUMS" || fail retained_component_manifest
    (cd "$ROOT" && sha256sum -c SHA256SUMS >/dev/null) || fail retained_component_checksum
    for item in 'native-udhcpc_wlan0 b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df' 'native-set_lan.sh 53cf6af55ad333d5781b0d48d89fd48bde22bc516bd22e76d45f098dc224443c';do
        file="$ROOT/${item%% *}";expected=${item#* }
        regular "$file" && [ "$(sha "$file")" = "$expected" ] || fail retained_native_profile
    done
fi
for process in /proc/[0-9]*;do
    [ -d "$process" ] || continue
    executable=$(readlink "$process/exe" 2>/dev/null) || executable=
    case "$executable" in "$ROOT"/*) fail component_running;;esac
    if grep -F "$ROOT/" "$process/maps" >/dev/null 2>&1;then fail component_mapped;fi
done
if pidof ec-eeb001-gui elegoo_printer ai_camera >/dev/null 2>&1;then
    fail native_services_already_started
fi
[ ! -e "$STAGE" ] && [ ! -L "$STAGE" ] && [ ! -e "$NEW_DHCP" ] && [ ! -L "$NEW_DHCP" ] || fail stage_collision
mkdir "$STAGE"
cp -a "$SOURCE/." "$STAGE/" || fail stage_copy
(cd "$STAGE" && sha256sum -c SHA256SUMS >/dev/null) || fail staged_checksums
cp "$SOURCE/wifi-client.sh" "$NEW_DHCP" && chmod 755 "$NEW_DHCP" || fail dhcp_stage
BACKUP=/opt/usr/cc2-network-firmware-backup-$(date +%Y%m%d-%H%M%S)-$$
mkdir "$BACKUP" || fail backup_collision
cp -p "$DHCP" "$BACKUP/dhcp" || fail dhcp_backup
if [ -d "$ROOT" ];then mv "$ROOT" "$BACKUP/component" || fail component_backup;MOVED=1;fi
mv "$STAGE" "$ROOT" || fail component_commit
REPLACED=1
mv "$NEW_DHCP" "$DHCP" || fail dhcp_commit
DHCP_REPLACED=1
sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail startup_profile
"$ROOT/control" --seed || fail configuration_seed
printf '%s\n' "$PAYLOAD" > "$ROOT/firmware-payload.sha256"
# Committed. The existing network worker owns all later connection changes.
MOVED=0;REPLACED=0;DHCP_REPLACED=0
printf 'CC2_CONNECTIONS_FIRMWARE_READY: settings preserved; kernel/modules unchanged\n'
