#!/bin/sh
set -eu
umask 077
MODE=${1:-check}
case "$MODE" in check|apply) ;; *) echo 'Usage: sh cc2-cleanup.sh check|apply' >&2;exit 2;; esac
ROOT=/opt/usr
APP=$ROOT/cc2-control
INIT=/etc/init.d/cc2-control
PLAN=/tmp/cc2-cleanup-plan-$$
LOCK=/tmp/cc2-control-install.lock
LOCKED=0
STOPPED=0
stop() { echo "STOP: $*" >&2;exit 1; }
finish() {
    code=$?
    trap - 0
    if [ "$STOPPED" = 1 ] && [ "$code" != 0 ];then echo 'CC2 Control was asked to stop; it is not restarted automatically.' >&2;fi
    rm -f "$PLAN"
    if [ "$LOCKED" = 1 ];then rmdir "$LOCK" || code=1;fi
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
[ "$(id -u)" = 0 ] || stop 'root required'
[ -d "$ROOT" ] && [ ! -L "$ROOT" ] && [ "$(cd "$ROOT" && pwd -P)" = "$ROOT" ] || stop 'persistent root must be regular'
[ -d "$APP" ] && [ ! -L "$APP" ] || stop 'application directory must be regular'
[ -f "$INIT" ] && [ ! -L "$INIT" ] || stop 'application service is missing or symlinked'
STATE=$(wget -qO- http://127.0.0.1:8081/api/printer) || stop 'telemetry unavailable'
case "$STATE" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) stop 'connected idle printer required';; esac
AGE=$(printf '%s' "$STATE" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
case "$AGE" in ''|*[!0-9]*) stop 'missing telemetry age';; esac
[ "$AGE" -le 15 ] || stop 'stale telemetry'
for HEATER in extruder heater_bed;do
    printf '%s' "$STATE" | sed -n "s/.*\"$HEATER\":{\"temperature\":\([-0-9.]*\),\"target\":\([-0-9.]*\)}.*/\1 \2/p" | awk 'NF==2 && $1 ~ /^[0-9]+([.][0-9]+)?$/ && $2 ~ /^[0-9]+([.][0-9]+)?$/ && $1<=50 && $2==0 {ok=1} END{exit !ok}' || stop 'cold printer with zero heater targets required'
done
UPDATE=$(wget -qO- http://127.0.0.1:8081/api/app-update/status) || stop 'update status unavailable'
case "$UPDATE" in *'"busy":false'*) ;; *) stop 'updater is busy or state is unknown';; esac
[ ! -e "$LOCK" ] && [ ! -L "$LOCK" ] || stop 'installer lock exists'
! pidof cc2-update-worker >/dev/null 2>&1 || stop 'update worker is running'
eligible() {
    case "$1" in
        "$APP/cc2-control.conf"|"$APP/cc2-control.conf.new"|"$APP/ui-preferences.json"|"$APP/ui-preferences.json.tmp"|"$APP/ui-preferences.json.new") return 0;;
        "$ROOT/cc2-control-updates"|"$ROOT/cc2-control.init-pre-1.1.24"|"$ROOT/cc2-control.inst-launch-pre-1.1.24") return 0;;
    esac
    [ "${1%/*}" = "$ROOT" ] || return 1
    printf '%s\n' "${1##*/}" | grep -Eq '^(cc2-control-backup-[0-9]{8}-[0-9]{6}-[0-9]+|cc2-reactor-before-firmware-[0-9]+|cc2-camera-before-reinstall-[0-9]{8}|cc2-camera-before-firmware-[0-9]+|cc2-camera-reap-boot-r1|cc2-camera-recovery-r2)$'
}
safe() {
    path=$1
    eligible "$path" || stop 'path is outside the exact cleanup scope'
    [ ! -L "$path" ] || stop "refusing symlink: $path"
    [ -f "$path" ] || [ -d "$path" ] || stop "not a regular file/directory: $path"
    # Never recurse into an additional mount, nor remove active executable,
    # mapped library, working directory or an active/rollback startup reference.
    awk -v p="$path" '$2==p || index($2,p"/")==1 {found=1} END{exit !found}' /proc/mounts && return 1
    for ref in /etc/init.d/printer /etc/init.d/cc2-camera-launch /etc/init.d/cc2-reactor-launch "$APP/camera/printer.original" "$ROOT/cc2-reactor-runtime-v2/printer.original";do
        [ ! -f "$ref" ] || ! grep -F "$path/" "$ref" >/dev/null 2>&1 || return 1
    done
    for proc in /proc/[0-9]*;do
        for ref in exe cwd;do
            dest=$(readlink "$proc/$ref" 2>/dev/null || true)
            case "$dest" in "$path"|"$path/"*) return 1;; esac
        done
        if [ -r "$proc/maps" ] && grep -F "$path/" "$proc/maps" >/dev/null 2>&1;then return 1;fi
        if [ -r "$proc/cmdline" ] && tr '\000' '\n' < "$proc/cmdline" | grep -F "$path/" >/dev/null 2>&1;then return 1;fi
    done
    return 0
}
: > "$PLAN"
for path in "$ROOT"/cc2-control-backup-* "$ROOT"/cc2-reactor-before-firmware-* "$ROOT"/cc2-camera-before-reinstall-* "$ROOT"/cc2-camera-before-firmware-* "$ROOT/cc2-camera-reap-boot-r1" "$ROOT/cc2-camera-recovery-r2" "$ROOT/cc2-control-updates" "$ROOT/cc2-control.init-pre-1.1.24" "$ROOT/cc2-control.inst-launch-pre-1.1.24" "$APP/cc2-control.conf" "$APP/cc2-control.conf.new" "$APP/ui-preferences.json" "$APP/ui-preferences.json.tmp" "$APP/ui-preferences.json.new";do
    [ -e "$path" ] || [ -L "$path" ] || continue
    eligible "$path" || continue
    if safe "$path";then printf '%s\n' "$path" >> "$PLAN";echo "REMOVE: $path";else echo "KEEP: active, mounted or required by startup/rollback: $path";fi
done
if [ "$MODE" = check ];then echo 'CHECK COMPLETE: no settings, files or services changed.';exit 0;fi
mkdir "$LOCK" || stop 'cannot acquire installer lock'
LOCKED=1
STOPPED=1
"$INIT" stop || stop 'cannot stop application service'
waited=0
while pidof cc2-control >/dev/null 2>&1;do
    [ "$waited" -lt 20 ] || stop 'application did not stop'
    sleep 1;waited=$((waited+1))
done
while IFS= read -r path;do
    safe "$path" || stop "candidate became active: $path"
    if [ -d "$path" ];then rm -rf -- "$path";else rm -f -- "$path";fi
    [ ! -e "$path" ] && [ ! -L "$path" ] || stop "removal failed: $path"
done < "$PLAN"
sync
echo 'CLEANUP COMPLETE: application credentials/preferences and unused old copies removed.'
echo 'Printer calibration, mesh, materials, plates, spools, G-code and active runtimes preserved.'
echo 'CC2 Control remains stopped. No printer or service reboot/restart was requested.'
echo 'Install the prepared complete firmware through the printer display. Its normal reboot is the next start.'
