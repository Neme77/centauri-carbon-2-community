#!/bin/sh
# Upgrade ONLY the verified R1b addon; no native process or app is restarted.
set -eu
umask 077
BASE=/opt/usr/cc2-cavity-floor
RUNTIME=/opt/usr/cc2-reactor-runtime-v2
NETWORK=/opt/usr/cc2-network-manager
GLOBAL=/opt/usr/.cc2-cavity-bootfix-lock
STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
SOURCE=$STAGE/payload
SAVED=$STAGE/previous-component
HELD=0
DISABLED=0
ARCHIVED=0
NEW=0
fail(){ echo "STOP: $*" >&2;exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
regular(){
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    LC_ALL=C ls -ldn "$1" 2>/dev/null | awk 'NR==1{ok=($1~/^-/&&$2=="1"&&$3=="0")} END{exit !(NR==1&&ok)}'
}
target(){
    case "$1" in
        reactor-launcher) echo "$RUNTIME/launcher.sh";;
        reactor-manifest) echo "$RUNTIME/SHA256SUMS";;
        network-profile) echo "$NETWORK/profile.sh";;
        network-entry) echo "$NETWORK/reactor-entry.sh";;
        network-activation) echo "$NETWORK/activate.sh";;
        network-manifest) echo "$NETWORK/SHA256SUMS";;
        *) return 1;;
    esac
}
baseline_matches(){
    for key in reactor-launcher reactor-manifest network-profile network-entry network-activation network-manifest;do
        file=$(target "$key") || return 1
        regular "$file" && [ "$(sha "$file")" = "$(sha "$STAGE/baseline/$key")" ] || return 1
    done
}
finish(){
    code=$?;trap - 0 1 2 15
    if [ "$code" != 0 ] && [ "$ARCHIVED" = 1 ];then
        if [ "$NEW" = 1 ] && ! baseline_matches;then
            sh "$BASE/manage.sh" disable || true
        fi
        if baseline_matches;then
            if [ "$NEW" = 1 ];then mv "$BASE" "$STAGE/failed-new-component" || true;fi
            if [ ! -e "$BASE" ] && [ ! -L "$BASE" ];then mv "$SAVED" "$BASE" || true;fi
            echo 'UPGRADE FAILED: original native startup retained; optional addon disabled. Previous component/backup preserved. No process restarted.' >&2
        else
            echo 'RESTORE INCOMPLETE: retain stage and original backups; do not reboot before reviewing targets.' >&2
        fi
    elif [ "$code" != 0 ] && [ "$DISABLED" = 1 ];then
        echo 'UPGRADE STOPPED: previous optional addon disabled; originals and backups retained. No process restarted.' >&2
    fi
    if [ "$HELD" = 1 ];then rmdir "$GLOBAL" 2>/dev/null || true;fi
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
[ "$(id -u)" = 0 ] || fail root_required
case "$STAGE" in /opt/usr/.cc2-cavity-bootfix-????????????????????????????????) ;; *) fail unexpected_stage;;esac
token=$(basename "$STAGE" | awk -F- '{print $NF}')
case "$token" in ''|*[!0-9a-f]*) fail invalid_stage_token;;esac
[ -d "$SOURCE" ] && [ ! -L "$SOURCE" ] || fail unsafe_payload
regular "$SOURCE/SHA256SUMS" && [ "$(sha "$SOURCE/SHA256SUMS")" = @PAYLOAD_SHA@ ] || fail wrong_payload_manifest
(cd "$SOURCE" && sha256sum -c SHA256SUMS) || fail damaged_payload
[ -d "$BASE" ] && [ ! -L "$BASE" ] && [ -d "$BASE/backup" ] && [ ! -L "$BASE/backup" ] || fail old_component_missing
regular "$BASE/SHA256SUMS" && [ "$(sha "$BASE/SHA256SUMS")" = 84b48f37fd2d6f433bb258c4aea0d30ddbd1a5f546ce0367f5081a11c405a7b4 ] || fail unsupported_old_component
(cd "$BASE" && sha256sum -c SHA256SUMS) || fail changed_old_component
[ ! -e "$SAVED" ] && [ ! -L "$SAVED" ] && [ ! -e "$STAGE/baseline" ] || fail retained_previous_attempt
for maps in /proc/[0-9]*/maps;do
    if grep -F -q "$BASE/libcc2-cavity-floor-r1.so" "$maps" 2>/dev/null;then fail optional_module_already_running;fi
done
regular "$NETWORK/reactor-entry.sh" && [ "$(sha "$NETWORK/reactor-entry.sh")" = 914a1f49c39e90f37c27bb50ddcf9b02459bd6895131a8051f12b6f72c940adb ] || fail unknown_network_entry
regular "$NETWORK/activate.sh" && [ "$(sha "$NETWORK/activate.sh")" = 0883272a7734f6f2206113eb215c9fc1d612fe9ecc62605ae6b8d6e8abc4be7d ] || fail unknown_network_activation
mkdir "$GLOBAL" || fail upgrade_locked
HELD=1
sh "$BASE/manage.sh" check
mkdir "$STAGE/baseline"
count=0;seen=' '
while read -r old new key;do
    case "$key" in reactor-launcher|reactor-manifest|network-profile|network-manifest) ;; *) fail unknown_old_plan_key;;esac
    case "$seen" in *" $key "*) fail duplicate_old_plan_key;;esac
    seen="$seen$key "
    file=$(target "$key")
    regular "$file" && regular "$BASE/backup/$key.old" && regular "$BASE/backup/$key.new" || fail unsafe_old_plan_file
    [ "$(sha "$file")" = "$new" ] && [ "$(sha "$BASE/backup/$key.new")" = "$new" ] && [ "$(sha "$BASE/backup/$key.old")" = "$old" ] || fail changed_old_plan
    cp -p "$BASE/backup/$key.old" "$STAGE/baseline/$key"
    count=$((count+1))
done <"$BASE/backup/plan"
[ "$count" = 4 ] || fail incomplete_old_plan
cp -p "$NETWORK/reactor-entry.sh" "$STAGE/baseline/network-entry"
cp -p "$NETWORK/activate.sh" "$STAGE/baseline/network-activation"
sh "$BASE/manage.sh" disable
DISABLED=1
baseline_matches || fail original_restore_mismatch
mv "$BASE" "$SAVED"
ARCHIVED=1
mv "$SOURCE" "$BASE"
NEW=1
chmod 700 "$BASE"
sh "$BASE/manage.sh" check
sh "$BASE/manage.sh" enable
(cd "$RUNTIME" && sha256sum -c SHA256SUMS) || fail runtime_postcheck
(cd "$NETWORK" && sha256sum -c SHA256SUMS) || fail network_postcheck
echo "BOOT WIRING UPDATED. Previous component and original backups retained: $SAVED"
echo 'No process restarted or app replaced. Reboot the complete idle printer manually, then refresh the browser.'
