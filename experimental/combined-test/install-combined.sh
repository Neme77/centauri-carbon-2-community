#!/bin/sh
set -eu
STAGE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$STAGE/common.sh"
LOCK=/tmp/cc2-combined-install.lock
mkdir "$LOCK" || { echo 'STOP: altra installazione in corso'; exit 1; }
BACKUP=''
STOPPED=0
CHANGED=0
PASSED=0
finish() {
    code=$?
    trap - 0 1 2 15
    set +e
    if [ "$PASSED" = 0 ] && [ "$STOPPED" = 1 ]; then
        echo 'Installazione fallita: ripristino della configurazione precedente.'
        "$INIT" stop
        "$SERVICE" stop
        sleep 3
        if pidof elegoo_printer cc2-control >/dev/null 2>&1; then
            echo "RIPRISTINO NON ESEGUITO: processo ancora attivo. Backup: $BACKUP"
            rmdir "$LOCK"
            exit 1
        fi
        if [ "$CHANGED" = 1 ]; then
            rm -rf "$TARGET" "$BASE"
            cp -a "$BACKUP/cc2-control" "$TARGET"
            cp -p "$BACKUP/cc2.init" "$INIT"
            cp -p "$BACKUP/printer.init" "$LAUNCH_FILE"
            rm -f /etc/init.d/cc2-reactor-launch
            if [ -d "$BACKUP/reactor.before" ]; then cp -a "$BACKUP/reactor.before" "$BASE"; fi
        fi
        "$SERVICE" start
        "$INIT" start
        echo "Ripristino richiesto ai servizi. Backup: $BACKUP; verificare PID e API."
    fi
    rmdir "$LOCK"
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
for cmd in sha256sum awk sed grep wget pidof cp sh; do command -v "$cmd" >/dev/null; done
(cd "$STAGE" && sha256sum -c SHA256SUMS)
[ "$(hash /opt/bin/elegoo_printer)" = "$VENDOR_EXPECTED" ] || { echo 'STOP: binario diverso o test temporaneo attivo. Riavvia prima di installare.'; exit 1; }
if awk '$2=="/opt/bin/elegoo_printer" || $2=="/etc/init.d/printer" {found=1} END {exit !found}' /proc/mounts; then
    echo 'STOP: mount sperimentale presente. Riavvia prima di installare.'; exit 1
fi
one_printer || { echo 'STOP: processo Elegoo non verificato'; exit 1; }
if grep -q '/libcc2-reactor-v2.so' "/proc/$PID/maps"; then echo 'STOP: modulo gia attivo; usare il ripristino prima di aggiornare'; exit 1; fi
idle_cold || { echo 'STOP: attendere macchina inattiva, fredda e telemetria fresca. Se la velocita resta obsoleta, riavvia prima di installare.'; exit 1; }
[ -d "$TARGET" ] && [ -x "$INIT" ] && [ -f "$SERVICE" ]
[ ! -e /etc/init.d/cc2-reactor-launch ] || { echo 'STOP: hook persistente gia presente. Ripristinare prima di aggiornare.'; exit 1; }
AVAIL=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
[ "${AVAIL:-0}" -ge 24000 ] || { echo 'STOP: memoria disponibile sotto 24000 kB'; exit 1; }
PROBE=/etc/init.d/.cc2-combined-write-$$
(umask 077; : > "$PROBE") || { echo 'STOP: init non scrivibile; la persistenza richiede un firmware integrato. Nessun servizio modificato.'; exit 1; }
rm -f "$PROBE"
LAUNCH_FILE="$SERVICE"
if grep -Eq '^[[:space:]]*run_printer[.]sh[[:space:]]+start[[:space:]]*&[[:space:]]*$' "$SERVICE"; then
    LAUNCH_FILE=/opt/bin/run_printer.sh
    [ -f "$LAUNCH_FILE" ] || { echo 'STOP: launcher vendor assente'; exit 1; }
    PROBE=$(dirname "$LAUNCH_FILE")/.cc2-combined-write-$$
    (umask 077; : > "$PROBE") || { echo 'STOP: launcher non scrivibile'; exit 1; }
    rm -f "$PROBE"
fi
awk '
 /^[[:space:]]*elegoo_printer([[:space:]]|$)/ {n++;sub(/elegoo_printer/,"/etc/init.d/cc2-reactor-launch")}
 /^[[:space:]]*LD_BIND_NOW=1[[:space:]]+elegoo_printer[[:space:]]/ {n++;sub(/elegoo_printer/,"/etc/init.d/cc2-reactor-launch")}
 {print}
 END {if(n!=1)exit 1}
' "$LAUNCH_FILE" > "$STAGE/printer.init.new" || { echo 'STOP: riga di avvio vendor non riconosciuta'; exit 1; }
sh -n "$STAGE/printer.init.new"
sh "$STAGE/reactor/preflight.sh"
BACKUP="/opt/usr/cc2-combined-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir "$BACKUP"
chmod 700 "$BACKUP"
cp -a "$TARGET" "$BACKUP/cc2-control"
cp -p "$INIT" "$BACKUP/cc2.init"
cp -p "$LAUNCH_FILE" "$BACKUP/printer.init"
printf '%s\n' "$LAUNCH_FILE" > "$BACKUP/launch.path"
if [ -d "$BASE" ]; then cp -a "$BASE" "$BACKUP/reactor.before"; fi
cp "$STAGE/restore-combined.sh" "$BACKUP/restore.sh"
cp "$STAGE/common.sh" "$BACKUP/common.sh"
cp "$STAGE/printer.init.new" "$BACKUP/printer.installed"
printf '%s\n' "$BACKUP" > "$STAGE/backup.path"
FREE=$(df -k /opt/usr | awk 'END {print $4}')
[ "${FREE:-0}" -ge 40000 ] || { echo 'STOP: spazio persistente insufficiente dopo il backup'; exit 1; }
idle_cold || { echo 'STOP: lo stato macchina e cambiato'; exit 1; }
STOPPED=1
"$INIT" stop
"$SERVICE" stop
sleep 3
if pidof cc2-control elegoo_printer >/dev/null 2>&1; then echo 'STOP: servizi non fermati'; exit 1; fi
CHANGED=1
rm -rf "$BASE"
mkdir "$BASE"
chmod 700 "$BASE"
cp "$STAGE/reactor/"* "$BASE/"
cp /opt/bin/elegoo_printer "$BASE/elegoo_printer"
[ "$(hash "$BASE/elegoo_printer")" = "$VENDOR_EXPECTED" ]
chmod 700 "$BASE/elegoo_printer" "$BASE/launcher.sh" "$BASE/cc2-reactor-bridge-test"
rm -f "$BASE/activation.marker" "$BASE/reactor-state.txt" "$BASE/exceptions.log"
cp "$STAGE/printer.init.new" "$LAUNCH_FILE"
chmod 755 "$LAUNCH_FILE"
cp "$STAGE/boot-launcher.sh" /etc/init.d/cc2-reactor-launch
chmod 755 /etc/init.d/cc2-reactor-launch
cp "$STAGE/cc2-control" "$TARGET/cc2-control.new"
chmod 755 "$TARGET/cc2-control.new"
mv "$TARGET/cc2-control.new" "$TARGET/cc2-control"
cp "$STAGE/start.sh" "$TARGET/start.sh"
cp "$STAGE/launch.sh" "$TARGET/launch.sh"
cp "$STAGE/cc2-control.init" "$INIT"
cp "$STAGE/build-info.json" "$TARGET/build-info.json"
cp "$STAGE/cc2-uds-probe" "$TARGET/cc2-uds-probe"
mkdir -p "$TARGET/web/locales"
cp "$STAGE/web/index.html" "$TARGET/web/index.html"
cp "$STAGE/web/locales/"*.json "$TARGET/web/locales/"
chmod 755 "$TARGET/start.sh" "$TARGET/launch.sh" "$TARGET/cc2-uds-probe" "$INIT"
chmod 644 "$TARGET/web/index.html" "$TARGET/web/locales/"*.json "$TARGET/build-info.json"
[ "$(hash "$TARGET/cc2-control")" = '@CC2_SHA256@' ]
printf '%s\n' "$BACKUP" > "$BASE/persistent-backup.path"
"$SERVICE" start > "$BASE/runtime.log" 2>&1
sleep 20
one_printer || { tail -n 40 "$BASE/runtime.log"; echo 'FAIL: printer non verificato'; exit 1; }
grep -q "$BASE/libcc2-reactor-v2.so" "/proc/$PID/maps"
grep -q "pid=$PID " "$BASE/activation.marker"
"$INIT" enable
"$INIT" start
TRIES=0
while [ "$TRIES" -lt 90 ]; do
    HEALTH=$(wget -qO- http://127.0.0.1:8081/api/health 2>/dev/null || true)
    UDS=$(wget -qO- http://127.0.0.1:8081/api/uds 2>/dev/null || true)
    CPID=$(pidof cc2-control || true)
    case "$CPID" in ''|*' '*) ;; *)
        if [ "$(hash "/proc/$CPID/exe")" = '@CC2_SHA256@' ] &&
            printf '%s' "$HEALTH" | grep -q '"mqtt_registered":true' &&
            printf '%s' "$HEALTH" | grep -q '"snapshot_received":true' &&
            printf '%s' "$UDS" | grep -q '"fresh":true' &&
            [ -r "/proc/$PID/maps" ] && grep -q "$BASE/libcc2-reactor-v2.so" "/proc/$PID/maps"; then
            PASSED=1
            printf '%s\n%s\n' "$HEALTH" "$UDS"
            cat "$BASE/activation.marker"
            echo "INSTALLAZIONE COMBINATA PASS. Persistente dopo il riavvio; backup: $BACKUP"
            echo "Ripristino, solo inattiva e fredda: sh $BACKUP/restore.sh"
            echo 'Prima prova Home All, poi stampa breve, quindi lunga. OOM/803 non ancora certificato.'
            exit 0
        fi;;
    esac
    TRIES=$((TRIES+1)); sleep 2
done
echo 'FAIL: verifica finale non completata'; exit 1
