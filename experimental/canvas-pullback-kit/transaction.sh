#!/bin/sh
# A staged, fingerprint-checked file transaction. No service restart or motor command.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASE=/opt/usr/cc2-reactor-runtime-v2
STATE=/opt/usr/cc2-canvas-pullback-1000
LOCK=/tmp/cc2-pullback.lock
MODE=${1:-}
case "$MODE" in install|remove) ;; *) echo 'STOP: invalid operation'; exit 1;; esac
case "$HERE" in /opt/usr/.cc2-pullback-stage-*) ;; *) echo 'STOP: unsupported staging directory'; exit 1;; esac
TOKEN=${HERE#/opt/usr/.cc2-pullback-stage-}
case "$TOKEN" in ''|*[!a-f0-9]*) echo 'STOP: invalid staging token'; exit 1;; esac
[ "${#TOKEN}" -eq 20 ] || exit 1
mkdir "$LOCK" || { echo 'STOP: another pull-back operation is active'; exit 1; }
CHANGED=0
DONE=0
fail() { echo "STOP: $*" >&2; exit 1; }
ready() {
    [ ! -e /tmp/cc2-control-install.lock ] || fail 'CC2 Control installation is active'
    [ "$(pidof elegoo_printer)" = "$PID" ] || fail 'printer process changed'
    START=$(sed 's/.*) //' "/proc/$PID/stat" | awk '{print $20}')
    [ "$START" = "$(cat "$HERE/start")" ] || fail 'printer process was replaced'
    S=$(wget -qO- http://127.0.0.1:8081/api/printer) || fail 'printer state unavailable'
    H=$(wget -qO- http://127.0.0.1:8081/api/health) || fail 'health unavailable'
    case "$S" in *'"connected":true'*) ;; *) fail 'printer disconnected';; esac
    printf '%s' "$S" | grep -Eq '"machine":\{"status":1[,}]' || fail 'printer must remain Idle'
    case "$S" in *'"print":{"enabled":false'*) ;; *) fail 'active print prevents installation';; esac
    case "$H" in *'"mqtt_registered":true'*) ;; *) fail 'MQTT registration unavailable';; esac
    AGE=$(printf '%s' "$S" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
    case "$AGE" in ''|*[!0-9]*) fail 'missing fresh telemetry';; esac
    [ "$AGE" -le 15 ] || fail 'stale telemetry'
    for HEATER in extruder heater_bed; do
        T=$(printf '%s' "$S" | sed -n "s/.*\"$HEATER\":{\"temperature\":\([-0-9.]*\),\"target\":\([-0-9.]*\)}.*/\1 \2/p")
        printf '%s\n' "$T" | awk 'NF==2 && $1~/^[0-9]+([.][0-9]+)?$/ && $2~/^[0-9]+([.][0-9]+)?$/ && $1<=50 && $2==0 {ok=1} END {exit !ok}' || fail 'printer must be cold with heater targets zero'
    done
}
hash() { sha256sum "$1" | awk '{print $1}'; }
replace() {
    SRC=$1; DEST=$2; PERM=$3; EXPECTED=$4
    [ -f "$DEST" ] && [ ! -L "$DEST" ] || return 1
    [ ! -e "$DEST.cc2-pb-new" ] && [ ! -L "$DEST.cc2-pb-new" ] || return 1
    cp "$SRC" "$DEST.cc2-pb-new" || return 1
    chmod "$PERM" "$DEST.cc2-pb-new" || return 1
    [ "$(hash "$DEST.cc2-pb-new")" = "$EXPECTED" ] || return 1
    mv -f "$DEST.cc2-pb-new" "$DEST"
}
finish() {
    CODE=$?
    trap - 0 1 2 15
    set +e
    if [ "$DONE" -eq 0 ] && [ "$CHANGED" -eq 1 ]; then
        while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do
            TEMP="$DEST.cc2-pb-new"
            if [ -f "$TEMP" ] && [ ! -L "$TEMP" ] && [ "$(hash "$TEMP")" = "$AFTER" ]; then rm -f "$TEMP"; fi
        done < "$HERE/plan"
    fi
    if [ "$DONE" -eq 0 ] && [ "$CHANGED" -eq 1 ] && [ "$MODE" = install ]; then
        # Installation never changes distance. Restore only exact known old/new files.
        OK=1
        while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do
            CURRENT=$(hash "$DEST")
            if [ "$CURRENT" = "$AFTER" ]; then
                replace "$STATE/backup/$KEY" "$DEST" "$PERM" "$BEFORE" || OK=0
            elif [ "$CURRENT" != "$BEFORE" ]; then OK=0; fi
        done < "$HERE/plan"
        [ "$OK" -eq 1 ] && echo 'Original files restored; no service restarted.' >&2
        [ "$OK" -eq 1 ] || echo 'Automatic restoration incomplete; retain backups and do not reboot.' >&2
    fi
    rmdir "$LOCK" 2>/dev/null
    exit "$CODE"
}
trap finish 0
trap 'exit 130' 1 2 15
PID=$(cat "$HERE/pid")
case "$PID" in ''|*[!0-9]*) fail 'invalid process ID';; esac
(cd "$HERE" && sha256sum -c payload.sha256 >/dev/null) || fail 'payload checksums'
# Validate the whole plan before any backup or target mutation.
SEEN='|'
ORDER=''
while IFS='|' read -r KEY DEST BEFORE AFTER PERM EXTRA; do
    [ -z "$EXTRA" ] || fail 'extra plan fields'
    case "$KEY:$DEST" in
        boot:/etc/init.d/cc2-reactor-launch|launcher:/opt/usr/cc2-reactor-runtime-v2/launcher.sh|sums:/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS|program:/opt/usr/cc2-reactor-runtime-v2/elegoo_printer|main:/opt/inst/printer_dsp.cfg|autosave:/opt/usr/cfg/autosave.cfg|autosave_backup:/opt/usr/cfg/autosave_backup.cfg) ;;
        *) fail 'invalid transaction target';;
    esac
    case "$SEEN" in *"|$KEY|"*) fail 'duplicate transaction key';; esac
    SEEN="$SEEN$KEY|"
    ORDER="$ORDER $KEY"
    case "$BEFORE$AFTER" in *[!a-f0-9]*) fail 'invalid fingerprint';; esac
    [ "${#BEFORE}" -eq 64 ] && [ "${#AFTER}" -eq 64 ] || fail 'invalid fingerprint length'
    case "$PERM" in [0-7][0-7][0-7]) ;; *) fail 'invalid permissions';; esac
    [ -f "$DEST" ] && [ ! -L "$DEST" ] || fail 'unsupported target'
    [ -f "$HERE/after/$KEY" ] && [ ! -L "$HERE/after/$KEY" ] || fail 'unsupported candidate'
    [ "$(hash "$HERE/after/$KEY")" = "$AFTER" ] || fail 'candidate fingerprint mismatch'
done < "$HERE/plan"
if [ "$MODE" = install ]; then
    [ "$ORDER" = ' boot launcher sums program' ] || fail 'invalid installation order'
else
    case "$ORDER" in *' program sums launcher boot') ;; *) fail 'invalid removal order';; esac
    CONFIG_ORDER=${ORDER% program sums launcher boot}
    case "$CONFIG_ORDER" in ''|' main'|' autosave'|' autosave_backup'|' main autosave'|' main autosave_backup'|' autosave autosave_backup'|' main autosave autosave_backup') ;; *) fail 'invalid configuration order';; esac
fi
ready
(cd / && sha256sum -c "$HERE/before.sha256" >/dev/null) || fail 'printer files changed while preparing'
# Preflight is the installed, fingerprint-qualified bridge helper; activation is disabled in that helper.
if [ "$MODE" = install ]; then
    sh "$BASE/preflight.sh" > "$HERE/preflight.log" 2>&1 || fail 'native runtime preflight failed'
fi
ready
if [ "$MODE" = install ]; then
    [ ! -e "$STATE" ] || fail 'an existing component must be inspected/removed first'
    FREE=$(df -Pk /opt/usr | awk 'END {print $4}')
    case "$FREE" in ''|*[!0-9]*) fail 'cannot verify free storage';; esac
    [ "$FREE" -ge 131072 ] || fail 'at least 128 MiB free persistent storage required'
    mkdir -m 700 "$STATE"
    mkdir -m 700 "$STATE/backup"
    while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do cp -p "$DEST" "$STATE/backup/$KEY"; done < "$HERE/plan"
    cp "$HERE/manifest.json" "$STATE/manifest.json"
    cp "$HERE/plan" "$STATE/install-plan"
    printf 'prepared\n' > "$STATE/phase"
    sync
else
    [ -d "$STATE/backup" ] && [ ! -L "$STATE" ] || fail 'missing qualified component backup'
    mkdir -p "$STATE/config-before-removal"
    while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do
        case "$KEY" in main|autosave|autosave_backup) cp -p "$DEST" "$STATE/config-before-removal/$KEY";; esac
    done < "$HERE/plan"
fi
# After the backups, recheck the complete input and the live printer state.
(cd / && sha256sum -c "$HERE/before.sha256" >/dev/null) || fail 'printer files changed during backup'
ready
CHANGED=1
printf '%s-in-progress\n' "$MODE" > "$STATE/phase"
sync
while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do
    case "$KEY:$DEST" in
        boot:/etc/init.d/cc2-reactor-launch|launcher:/opt/usr/cc2-reactor-runtime-v2/launcher.sh|sums:/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS|program:/opt/usr/cc2-reactor-runtime-v2/elegoo_printer|main:/opt/inst/printer_dsp.cfg|autosave:/opt/usr/cfg/autosave.cfg|autosave_backup:/opt/usr/cfg/autosave_backup.cfg) ;;
        *) fail 'invalid transaction target';;
    esac
    [ "$(hash "$DEST")" = "$BEFORE" ] || fail "concurrent change to $DEST"
    replace "$HERE/after/$KEY" "$DEST" "$PERM" "$AFTER"
    sync
done < "$HERE/plan"
(cd / && sha256sum -c "$HERE/after.sha256" >/dev/null) || fail 'installed readback mismatch'
printf '%s-complete\n' "$MODE" > "$STATE/phase"
sync
if [ "$MODE" = remove ]; then
    ARCHIVE="$STATE.removed-$(basename "$HERE")"
    [ ! -e "$ARCHIVE" ] || fail 'backup archive already exists'
    mv "$STATE" "$ARCHIVE"
    echo "REMOVAL PASSED. Backups retained: $ARCHIVE"
else
    echo 'INSTALLATION PASSED. Distance unchanged; no service restarted.'
fi
DONE=1
# Delete only the enumerated staged payload; retain metadata/logs and all backups.
while IFS='|' read -r KEY DEST BEFORE AFTER PERM; do rm -f "$HERE/after/$KEY"; done < "$HERE/plan"
rmdir "$HERE/after"
echo 'Reboot manually while idle. Current printer process keeps its previous executable until reboot.'
