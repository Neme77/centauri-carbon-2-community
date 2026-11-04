#!/bin/sh
# Private opt-in trial. On-disk activation/rollback only; never restart a service.
set -eu
umask 077
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
BASE=/opt/usr/cc2-cavity-floor
RUNTIME=/opt/usr/cc2-reactor-runtime-v2
NETWORK=/opt/usr/cc2-network-manager
BACKUP=$BASE/backup
LOCK=$BASE/lock
HELD=0
CHANGED=0
READER=
fail(){ echo "STOP: $*" >&2;exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
regular(){
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    # TinaLinux has ls/awk but no standalone stat. Numeric ls retains the
    # same root-owner/single-hardlink checks; reject missing/extra records.
    LC_ALL=C ls -ldn "$1" 2>/dev/null | awk '
        NR==1 {ok=($1 ~ /^-/ && $2=="1" && $3=="0")}
        END {exit !(NR==1 && ok)}
    '
}
target(){
    case "$1" in
        reactor-launcher) printf '%s/launcher.sh\n' "$RUNTIME";;
        reactor-manifest) printf '%s/SHA256SUMS\n' "$RUNTIME";;
        network-profile) printf '%s/profile.sh\n' "$NETWORK";;
        network-entry) printf '%s/reactor-entry.sh\n' "$NETWORK";;
        network-activation) printf '%s/activate.sh\n' "$NETWORK";;
        network-manifest) printf '%s/SHA256SUMS\n' "$NETWORK";;
        *) return 1;;
    esac
}
publish(){
    source=$1;destination=$2;expected=$3
    regular "$source" && regular "$destination" && [ "$(sha "$destination")" = "$expected" ] || return 1
    temporary=$destination.cc2-cavity-$$
    [ ! -e "$temporary" ] && [ ! -L "$temporary" ] || return 1
    (set -C; : >"$temporary") || return 1
    if ! cp -p "$source" "$temporary";then rm -f "$temporary";return 1;fi
    if ! regular "$temporary" || [ "$(sha "$temporary")" != "$(sha "$source")" ] ||
       ! regular "$destination" || [ "$(sha "$destination")" != "$expected" ];then
        rm -f "$temporary";return 1
    fi
    mv -f "$temporary" "$destination"
}
validate_plan(){
    [ -d "$BACKUP" ] && [ ! -L "$BACKUP" ] && regular "$BACKUP/plan" || return 1
    count=0;seen=' '
    while read -r old new key;do
        file=$(target "$key") || return 1
        case "$seen" in *" $key "*) return 1;;esac
        seen="$seen$key "
        case "$old$new" in *[!0-9a-f]*|'') return 1;;esac
        [ "${#old}" = 64 ] && [ "${#new}" = 64 ] || return 1
        regular "$BACKUP/$key.old" && regular "$BACKUP/$key.new" || return 1
        [ "$(sha "$BACKUP/$key.old")" = "$old" ] && [ "$(sha "$BACKUP/$key.new")" = "$new" ] || return 1
        regular "$file" || return 1
        actual=$(sha "$file")
        [ "$actual" = "$old" ] || [ "$actual" = "$new" ] || return 1
        count=$((count+1))
    done <"$BACKUP/plan"
    case "$seen" in *' reactor-launcher '*) ;; *) return 1;;esac
    case "$seen" in *' reactor-manifest '*) ;; *) return 1;;esac
    if [ "$count" = 2 ];then [ ! -e "$NETWORK" ] && [ ! -L "$NETWORK" ];return;fi
    [ "$count" = 6 ] || return 1
    case "$seen" in *' network-profile '*) ;; *) return 1;;esac
    case "$seen" in *' network-manifest '*) ;; *) return 1;;esac
    case "$seen" in *' network-entry '*) ;; *) return 1;;esac
    case "$seen" in *' network-activation '*) ;; *) return 1;;esac
}
restore(){
    validate_plan || return 1
    rm -f "$BASE/enabled"
    while read -r old new key;do
        file=$(target "$key");actual=$(sha "$file")
        if [ "$actual" = "$new" ];then publish "$BACKUP/$key.old" "$file" "$new" || return 1;fi
    done <"$BACKUP/plan"
}
finish(){
    code=$?;trap - 0 1 2 15
    if [ -n "$READER" ];then kill -KILL "$READER" 2>/dev/null || true;wait "$READER" 2>/dev/null || true;fi
    if [ "$code" != 0 ] && [ "$CHANGED" = 1 ];then
        if restore;then echo 'ROLLBACK COMPLETE: exact original files restored.' >&2
        else echo 'ROLLBACK INCOMPLETE: retain backup and do not reboot.' >&2;fi
    fi
    if [ "$HELD" = 1 ];then rm -f "$LOCK/http";rmdir "$LOCK" 2>/dev/null || true;fi
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
http(){
    # Bound even a wget build without a working timeout option.
    file=$LOCK/http
    [ ! -e "$file" ] && [ ! -L "$file" ] || return 1
    wget -qO- "http://127.0.0.1:8081$1" >"$file" 2>/dev/null & READER=$!
    elapsed=0
    while kill -0 "$READER" 2>/dev/null;do
        if [ "$elapsed" -ge 5 ];then kill -KILL "$READER" 2>/dev/null || true;wait "$READER" 2>/dev/null || true;READER=;rm -f "$file";return 1;fi
        sleep 1;elapsed=$((elapsed+1))
    done
    if wait "$READER";then READER=;cat "$file";rm -f "$file";else READER=;rm -f "$file";return 1;fi
}
idle(){
    data=$(http /api/printer) || fail printer_unavailable
    printf '%s\n' "$data" | grep -q '"connected":true' || fail printer_disconnected
    printf '%s\n' "$data" | grep -q '"machine":{"status":1,' || fail printer_not_idle
    age=$(printf '%s\n' "$data" | awk -F'"last_message_age":' 'NF==2{split($2,a,",");print a[1]}')
    case "$age" in ''|*[!0-9]*) fail invalid_telemetry;;esac
    [ "$age" -le 5 ] || fail stale_telemetry
    for heater in extruder heater_bed;do
        printf '%s\n' "$data" | awk -v k="$heater" -f "$BASE/thermal.awk" || fail printer_not_cold
    done
    printf '%s\n' "$data" | grep -q '"active":true' && fail active_background_session
    uds=$(http /api/uds) || fail uds_unavailable
    printf '%s\n' "$uds" | grep -q '"connected":true,"fresh":true' || fail uds_not_fresh
    printf '%s\n' "$uds" | grep -Eq '"print_state":"(standby|complete|cancelled)"' || fail native_print_active
    for endpoint in /api/app-update/status /api/network/manager /api/calibration-tools/status /api/console;do
        status=$(http "$endpoint") || fail operation_status_unavailable
        printf '%s\n' "$status" | grep -q '"busy":false' || fail operation_busy
        printf '%s\n' "$status" | grep -q '"owned":true' && fail active_calibration
        if [ "$endpoint" = /api/network/manager ];then
            printf '%s\n' "$status" | grep -q '"pending":null' || fail network_confirmation_pending
        fi
    done
    status=$(http /api/nozzle-expansion) || fail nozzle_status_unavailable
    printf '%s\n' "$status" | grep -q '"state":"idle"' || fail nozzle_calibration_active
}
stage_file(){
    key=$1;file=$(target "$key") || fail unknown_target
    regular "$file" || fail unsafe_target
    cp -p "$file" "$BACKUP/$key.old" || fail backup_copy
    cp -p "$file" "$BACKUP/$key.new" || fail stage_copy
}
rewrite_manifest(){
    file=$1;name=$2;old=$3;new=$4
    awk -v name="$name" -v old="$old" -v new="$new" '
      $2==name || $2=="./" name {
          if($1!=old)exit 1
          count++
          if(!sub("^" old,new))exit 1
          print;next
      }
      {print}
      END {if(count!=1)exit 1}
    ' "$file" >"$BACKUP/manifest.next" || fail manifest_entry_mismatch
    cat "$BACKUP/manifest.next" >"$file";rm -f "$BACKUP/manifest.next"
}
verify_native(){
    regular "$RUNTIME/elegoo_printer" && [ "$(sha "$RUNTIME/elegoo_printer")" = 19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745 ] || fail unsupported_native_executable
    regular "$RUNTIME/libcc2-reactor-v2.so" && [ "$(sha "$RUNTIME/libcc2-reactor-v2.so")" = 4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c ] || fail unsupported_reactor_library
    (cd "$RUNTIME" && sha256sum -c SHA256SUMS >/dev/null) || fail damaged_reactor
}
prepare(){
    [ ! -e "$BACKUP" ] && [ ! -L "$BACKUP" ] || fail backup_already_exists
    regular "$RUNTIME/launcher.sh" && [ "$(sha "$RUNTIME/launcher.sh")" = a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc ] || fail unsupported_reactor_launcher
    mkdir "$BACKUP" || fail backup_creation
    stage_file reactor-launcher;stage_file reactor-manifest
    cat "$BASE/launcher.sh" >"$BACKUP/reactor-launcher.new"
    rewrite_manifest "$BACKUP/reactor-manifest.new" launcher.sh "$(sha "$BACKUP/reactor-launcher.old")" "$(sha "$BACKUP/reactor-launcher.new")"
    keys='reactor-launcher reactor-manifest'
    if [ -e "$NETWORK" ] || [ -L "$NETWORK" ];then
        [ -d "$NETWORK" ] && [ ! -L "$NETWORK" ] || fail unsafe_network_component
        regular "$NETWORK/profile.sh" && [ "$(sha "$NETWORK/profile.sh")" = "$(cat "$BASE/profile.original.sha256")" ] || fail unsupported_network_profile
        regular "$NETWORK/reactor-entry.sh" && [ "$(sha "$NETWORK/reactor-entry.sh")" = 914a1f49c39e90f37c27bb50ddcf9b02459bd6895131a8051f12b6f72c940adb ] || fail unsupported_network_entry
        regular "$NETWORK/activate.sh" && [ "$(sha "$NETWORK/activate.sh")" = 0883272a7734f6f2206113eb215c9fc1d612fe9ecc62605ae6b8d6e8abc4be7d ] || fail unsupported_network_activation
        (cd "$NETWORK" && sha256sum -c SHA256SUMS >/dev/null) || fail damaged_network_component
        if [ -f "$NETWORK/startup.sha256" ];then sha256sum -c "$NETWORK/startup.sha256" >/dev/null || fail changed_network_startup;fi
        stage_file network-profile;stage_file network-entry;stage_file network-activation;stage_file network-manifest
        cat "$BASE/profile.sh" >"$BACKUP/network-profile.new"
        cat "$BASE/reactor-entry.sh" >"$BACKUP/network-entry.new"
        cat "$BASE/activate.sh" >"$BACKUP/network-activation.new"
        rewrite_manifest "$BACKUP/network-manifest.new" profile.sh "$(sha "$BACKUP/network-profile.old")" "$(sha "$BACKUP/network-profile.new")"
        rewrite_manifest "$BACKUP/network-manifest.new" reactor-entry.sh "$(sha "$BACKUP/network-entry.old")" "$(sha "$BACKUP/network-entry.new")"
        rewrite_manifest "$BACKUP/network-manifest.new" activate.sh "$(sha "$BACKUP/network-activation.old")" "$(sha "$BACKUP/network-activation.new")"
        keys="$keys network-profile network-entry network-activation network-manifest"
    fi
    : >"$BACKUP/plan"
    for key in $keys;do printf '%s %s %s\n' "$(sha "$BACKUP/$key.old")" "$(sha "$BACKUP/$key.new")" "$key" >>"$BACKUP/plan";done
    validate_plan || fail prepared_plan_invalid
}
[ "$(id -u)" = 0 ] || fail root_required
[ -d "$BASE" ] && [ ! -L "$BASE" ] && [ -d "$RUNTIME" ] && [ ! -L "$RUNTIME" ] || fail component_missing
command=${1:-check}
case "$command" in
    status) if [ -f "$BASE/enabled" ];then echo 'Enabled for next full boot';else echo 'Disabled';fi;exit 0;;
    check|enable|disable) ;;
    *) fail unknown_action;;
esac
mkdir "$LOCK" || fail operation_locked
HELD=1
idle
if [ "$command" = disable ];then
    restore || fail changed_or_missing_backup
    echo 'DISABLED: exact launcher and network profile restored. No process restarted. Reboot the complete idle printer.'
    exit 0
fi
(cd "$BASE" && sha256sum -c SHA256SUMS >/dev/null) || fail damaged_payload
verify_native
if [ -e "$BACKUP" ];then validate_plan || fail changed_backup_or_target;else prepare;fi
[ "$(sha "$BASE/launcher.sh")" = "$(sha "$BACKUP/reactor-launcher.new")" ] || fail different_trial_payload
if [ -f "$BACKUP/network-profile.new" ];then
    [ "$(sha "$BASE/profile.sh")" = "$(sha "$BACKUP/network-profile.new")" ] || fail different_trial_profile
    [ "$(sha "$BASE/reactor-entry.sh")" = "$(sha "$BACKUP/network-entry.new")" ] || fail different_trial_entry
    [ "$(sha "$BASE/activate.sh")" = "$(sha "$BACKUP/network-activation.new")" ] || fail different_trial_activation
fi
if [ "$command" = check ];then echo 'CHECK PASS: guarded native minimum, exact profile and reversible plan. No target file changed.';exit 0;fi
idle
verify_native
CHANGED=1
rm -f "$BASE/enabled"
while read -r old new key;do
    file=$(target "$key");regular "$file" || fail unsafe_activation_target
    actual=$(sha "$file")
    if [ "$actual" = "$old" ];then publish "$BACKUP/$key.new" "$file" "$old" || fail activation_copy
    elif [ "$actual" != "$new" ];then fail changed_activation_target;fi
done <"$BACKUP/plan"
printf 'CC2-CAVITY-FLOOR-R1\n' >"$LOCK/enabled"
chmod 600 "$LOCK/enabled"
[ ! -L "$BASE/enabled" ] || fail unsafe_enabled_marker
mv -f "$LOCK/enabled" "$BASE/enabled"
CHANGED=0
echo 'ENABLED ON DISK. No process restarted. Reboot the complete idle printer. Disable this private trial before another application/firmware update.'
