# Syntax-only qualification copy: octal camera-data literals replaced with one byte; never install this file.
#!/bin/sh
set -eu
STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=/opt/usr/cc2-control
INIT=/etc/init.d/cc2-control
LOCK=/tmp/cc2-control-install.lock
wait_stopped() {
    stop_wait=0
    while pidof cc2-control >/dev/null 2>&1; do
        [ "$stop_wait" -lt 20 ] || return 1
        sleep 1
        stop_wait=$((stop_wait + 1))
    done
    return 0
}
EXPECTED=3b40ae6dd0d5627209560dfb3eb7a32e4a30c88ef56082e025377b3a0f2c6ef8
# Bounded local health read for BusyBox without timeout or working wget -T.
# Called only while the private installer lock directory exists.
installer_http_get() (
    umask 077
    http_file="$LOCK/http-read.$$"
    http_reader=
    [ ! -e "$http_file" ] && [ ! -L "$http_file" ] || exit 1
    http_cleanup() {
        trap - 0 1 2 15
        if [ -n "$http_reader" ]; then kill -KILL "$http_reader" 2>/dev/null || true; fi
        rm -f "$http_file"
    }
    trap http_cleanup 0
    trap 'exit 130' 1 2 15
    wget -qO- "$1" > "$http_file" 2>/dev/null &
    http_reader=$!
    http_seconds=0
    while kill -0 "$http_reader" 2>/dev/null; do
        if [ "$http_seconds" -ge 5 ]; then
            echo 'Local status request exceeded five seconds.' >&2
            exit 124
        fi
        sleep 1
        http_seconds=$((http_seconds + 1))
    done
    if wait "$http_reader"; then
        http_reader=
        cat "$http_file"
    else
        http_reader=
        exit 1
    fi
)

mkdir "$LOCK" || { echo 'Another installation is active.' >&2; exit 1; }
CHANGED=0
STOPPED=0
PASSED=0
BACKUP=''
CAMERA_SOURCE=''
camera_restore_startup() {
    camera_backup=$1
    camera_init=/etc/init.d/printer
    [ -f "$camera_backup/printer-camera.before" ] && [ ! -L "$camera_backup/printer-camera.before" ] || return 1
    [ -f "$camera_init" ] && [ ! -L "$camera_init" ] || return 1
    camera_before=$(sha256sum "$camera_backup/printer-camera.before" | awk '{print $1}')
    camera_current=$(sha256sum "$camera_init" | awk '{print $1}')
    [ "$camera_current" != "$camera_before" ] || return 0
    case "$camera_before" in
      bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563|01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc) ;;
      *) return 1;;
    esac
    [ -f "$TARGET/camera/patched.sha256" ] && [ ! -L "$TARGET/camera/patched.sha256" ] || return 1
    [ "$camera_current" = "$(cat "$TARGET/camera/patched.sha256")" ] || return 1
    camera_restore_dir=/etc/init.d/.cc2-camera-restore-$$
    mkdir "$camera_restore_dir" || return 1
    if ! cp -p "$camera_backup/printer-camera.before" "$camera_restore_dir/printer"; then
        rmdir "$camera_restore_dir"; return 1
    fi
    if [ "$(sha256sum "$camera_init" | awk '{print $1}')" != "$camera_current" ] || ! sh -n "$camera_restore_dir/printer"; then
        rm -f "$camera_restore_dir/printer"; rmdir "$camera_restore_dir"; return 1
    fi
    if ! mv "$camera_restore_dir/printer" "$camera_init"; then
        rm -f "$camera_restore_dir/printer"; rmdir "$camera_restore_dir"; return 1
    fi
    rmdir "$camera_restore_dir"
    sync
    [ "$(sha256sum "$camera_init" | awk '{print $1}')" = "$camera_before" ]
}

camera_unpack() {
    CAMERA_SOURCE="$STAGE/camera-component-$$"
    (umask 077; mkdir "$CAMERA_SOURCE") || return 1
    printf '\000' > "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    printf '\000' >> "$CAMERA_SOURCE/payload.tar.gz" || return 1
    [ "$(sha256sum "$CAMERA_SOURCE/payload.tar.gz" | awk '{print $1}')" = "95e7a364676be6dc0a4e134fbb21c992e67f03222a48aebfa803e720d5b2aaa0" ] || return 1
    tar -xzf "$CAMERA_SOURCE/payload.tar.gz" -C "$CAMERA_SOURCE" || return 1
    rm -f "$CAMERA_SOURCE/payload.tar.gz"
    (cd "$CAMERA_SOURCE" && sha256sum -c SHA256SUMS) || return 1
}

finish() {
    code=$?
    trap - 0 1 2 15
    set +e
    if [ "$PASSED" -eq 0 ] && [ -n "$CAMERA_SOURCE" ] && [ -n "$BACKUP" ] && [ -f "$BACKUP/printer-camera.before" ]; then
        if ! camera_restore_startup "$BACKUP"; then
            echo "Camera startup restoration refused; inspect $BACKUP before reboot." >&2
            rmdir "$LOCK"
            exit 1
        fi
    fi
    if [ "$PASSED" -eq 0 ] && [ "$STOPPED" -eq 1 ]; then
        "$INIT" stop >/dev/null 2>&1
        if ! wait_stopped; then
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
[ "$(sha256sum "$STAGE/cc2-control" | awk '{print $1}')" = "$EXPECTED" ] || { echo "Candidate differs from installer pin; no installation changed." >&2; exit 1; }
# Fingerprint the installed and running vendor components before any backup or service stop.
# The application-reported firmware number alone cannot identify the installed base.
required_firmware='4.4'
expected_printer='19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745'
expected_daemon='83433b14e49d1e8aa9523282ead03618378573f4b66de1ad8f7f3c444b11aada'
require_hash() {
    actual=$(sha256sum "$1" 2>/dev/null | awk '{print $1}')
    [ "$actual" = "$2" ] || { echo "Incompatible firmware: Community Firmware V$required_firmware is required ($1)." >&2; exit 1; }
}
require_hash /opt/bin/elegoo_printer "$expected_printer"
require_hash /opt/inst/daemon-000/daemon-000 "$expected_daemon"
printer_pid=$(pidof elegoo_printer 2>/dev/null || true)
case "$printer_pid" in ''|*' '*) echo 'Cannot identify the running printer process.' >&2; exit 1;; esac
require_hash "/proc/$printer_pid/exe" "$expected_printer"

test -d "$TARGET"
test -x "$INIT"
STATUS=$(installer_http_get http://127.0.0.1:8081/api/printer)
HEALTH=$(installer_http_get http://127.0.0.1:8081/api/health)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Printer must be connected and Idle.' >&2; exit 1;; esac
case "$HEALTH" in *'"mqtt_registered":true'*) ;; *) echo 'MQTT registration unavailable.' >&2; exit 1;; esac
AGE=$(printf '%s' "$STATUS" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
case "$AGE" in ''|*[!0-9]*) echo 'Missing fresh printer state.' >&2; exit 1;; esac
[ "$AGE" -le 15 ] || { echo 'Printer state is stale.' >&2; exit 1; }
require_cold() {
    for heater in extruder heater_bed; do
        temperatures=$(printf '%s' "$STATUS" | sed -n "s/.*\"$heater\":{\"temperature\":\([-0-9.]*\),\"target\":\([-0-9.]*\)}.*/\1 \2/p")
        printf '%s\n' "$temperatures" | awk 'NF == 2 && $1 ~ /^[0-9]+([.][0-9]+)?$/ && $2 ~ /^[0-9]+([.][0-9]+)?$/ && $1 <= 50 && $2 == 0 {valid=1} END {exit !valid}' ||
            { echo 'Printer must be cold (heaters at most 50 C, targets zero) with valid readback.' >&2; exit 1; }
    done
}
require_cold
camera_unpack
sh "$CAMERA_SOURCE/manage.sh" check
BACKUP="/opt/usr/cc2-control-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$BACKUP"
cp -a "$TARGET" "$BACKUP/installation"
cp -p "$INIT" "$BACKUP/init.before"
cp "$STAGE/restore-on-printer.sh" "$BACKUP/restore.sh"
if [ -n "$CAMERA_SOURCE" ]; then cp -p /etc/init.d/printer "$BACKUP/printer-camera.before"; fi
STATUS=$(installer_http_get http://127.0.0.1:8081/api/printer)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Printer state changed; update cancelled.' >&2; exit 1;; esac
require_cold
STOPPED=1
"$INIT" stop
if ! wait_stopped; then echo 'Service did not stop; update cancelled.' >&2; exit 1; fi
CHANGED=1
cp "$STAGE/cc2-control" "$TARGET/cc2-control.new"
chmod 755 "$TARGET/cc2-control.new"
mv "$TARGET/cc2-control.new" "$TARGET/cc2-control"
cp "$STAGE/cc2-update-worker" "$TARGET/cc2-update-worker.new"
chmod 755 "$TARGET/cc2-update-worker.new"
mv "$TARGET/cc2-update-worker.new" "$TARGET/cc2-update-worker"
cp "$STAGE/cc2-https-download" "$TARGET/cc2-https-download"
chmod 755 "$TARGET/cc2-https-download"
cp "$STAGE/ca-bundle.pem" "$TARGET/ca-bundle.pem"
cp "$STAGE/update-runner.sh" "$TARGET/update-runner.sh.new"
chmod 755 "$TARGET/update-runner.sh.new"
mv "$TARGET/update-runner.sh.new" "$TARGET/update-runner.sh"
cp "$STAGE/updater-trusted-key.json" "$TARGET/updater-trusted-key.json"
cp "$STAGE/THIRD_PARTY_NOTICES.txt" "$TARGET/THIRD_PARTY_NOTICES.txt"
cp "$STAGE/start.sh" "$TARGET/start.sh"
cp "$STAGE/launch.sh" "$TARGET/launch.sh"
cp "$STAGE/cc2-control.init" "$INIT"
mkdir -p "$TARGET/web/locales"
cp "$STAGE/web/index.html" "$TARGET/web/index.html"
cp "$STAGE/web/locales/"*.json "$TARGET/web/locales/"
cp "$STAGE/build-info.json" "$TARGET/build-info.json"
chmod 755 "$TARGET/update-runner.sh" "$TARGET/start.sh" "$TARGET/launch.sh" "$INIT"
chmod 644 "$TARGET/web/index.html" "$TARGET/web/locales/"*.json "$TARGET/build-info.json"
# Configuration, material presets, UI preferences, the plate library and the spool library are never replaced.
for file in cc2-control cc2-update-worker cc2-https-download ca-bundle.pem update-runner.sh updater-trusted-key.json THIRD_PARTY_NOTICES.txt start.sh launch.sh web/index.html build-info.json; do
    BEFORE=$(sha256sum "$STAGE/$file" | awk '{print $1}')
    AFTER=$(sha256sum "$TARGET/$file" | awk '{print $1}')
    [ "$BEFORE" = "$AFTER" ] || { echo "Installed checksum mismatch: $file" >&2; exit 1; }
done
for locale in "$STAGE/web/locales/"*.json; do
    name=${locale##*/}
    before=$(sha256sum "$locale" | awk '{print $1}')
    after=$(sha256sum "$TARGET/web/locales/$name" | awk '{print $1}')
    [ "$before" = "$after" ] || { echo "Installed locale checksum mismatch: $name" >&2; exit 1; }
done
"$INIT" start
tries=0
# The launcher waits up to 120 seconds for the vendor process, then 30
# seconds for hardware initialization. Leave time for MQTT registration too.
VERIFY_DEADLINE=$(awk '{print int($1)+240}' /proc/uptime)
while [ "$tries" -lt 120 ] && [ "$(awk '{print int($1)}' /proc/uptime)" -lt "$VERIFY_DEADLINE" ]; do
    RESULT=$(installer_http_get http://127.0.0.1:8081/api/health 2>/dev/null || true)
    case "$RESULT" in *'"service":"cc2-control"'*'"version":"1.2.23"'*)
        RUN_PID=$(pidof cc2-control 2>/dev/null || true)
        case "$RUN_PID" in ''|*' '*) ;; *)
            RUN_HASH=$(sha256sum "/proc/$RUN_PID/exe" 2>/dev/null | awk '{print $1}')
            if [ "$RUN_HASH" = "$EXPECTED" ]; then
                case "$RESULT" in *'"mqtt_registered":true'*'"snapshot_received":true'*) ;; *) tries=$((tries+1)); sleep 2; continue;; esac
                if [ -n "$CAMERA_SOURCE" ]; then
                    sh "$CAMERA_SOURCE/manage.sh" install || exit 1
                    echo 'REBOOT REQUIRED: reboot the complete idle printer to activate the camera fixes.'
                fi
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
