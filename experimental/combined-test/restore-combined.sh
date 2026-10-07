#!/bin/sh
set -eu
BACKUP=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$BACKUP/common.sh"
LOCK=/tmp/cc2-combined-install.lock
mkdir "$LOCK" || { echo 'STOP: installazione o ripristino in corso'; exit 1; }
trap 'rmdir "$LOCK"' 0
LAUNCH_FILE="$SERVICE"
if [ -f "$BACKUP/launch.path" ]; then
    LAUNCH_FILE=$(cat "$BACKUP/launch.path")
    case "$LAUNCH_FILE" in /etc/init.d/printer|/opt/bin/run_printer.sh) ;; *) echo 'STOP: percorso backup non valido'; exit 1;; esac
fi
idle_cold || { echo 'STOP: macchina non inattiva/fredda o dati non freschi'; exit 1; }
[ -d "$BACKUP/cc2-control" ] && [ -f "$BACKUP/printer.init" ] && [ -f "$BACKUP/cc2.init" ]
[ "$(hash "$LAUNCH_FILE")" = "$(hash "$BACKUP/printer.installed")" ] || { echo 'STOP: init modificato dopo questo test. Non sovrascrivo.'; exit 1; }
PREFERENCES="$BACKUP/preferences-at-restore-$$"
mkdir "$PREFERENCES"
chmod 700 "$PREFERENCES"
for name in cc2-control.conf material-presets.json ui-preferences.json; do
    if [ -f "$TARGET/$name" ]; then cp -p "$TARGET/$name" "$PREFERENCES/$name"; fi
done
"$INIT" stop
"$SERVICE" stop
sleep 3
if pidof cc2-control elegoo_printer >/dev/null 2>&1; then echo 'STOP: processo ancora attivo'; exit 1; fi
rm -rf "$TARGET" "$BASE"
cp -a "$BACKUP/cc2-control" "$TARGET"
for name in cc2-control.conf material-presets.json ui-preferences.json; do
    rm -f "$TARGET/$name"
    if [ -f "$PREFERENCES/$name" ]; then cp -p "$PREFERENCES/$name" "$TARGET/$name"; fi
done
cp -p "$BACKUP/cc2.init" "$INIT"
cp -p "$BACKUP/printer.init" "$LAUNCH_FILE"
rm -f /etc/init.d/cc2-reactor-launch
if [ -d "$BACKUP/reactor.before" ]; then cp -a "$BACKUP/reactor.before" "$BASE"; fi
"$SERVICE" start
"$INIT" start
sleep 20
one_printer || { echo 'Ripristinati i file, processo originale non verificato: riavviare'; exit 1; }
if grep -q '/libcc2-reactor-v2.so' "/proc/$PID/maps"; then echo 'FAIL: modulo ancora attivo'; exit 1; fi
echo "RIPRISTINO PASS: printer originale PID=$PID; CC2 precedente in avvio. Attendere 90 secondi e verificare le API."
