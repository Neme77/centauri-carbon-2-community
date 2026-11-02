#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Local native display refresh: on-disk files only, no signals or automatic reboot.
set -eu
umask 077
BASE=/opt/usr/cc2-native-display
GUI=/opt/bin/ec-eeb001-gui
IMAGES=/opt/usr/images/theme_default
STATE=$BASE/state
LOCK=$STATE/lock
PHASE=initial
HELD=0
CHANGED=0
BACKUP=
fail(){ echo "STOP: $*" >&2; exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
target(){
    case "$1" in
        gui/ec-eeb001-gui) printf '%s\n' "$GUI";;
        images/*) tail=${1#images/};case "$tail" in ''|*/*|*..*|*[!a-zA-Z0-9_.-]*) return 1;;esac;printf '%s/%s\n' "$IMAGES" "$tail";;
        *) return 1;;
    esac
}
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
http_get() (
    umask 077
    file=$LOCK/http.$$
    [ ! -e "$file" ] && [ ! -L "$file" ] || exit 1
    reader=
    cleanup(){ trap - 0 1 2 15;if [ -n "$reader" ];then kill -KILL "$reader" 2>/dev/null || true;wait "$reader" 2>/dev/null || true;fi;rm -f "$file"; }
    trap cleanup 0
    trap 'exit 130' 1 2 15
    wget -qO- "$1" >"$file" 2>/dev/null &
    reader=$!
    seconds=0
    while kill -0 "$reader" 2>/dev/null;do
        [ "$seconds" -lt 5 ] || exit 124
        sleep 1;seconds=$((seconds+1))
    done
    if wait "$reader";then reader=;cat "$file";else reader=;exit 1;fi
)
idle(){
    attempt=0
    while [ "$attempt" -lt 15 ];do
        data=$(http_get http://127.0.0.1:8081/api/printer) || fail printer_status_unavailable
        age=$(printf '%s\n' "$data" | awk -F'"last_message_age":' 'NF==2 {split($2,a,",");print a[1]}')
        case "$age" in ''|*[!0-9]*) fail invalid_telemetry;;esac
        [ "$age" -gt 5 ] || break
        attempt=$((attempt+1));sleep 1
    done
    [ "$age" -le 5 ] || fail stale_telemetry
    printf '%s\n' "$data" | grep -q '"connected":true' || fail printer_disconnected
    printf '%s\n' "$data" | grep -q '"machine":{"status":1,' || fail printer_not_idle
    if ! printf '%s\n' "$data" | grep -q '"print":{"enabled":false,';then
        uds=$(http_get http://127.0.0.1:8081/api/uds) || fail uds_unavailable
        printf '%s\n' "$uds" | grep -q '"connected":true,"fresh":true' || fail stale_uds
        printf '%s\n' "$uds" | grep -q '"print_state":"complete"' || fail active_print_session
    fi
    for heater in extruder heater_bed;do
        printf '%s\n' "$data" | awk -v k="$heater" -f "$BASE/thermal.awk" || fail heaters_not_cold
    done
    if printf '%s\n' "$data" | grep -q '"active":true';then fail background_operation;fi
    update=$(http_get http://127.0.0.1:8081/api/app-update/status) || fail update_state_unavailable
    printf '%s\n' "$update" | grep -q '"busy":false' || fail update_busy
    manager=$(http_get http://127.0.0.1:8081/api/network/manager) || fail network_state_unavailable
    printf '%s\n' "$manager" | grep -q '"busy":false' || fail network_busy
    printf '%s\n' "$manager" | grep -q '"pending":null' || fail network_confirmation_pending
}
files_match(){
    kind=$1
    while read -r old new relative;do
        destination=$(target "$relative") || return 1
        regular "$destination" || return 1
        actual=$(sha "$destination")
        case "$kind" in
            original) [ "$actual" = "$old" ] || return 1;;
            preview) [ "$actual" = "$new" ] || return 1;;
            mixed) [ "$actual" = "$old" ] || [ "$actual" = "$new" ] || return 1;;
            *) return 1;;
        esac
    done <"$BASE/file-pins.txt"
}
load_backup(){
    regular "$STATE/backup" || return 1
    name=$(cat "$STATE/backup")
    case "$name" in backup-*) ;; *) return 1;;esac
    case "$name" in *[!a-zA-Z0-9-]*) return 1;;esac
    BACKUP=$STATE/$name
    [ -d "$BACKUP" ] && [ ! -L "$BACKUP" ] || return 1
    while read -r old new relative;do
        regular "$BACKUP/$relative" && [ "$(sha "$BACKUP/$relative")" = "$old" ] || return 1
    done <"$BASE/file-pins.txt"
}
publish(){
    source=$1;destination=$2
    regular "$destination" || return 1
    previous=$(sha "$destination")
    [ "$previous" = "$old" ] || [ "$previous" = "$new" ] || return 1
    temporary=${destination}.cc2-native-display-$$
    [ ! -e "$temporary" ] && [ ! -L "$temporary" ] || return 1
    if ! cp -p "$source" "$temporary";then rm -f "$temporary";return 1;fi
    if [ "$(sha "$source")" != "$(sha "$temporary")" ];then rm -f "$temporary";return 1;fi
    if ! regular "$destination" || [ "$(sha "$destination")" != "$previous" ];then rm -f "$temporary";return 1;fi
    if ! mv -f "$temporary" "$destination";then rm -f "$temporary";return 1;fi
    [ "$(sha "$source")" = "$(sha "$destination")" ]
}
restore_files(){
    load_backup || { echo 'RESTORE REFUSED: original backup is missing or changed.' >&2;return 1; }
    files_match mixed || { echo 'RESTORE REFUSED: a target contains a foreign change.' >&2;return 1; }
    while read -r old new relative;do
        destination=$(target "$relative") || return 1
        publish "$BACKUP/$relative" "$destination" || return 1
    done <"$BASE/file-pins.txt"
    files_match original || return 1
    sync
    printf '%s\n' restored >"$STATE/phase"
}
finish(){
    result=$?
    trap - 0 1 2 15
    set +e
    if [ "$result" != 0 ] && [ "$CHANGED" = 1 ];then
        if restore_files;then echo 'ROLLBACK PASSED: original disk files restored. No process restarted.' >&2
        else echo 'RESTORE NEEDED: preserve the backup and inspect files before reboot.' >&2;fi
    fi
    if [ "$HELD" = 1 ];then rmdir "$LOCK" 2>/dev/null || true;fi
    echo "DISPLAY_EXIT=$result PHASE=$PHASE" >&2
    exit "$result"
}
trap finish 0
trap 'exit 130' 1 2 15
[ "$(id -u)" = 0 ] || fail root_required
[ -d "$BASE" ] && [ ! -L "$BASE" ] || fail component_path
regular "$BASE/pins.sh" || fail payload_pins
case "${1:-status}" in
    restore|status) (cd "$BASE" && sha256sum -c CONTROL-SHA256SUMS >/dev/null) || fail control_checksum;;
    *) (cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || fail payload_checksum;;
esac
. "$BASE/pins.sh"
for directory in "$IMAGES";do [ -d "$directory" ] && [ ! -L "$directory" ] || fail image_directory;done
[ "$(readlink /opt/usr/images/current_theme)" = "$IMAGES" ] || fail different_active_theme
[ "$(readlink /etc/rc.d/S95printer)" = ../init.d/printer ] || fail different_startup
[ "$(sha /etc/init.d/printer)" = "$INIT_SHA" ] || fail different_native_init
[ "$(cat /sys/class/graphics/fb0/modes)" = U:532x300p-60 ] || fail different_visible_geometry
[ "$(cat /sys/class/graphics/fb0/bits_per_pixel)" = 32 ] || fail different_pixel_format
[ "$(uname -m)" = armv7l ] && [ "$(uname -r)" = 5.4.61 ] || fail different_platform
[ ! -L "$STATE" ] || fail state_symlink
mkdir -p "$STATE"
[ -d "$STATE" ] || fail state_path
mkdir "$LOCK" || fail another_operation_or_stale_lock
HELD=1
case "${1:-status}" in
    status)
        echo "GUI on disk: $(sha "$GUI")"
        [ ! -f "$STATE/phase" ] || cat "$STATE/phase"
        echo 'No process restarted; native init and boot partitions unchanged.'
        ;;
    check|install)
        PHASE=preflight
        files_match original || fail original_gui_or_images_differ
        pid=$(pidof ec-eeb001-gui 2>/dev/null || true)
        case "$pid" in ''|*' '*|*[!0-9]*) fail one_native_display_required;;esac
        [ "$(sha "/proc/$pid/exe")" = "$GUI_SHA" ] || fail running_gui_changed
        if grep -q 'cc2-display' "/proc/$pid/maps";then fail old_display_experiment_active;fi
        if tr '\0' '\n' <"/proc/$pid/environ" | grep -q '^LD_PRELOAD=.';then fail existing_display_preload;fi
        idle
        echo 'CHECK PASS: exact native display/resources/startup, fresh cold Idle and no pending transaction.'
        if [ "$1" = check ];then echo 'No target file changed and no process restarted.';exit 0;fi
        [ ! -e "$STATE/backup" ] && [ ! -L "$STATE/backup" ] || fail prior_backup_retained
        PHASE=backup
        name=backup-$(date +%Y%m%d-%H%M%S)-$$
        BACKUP=$STATE/$name
        mkdir "$BACKUP" && mkdir "$BACKUP/gui" "$BACKUP/images" || fail backup_directory
        while read -r old new relative;do
            destination=$(target "$relative") || fail target_path
            cp -p "$destination" "$BACKUP/$relative" || fail backup_copy
            [ "$(sha "$BACKUP/$relative")" = "$old" ] || fail backup_readback
        done <"$BASE/file-pins.txt"
        printf '%s\n' "$name" >"$STATE/backup.new"
        mv "$STATE/backup.new" "$STATE/backup"
        sync
        idle
        files_match original || fail target_changed_after_backup
        PHASE=applying
        printf '%s\n' applying >"$STATE/phase"
        CHANGED=1
        while read -r old new relative;do
            destination=$(target "$relative") || fail target_path
            publish "$BASE/$relative" "$destination" || fail publish_failed
        done <"$BASE/file-pins.txt"
        files_match preview || fail installed_readback
        sync
        printf '%s\n' installed >"$STATE/phase"
        CHANGED=0
        echo 'INSTALLED ON DISK: native display refresh. No process restarted and no automatic reboot.'
        echo 'Reboot the complete idle printer manually. Keep SSH access and the backup.'
        ;;
    restore)
        PHASE=restore_preflight
        idle
        restore_files || fail restore_failed
        echo 'RESTORED ON DISK: original display and images. No process restarted.'
        echo 'Reboot the complete idle printer manually to return to the original display.'
        ;;
    *) fail 'usage: manage.sh check|install|restore|status';;
esac
