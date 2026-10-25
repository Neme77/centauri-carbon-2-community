#!/bin/sh
# Snapshot only. It never restarts services, edits configuration or sends printer commands.
set -eu
BASE=/opt/usr/cc2-reactor-runtime-v2
STATE=/opt/usr/cc2-canvas-pullback-1000
[ ! -e /tmp/cc2-pullback.lock ] || { echo 'STOP: another pull-back operation is active' >&2; exit 1; }
[ ! -e /opt/usr/cc2-reactor-disabled ] || { echo 'STOP: reactor runtime is disabled' >&2; exit 1; }
P=$(pidof elegoo_printer)
case "$P" in ''|*[!0-9]*) echo 'STOP: exactly one printer process is required' >&2; exit 1;; esac
M=$(mktemp -d /tmp/cc2-pb-read.XXXXXX)
cleanup() { rm -f "$M/printer.json" "$M/health.json" "$M/pid" "$M/exe" "$M/running.sha" "$M/checks" "$M/cmdline" "$M/activation" "$M/mapped" "$M/stat"; rmdir "$M"; }
trap cleanup 0
wget -qO- http://127.0.0.1:8081/api/printer > "$M/printer.json"
wget -qO- http://127.0.0.1:8081/api/health > "$M/health.json"
printf '%s\n' "$P" > "$M/pid"
readlink "/proc/$P/exe" > "$M/exe"
sha256sum "/proc/$P/exe" > "$M/running.sha"
cat "/proc/$P/cmdline" > "$M/cmdline"
cat "/proc/$P/stat" > "$M/stat"
cat "$BASE/activation.marker" > "$M/activation"
grep -F "$BASE/libcc2-reactor-v2.so" "/proc/$P/maps" > "$M/mapped"
sha256sum /opt/bin/elegoo_printer /opt/lib/libcolib.so "$BASE/preflight.sh" "$BASE/libcc2-reactor-v2.so" "$BASE/cc2-reactor-bridge-test" > "$M/checks"
(cd /opt/inst/cc2-reactor && sha256sum -c SHA256SUMS >/dev/null)
cd /
set -- "$BASE/elegoo_printer" "$BASE/launcher.sh" "$BASE/SHA256SUMS" "$BASE/firmware-payload.sha256" /etc/init.d/cc2-reactor-launch /opt/inst/cc2-reactor/SHA256SUMS /opt/inst/printer_dsp.cfg /opt/usr/cfg/autosave.cfg
if [ -f /opt/usr/cfg/autosave_backup.cfg ]; then set -- "$@" /opt/usr/cfg/autosave_backup.cfg; fi
if [ -e "$STATE" ]; then
    [ -d "$STATE" ] && [ ! -L "$STATE" ] || exit 1
    set -- "$@" "$STATE/manifest.json" "$STATE/backup/boot" "$STATE/backup/launcher" "$STATE/backup/sums" "$STATE/backup/program"
fi
for F in "$@"; do [ -f "$F" ] && [ ! -L "$F" ] || { echo "STOP: unsupported file $F" >&2; exit 1; }; done
[ "$(pidof elegoo_printer)" = "$P" ] || { echo 'STOP: printer process changed' >&2; exit 1; }
tar -cf - "$@" "$M/printer.json" "$M/health.json" "$M/pid" "$M/exe" "$M/running.sha" "$M/checks" "$M/cmdline" "$M/activation" "$M/mapped" "$M/stat"
