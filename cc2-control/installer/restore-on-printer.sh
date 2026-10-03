#!/bin/sh
set -eu
BACKUP=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=/opt/usr/cc2-control
INIT=/etc/init.d/cc2-control
LOCK=/tmp/cc2-control-install.lock
test -d "$BACKUP/installation"
test -f "$BACKUP/init.before"
STATUS=$(wget -qO- http://127.0.0.1:8081/api/printer)
case "$STATUS" in *'"connected":true'*'"machine":{"status":1,'*) ;; *) echo 'Restore requires connected Idle printer.' >&2; exit 1;; esac
AGE=$(printf '%s' "$STATUS" | sed -n 's/.*"last_message_age":\([0-9][0-9]*\),.*/\1/p')
case "$AGE" in ''|*[!0-9]*) exit 1;; esac
[ "$AGE" -le 15 ] || exit 1
mkdir "$LOCK" || exit 1
trap 'rmdir "$LOCK"' 0
"$INIT" stop
if pidof cc2-control >/dev/null; then echo 'Service did not stop.' >&2; exit 1; fi
# Keep current preferences and access configuration when reverting binaries.
for keep in cc2-control.conf material-presets.json ui-preferences.json; do
    if [ -f "$TARGET/$keep" ]; then cp -p "$TARGET/$keep" "$BACKUP/$keep.restore-current"; fi
done
FAILED="/opt/usr/cc2-control-before-restore-$(date +%Y%m%d-%H%M%S)-$$"
mv "$TARGET" "$FAILED"
if ! cp -a "$BACKUP/installation" "$TARGET"; then
    rm -rf "$TARGET";mv "$FAILED" "$TARGET";"$INIT" start;exit 1
fi
for keep in cc2-control.conf material-presets.json ui-preferences.json; do
    if [ -f "$BACKUP/$keep.restore-current" ]; then cp -p "$BACKUP/$keep.restore-current" "$TARGET/$keep"; fi
done
cp -p "$BACKUP/init.before" "$INIT"
"$INIT" start
echo "Previous build restored. Replaced installation retained at $FAILED"
