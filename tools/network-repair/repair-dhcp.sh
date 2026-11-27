#!/bin/sh
# Manual repair of one recognised startup mismatch. Never restart networking.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
SOURCE=/opt/inst/cc2-connections
ROOT=/opt/usr/cc2-network-manager
DHCP=/opt/usr/wifi/udhcpc_wlan0
RFID=/opt/usr/cc2-rfid
BASE=/opt/usr
API=http://127.0.0.1:8081
ACTION=${1:---check}
case "$ACTION" in --check|--apply) ;; *) echo 'STOP: use --check or --apply' >&2; exit 2;; esac
fail(){ echo "STOP: $1; retain audit and backup" >&2; exit 125; }
safe(){
    (
        path=$1
        while [ "$path" != / ]; do
            [ ! -L "$path" ] || exit 1
            path=${path%/*}; [ -n "$path" ] || path=/
        done
    )
}
regular(){ safe "$1" && [ -f "$1" ]; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
expect(){ regular "$1" && [ "$(sha "$1")" = "$2" ] || fail "identity:$1"; }
OLD=b76f68e345f1221d835d1a0d6279827c5df4823c2e7fadfe0298cd49ec7bb7df
NEW=8061ab23e446c3cee49b6148b01eee098e04cfb4e70c271306f16832dd4cb56f
MANAGER=136ac6f5081b733596cbf9e1518920e1e6a64612d35f2e3547cda9ec36289a65
STARTUP=1b4aeec37ceb49b27f0ea2f6aaba264c4a8a236ce36813db060efaef90c9d380
[ "$(id -u)" = 0 ] || fail root_required
expect "$SOURCE/SHA256SUMS" 7d166a1df548e0efbb0c7330facbb46d6ae7b9c31dd002913d0d3ee6284a22c3
(cd "$SOURCE" && sha256sum -c SHA256SUMS >/dev/null) || fail firmware_source_changed
expect "$SOURCE/control" "$MANAGER"
expect "$ROOT/control" "$MANAGER"
expect "$SOURCE/wifi-client.sh" "$NEW"
expect "$ROOT/startup.sha256" "$STARTUP"
regular "$DHCP" || fail dhcp_type
# No audit, backup, permission or state changes for an already working profile.
if [ "$(sha "$DHCP")" = "$NEW" ]; then
    sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail another_startup_mismatch
    echo DHCP_ALREADY_ALIGNED_NO_CHANGES
    exit 0
fi
expect "$DHCP" "$OLD"
for path in "$ROOT" "$BASE" "$RFID"; do
    safe "$path" && [ -d "$path" ] || fail directory_type
done
# Read application guards only. No coordinator, manager or recovery worker run.
manager_idle(){
    status=$(wget -qO- "$API/api/network/manager") || fail manager_status_unavailable
    for field in '"state_valid":true' '"ready":true' '"busy":false' '"phase":"idle"' '"pending":null'; do
        printf '%s' "$status" | tr -d '\r\n\t ' | grep -F "$field" >/dev/null || fail manager_not_idle_and_ready
    done
}
manager_idle
audit=$(mktemp -d "$BASE/cc2-network-dhcp-repair.XXXXXX") || fail audit_directory
echo "AUDIT=$audit"
replacement=
armed=0
finish(){
    code=$?
    trap - 0 1 2 15
    if [ "$code" != 0 ] && [ "$armed" = 1 ]; then
        if regular "$DHCP" && [ "$(sha "$DHCP")" = "$NEW" ] &&
           regular "$audit/dhcp.original" && [ "$(sha "$audit/dhcp.original")" = "$OLD" ]; then
            rollback=$(mktemp "${DHCP%/*}/.cc2-dhcp-rollback.XXXXXX") &&
            cp -p "$audit/dhcp.original" "$rollback" && mv -f "$rollback" "$DHCP" && sync ||
                echo 'STOP: DHCP_RESTORE_FAILED; retain all reports' >&2
        else
            echo 'STOP: DHCP_CHANGED_EXTERNALLY; no overwrite attempted' >&2
        fi
    fi
    [ -z "$replacement" ] || [ ! -f "$replacement" ] || rm -f "$replacement"
    exit "$code"
}
trap finish 0
trap 'exit 125' 1 2 15
awk '$2!="/opt/usr/wifi/udhcpc_wlan0" {print}' "$ROOT/startup.sha256" > "$audit/other-startup.sha256"
sha256sum -c "$audit/other-startup.sha256" >/dev/null || fail another_startup_mismatch
find "$RFID" -type l > "$audit/rfid.links" || fail rfid_inventory
[ ! -s "$audit/rfid.links" ] || fail rfid_symlink
find "$RFID" -type f > "$audit/rfid.unsorted" || fail rfid_inventory
LC_ALL=C sort "$audit/rfid.unsorted" > "$audit/rfid.files"
[ -s "$audit/rfid.files" ] || fail rfid_inventory_empty
: > "$audit/rfid.before"
while IFS= read -r path; do
    case "$path" in *[!A-Za-z0-9_./-]*) fail rfid_path;; esac
    regular "$path" || fail rfid_type
    sha256sum "$path" >> "$audit/rfid.before" || fail rfid_read
done < "$audit/rfid.files"
rfid_unchanged(){
    find "$RFID" -type l > "$audit/rfid.after.links" || fail rfid_inventory
    [ ! -s "$audit/rfid.after.links" ] || fail rfid_symlink
    find "$RFID" -type f > "$audit/rfid.after.unsorted" || fail rfid_inventory
    LC_ALL=C sort "$audit/rfid.after.unsorted" > "$audit/rfid.after.files"
    [ "$(sha "$audit/rfid.files")" = "$(sha "$audit/rfid.after.files")" ] || fail rfid_inventory_changed
    sha256sum -c "$audit/rfid.before" >/dev/null || fail rfid_changed
}
if [ "$ACTION" = --check ]; then
    rfid_unchanged
    echo RFID_FILES_UNCHANGED
    echo DHCP_REPAIR_READ_ONLY_PASSED
    exit 0
fi
cp -p "$DHCP" "$audit/dhcp.original"
expect "$audit/dhcp.original" "$OLD"
replacement=$(mktemp "${DHCP%/*}/.cc2-dhcp-repair.XXXXXX") || fail dhcp_stage
cp "$SOURCE/wifi-client.sh" "$replacement"
chmod 755 "$replacement"
expect "$replacement" "$NEW"
# Repeat the admission immediately before the only active file replacement.
expect "$DHCP" "$OLD"
sha256sum -c "$audit/other-startup.sha256" >/dev/null || fail another_startup_mismatch
rfid_unchanged
manager_idle
armed=1
mv -f "$replacement" "$DHCP"
replacement=
sync
expect "$DHCP" "$NEW"
sha256sum -c "$ROOT/startup.sha256" >/dev/null || fail startup_after_repair
rfid_unchanged
armed=0
echo RFID_FILES_UNCHANGED
echo DHCP_DELEGATE_RESTORED
echo 'No service restarted; no reboot requested. Retain audit/dhcp.original.'
