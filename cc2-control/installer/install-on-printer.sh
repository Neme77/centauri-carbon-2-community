#!/bin/sh
set -eu
STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=/opt/usr/cc2-control
INIT=/etc/init.d/cc2-control
LOCK=/tmp/cc2-control-install.lock
EXPECTED=@BINARY_SHA256@
mkdir "$LOCK" || { echo 'Another installation is active.' >&2; exit 1; }
CHANGED=0
STOPPED=0
PASSED=0
BACKUP=''
finish() {
    code=$?
    trap - 0 1 2 15
    set +e
    if [ "$PASSED" -eq 0 ] && [ "$STOPPED" -eq 1 ]; then
        "$INIT" stop >/dev/null 2>&1
        if pidof cc2-control >/dev/null; then
            echo "Cannot restore while service is running. Backup: $BACKUP" >&2
        else
            if [ "$CHANGED" -eq 1 ]; then
                rm -rf "$TARGET"
                cp -a "$BACKUP/installation" "$TARGET"
                cp -p "$BACKUP/init.before" "$INIT"
            fi
            "$INIT" start
            echo "Previous installation restored/restarted. Backup: $BACKUP" >&2
        fi
    fi
    rmdir "$LOCK"
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
(cd "$STAGE" && sha256sum -c SHA256SUMS)
test -d "$TARGET"
test -x "$INIT"
STATUS=$(wget -qO- http://127.0.0.1:8081/api/printer)
HEALTH=$(wget -qO- http://127.0.0.1:8081/api/health)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Printer must be connected and Idle.' >&2; exit 1;; esac
case "$HEALTH" in *'"mqtt_registered":true'*) ;; *) echo 'MQTT registration unavailable.' >&2; exit 1;; esac
AGE=$(printf '%s' "$STATUS" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
case "$AGE" in ''|*[!0-9]*) echo 'Missing fresh printer state.' >&2; exit 1;; esac
[ "$AGE" -le 15 ] || { echo 'Printer state is stale.' >&2; exit 1; }
BACKUP="/opt/usr/cc2-control-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$BACKUP"
cp -a "$TARGET" "$BACKUP/installation"
cp -p "$INIT" "$BACKUP/init.before"
cp "$STAGE/restore-on-printer.sh" "$BACKUP/restore.sh"
STATUS=$(wget -qO- http://127.0.0.1:8081/api/printer)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Printer state changed; update cancelled.' >&2; exit 1;; esac
STOPPED=1
"$INIT" stop
if pidof cc2-control >/dev/null; then echo 'Service did not stop; update cancelled.' >&2; exit 1; fi
CHANGED=1
cp "$STAGE/cc2-control" "$TARGET/cc2-control.new"
chmod 755 "$TARGET/cc2-control.new"
mv "$TARGET/cc2-control.new" "$TARGET/cc2-control"
cp "$STAGE/start.sh" "$TARGET/start.sh"
cp "$STAGE/launch.sh" "$TARGET/launch.sh"
cp "$STAGE/cc2-control.init" "$INIT"
mkdir -p "$TARGET/web/locales"
cp "$STAGE/web/index.html" "$TARGET/web/index.html"
cp "$STAGE/web/locales/"*.json "$TARGET/web/locales/"
cp "$STAGE/build-info.json" "$TARGET/build-info.json"
chmod 755 "$TARGET/start.sh" "$TARGET/launch.sh" "$INIT"
chmod 644 "$TARGET/web/index.html" "$TARGET/web/locales/"*.json "$TARGET/build-info.json"
# Configuration, material presets and UI preferences are never replaced.
for file in cc2-control start.sh launch.sh web/index.html build-info.json; do
    BEFORE=$(sha256sum "$STAGE/$file" | awk '{print $1}')
    AFTER=$(sha256sum "$TARGET/$file" | awk '{print $1}')
    [ "$BEFORE" = "$AFTER" ] || { echo "Installed checksum mismatch: $file" >&2; exit 1; }
done
"$INIT" start
tries=0
# The launcher waits up to 120 seconds for the vendor process, then 60
# seconds for hardware initialization. Leave time for MQTT registration too.
while [ "$tries" -lt 120 ]; do
    RESULT=$(wget -qO- http://127.0.0.1:8081/api/health 2>/dev/null || true)
    case "$RESULT" in *'"service":"cc2-control"'*'"version":"1.1.31"'*)
        RUN_PID=$(pidof cc2-control 2>/dev/null || true)
        case "$RUN_PID" in ''|*' '*) ;; *)
            RUN_HASH=$(sha256sum "/proc/$RUN_PID/exe" 2>/dev/null | awk '{print $1}')
            if [ "$RUN_HASH" = "$EXPECTED" ]; then
                case "$RESULT" in *'"mqtt_registered":true'*'"snapshot_received":true'*) ;; *) tries=$((tries+1)); sleep 2; continue;; esac
                PASSED=1
                echo "$RESULT"
                echo "INSTALLATION PASSED. Backup: $BACKUP"
                echo "Restore while Idle: sh $BACKUP/restore.sh"
                exit 0
            fi;; esac;; esac
    tries=$((tries+1));sleep 2
done
echo 'Updater health/binary verification failed.' >&2
exit 1
