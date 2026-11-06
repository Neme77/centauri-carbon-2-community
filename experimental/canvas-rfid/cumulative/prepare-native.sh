#!/bin/sh
# Closed maintainer cumulative trial. On-disk staging only, no native restart.
set -eu
umask 077
STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
FLOOR=/opt/usr/cc2-cavity-floor
LOCK=/tmp/cc2-control-install.lock
held=0
finish(){
    code=$?;trap - 0 1 2 15
    if [ "$held" = 1 ];then rmdir "$LOCK" || code=1;fi
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
PAYLOAD_SHA=7a08e95b2e92dcd93b7cdba4353001f71927f3f2955343205c607553981afa28
case "${1:-}" in --check|--apply) ;; *) echo 'STOP: unsupported_cumulative_action' >&2;exit 1;;esac
[ "$(id -u)" = 0 ] || exit 1
cd "$STAGE"
sha256sum -c SHA256SUMS || exit 1
[ "$(sha256sum "$STAGE/floor/SHA256SUMS" | awk '{print $1}')" = "$PAYLOAD_SHA" ] || exit 1
# Windows/SCP does not preserve the Unix executable bit. Restore only the
# already hash-verified, regular helper inside this private staging directory.
[ -f "$STAGE/floor-publisher" ] && [ ! -L "$STAGE/floor-publisher" ] || exit 1
chmod 700 "$STAGE/floor-publisher" || exit 1
if [ -e "$FLOOR" ] || [ -L "$FLOOR" ];then
    [ -d "$FLOOR" ] && [ ! -L "$FLOOR" ] || exit 1
    LC_ALL=C ls -ldn "$FLOOR" | awk 'NR==1{ok=($1=="drwx------"&&$3==0&&$4==0)}END{exit !(NR==1&&ok)}' || exit 1
    [ -f "$FLOOR/SHA256SUMS" ] && [ ! -L "$FLOOR/SHA256SUMS" ] || exit 1
    [ "$(sha256sum "$FLOOR/SHA256SUMS" | awk '{print $1}')" = "$PAYLOAD_SHA" ] || exit 1
fi
if [ "$1" = --apply ];then sh "$STAGE/retire_completed.sh" --apply;else sh "$STAGE/retire_completed.sh" --check;fi
mkdir "$LOCK" || exit 1
held=1
if [ ! -e "$FLOOR" ] && [ ! -L "$FLOOR" ];then
    # The existing bounded ARM publisher provides atomic no-replace publication.
    # It is compiled for the separate chamber component namespace.
    "$STAGE/floor-publisher" --publish "$STAGE/floor" || exit 1
fi
(cd "$FLOOR" && sha256sum -c SHA256SUMS) || exit 1
action=check
[ "$1" != --apply ] || action=enable
n=1
while :;do
    log=$STAGE/floor-$action-$n.log
    if sh "$FLOOR/manage.sh" "$action" >"$log" 2>&1;then cat "$log";break;else code=$?;fi
    cat "$log"
    # Only the qualified no-write stale snapshot refusal may be retried.
    [ "$n" -lt 3 ] && grep -qx 'STOP: stale_telemetry' "$log" &&
        [ "$(grep -c '^STOP:' "$log")" = 1 ] || exit "$code"
    sleep 2;n=$((n+1))
done
if [ "$1" = --check ];then
    echo 'CUMULATIVE PREFLIGHT PASSED: previous RFID completed; exact EXHAUST plan prepared. No active startup or Canvas changed.'
else
    echo 'EXHAUST STAGED ON DISK. Continue the bundled app installer before the requested full reboot. No native process restarted or Canvas written.'
fi
