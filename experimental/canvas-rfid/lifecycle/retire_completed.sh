#!/bin/sh
# Retire ONLY the exact completed owner trial, before cumulative preparation.
# No deletion, native stop, UART, startup edit or firmware action.
set -eu
umask 077
BASE=/opt/usr/cc2-rfid
PARENT=/opt/usr
LOCK=/tmp/cc2-control-install.lock
COORDINATOR_SHA=add9d432ebd4f91650eccb6b42f92aa172c0d972d451fbace65cbed1467faf1d
held=0
reader=
fail(){ echo "STOP: $*" >&2;exit 1; }
cleanup(){
    trap - 0 1 2 15
    if [ -n "$reader" ];then kill -KILL "$reader" 2>/dev/null || true;wait "$reader" 2>/dev/null || true;fi
    if [ "$held" = 1 ];then rm -f "$LOCK/retire-http";rmdir "$LOCK" || true;fi
}
trap cleanup 0
trap 'exit 130' 1 2 15
regular(){
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    LC_ALL=C ls -ldn "$1" | awk 'NR==1{ok=($1 ~ /^-/ && $2==1 && $3==0 && $4==0)}END{exit !(NR==1&&ok)}'
}
status(){
    [ ! -e "$LOCK/retire-http" ] && [ ! -L "$LOCK/retire-http" ] || return 1
    wget -qO- http://127.0.0.1:8081/api/rfid/lifecycle >"$LOCK/retire-http" & reader=$!
    n=0
    while kill -0 "$reader" 2>/dev/null;do
        [ "$n" -lt 5 ] || return 1
        sleep 1;n=$((n+1))
    done
    wait "$reader" || return 1
    reader=
    data=$(cat "$LOCK/retire-http");rm -f "$LOCK/retire-http"
    for field in '"installed":true' '"metadata_valid":true' '"phase":10,' '"busy":false' '"reboot_required":false' '"recovery_required":false' '"result":0,' '"activation_requested":false';do
        printf '%s\n' "$data" | grep -F "$field" >/dev/null || return 1
    done
}
[ "$(id -u)" = 0 ] || fail root_required
case "${1:-}" in --check|--apply) ;; *) fail unsupported_retirement_action;;esac
if [ ! -e "$BASE" ] && [ ! -L "$BASE" ];then echo 'NO PREVIOUS RFID COMPONENT';exit 0;fi
[ -d "$BASE" ] && [ ! -L "$BASE" ] || fail foreign_component_type
LC_ALL=C ls -ldn "$BASE" | awk 'NR==1{ok=($1=="drwx------" && $3==0 && $4==0)}END{exit !(NR==1&&ok)}' || fail unsafe_component_directory
[ -d "$PARENT" ] && [ ! -L "$PARENT" ] || fail unsafe_archive_parent
LC_ALL=C ls -ldn "$PARENT" | awk 'NR==1{ok=($1 ~ /^d/ && substr($1,6,1)!="w" && substr($1,9,1)!="w" && $3==0 && $4==0)}END{exit !(NR==1&&ok)}' || fail unsafe_archive_parent
regular "$BASE/coordinator" || fail foreign_coordinator
[ "$(sha256sum "$BASE/coordinator" | awk '{print $1}')" = "$COORDINATOR_SHA" ] || fail unsupported_previous_coordinator
# The pinned old coordinator verifies phase10, exact profile, original startup
# files, original Canvas transfer receipt, live native identity and cold/idle.
"$BASE/coordinator" --verify-disabled-boot || fail previous_trial_not_verified_disabled
mkdir "$LOCK" || fail another_operation_active
held=1
status || fail previous_trial_not_disabled
# Expire the app's one-second status cache while mutations remain blocked.
sleep 2
status || fail previous_trial_state_changed
if [ "$1" = --check ];then echo 'RETIREMENT CHECK PASS: completed trial retained, no component moved';exit 0;fi
DEST=$PARENT/cc2-rfid-retired-$(date +%Y%m%d-%H%M%S)-$$
case "$DEST" in "$PARENT"/cc2-rfid-retired-*) ;; *) fail unsafe_archive_target;;esac
mkdir "$DEST" || fail archive_destination_exists
[ ! -e "$DEST/component" ] && [ ! -L "$DEST/component" ] || fail archive_target_exists
mv "$BASE" "$DEST/component" || fail archive_move_failed
sync
echo "COMPLETED RFID TRIAL PRESERVED AT $DEST/component"
echo 'No file deleted, native process stopped, startup changed or Canvas written.'
