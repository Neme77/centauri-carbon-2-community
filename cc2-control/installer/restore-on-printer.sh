#!/bin/sh
set -eu
BACKUP=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=/opt/usr/cc2-control
INIT=/etc/init.d/cc2-control
LOCK=/tmp/cc2-control-install.lock
@CAMERA_RESTORE_FUNCTION@
wait_stopped() {
    stop_wait=0
    while pidof cc2-control >/dev/null 2>&1; do
        [ "$stop_wait" -lt 20 ] || return 1
        sleep 1
        stop_wait=$((stop_wait + 1))
    done
    return 0
}
# Fingerprint the installed and running vendor components before any backup or service stop.
# The application-reported firmware number alone cannot identify the installed base.
required_firmware='@FIRMWARE_VERSION@'
expected_printer='@PRINTER_SHA256@'
expected_daemon='@DAEMON_SHA256@'
require_hash() {
    actual=$(sha256sum "$1" 2>/dev/null | awk '{print $1}')
    [ "$actual" = "$2" ] || { echo "Incompatible firmware: Community Firmware V$required_firmware is required ($1)." >&2; exit 1; }
}
require_hash /opt/bin/elegoo_printer "$expected_printer"
require_hash /opt/inst/daemon-000/daemon-000 "$expected_daemon"
printer_pid=$(pidof elegoo_printer 2>/dev/null || true)
case "$printer_pid" in ''|*' '*) echo 'Cannot identify the running printer process.' >&2; exit 1;; esac
require_hash "/proc/$printer_pid/exe" "$expected_printer"

test -d "$BACKUP/installation"
test -f "$BACKUP/init.before"
STATUS=$(wget -qO- http://127.0.0.1:8081/api/printer)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Restore requires connected Idle printer.' >&2; exit 1;; esac
AGE=$(printf '%s' "$STATUS" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
case "$AGE" in ''|*[!0-9]*) exit 1;; esac
[ "$AGE" -le 15 ] || exit 1
require_cold() {
    for heater in extruder heater_bed; do
        temperatures=$(printf '%s' "$STATUS" | sed -n "s/.*\"$heater\":{\"temperature\":\([-0-9.]*\),\"target\":\([-0-9.]*\)}.*/\1 \2/p")
        printf '%s\n' "$temperatures" | awk 'NF == 2 && $1 ~ /^[0-9]+([.][0-9]+)?$/ && $2 ~ /^[0-9]+([.][0-9]+)?$/ && $1 <= 50 && $2 == 0 {valid=1} END {exit !valid}' ||
            { echo 'Printer must be cold (heaters at most 50 C, targets zero) with valid readback.' >&2; exit 1; }
    done
}
require_cold

mkdir "$LOCK" || exit 1
trap 'rmdir "$LOCK"' 0
if [ -f "$BACKUP/printer-camera.before" ]; then camera_restore_startup "$BACKUP"; fi
"$INIT" stop
if ! wait_stopped; then echo 'Service did not stop.' >&2; exit 1; fi
# Keep current preferences and access configuration when reverting binaries.
for keep in cc2-control.conf material-presets.json ui-preferences.json bed-plates.json spools.json; do
    if [ -f "$TARGET/$keep" ]; then cp -p "$TARGET/$keep" "$BACKUP/$keep.restore-current"; fi
done
FAILED="/opt/usr/cc2-control-before-restore-$(date +%Y%m%d-%H%M%S)-$$"
mv "$TARGET" "$FAILED"
if ! cp -a "$BACKUP/installation" "$TARGET"; then
    rm -rf "$TARGET";mv "$FAILED" "$TARGET";"$INIT" start;exit 1
fi
for keep in cc2-control.conf material-presets.json ui-preferences.json bed-plates.json spools.json; do
    if [ -f "$BACKUP/$keep.restore-current" ]; then cp -p "$BACKUP/$keep.restore-current" "$TARGET/$keep"; fi
done
cp -p "$BACKUP/init.before" "$INIT"
"$INIT" start
echo "Previous build restored. Replaced installation retained at $FAILED"
