#!/bin/sh
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "$BASE" = /opt/usr/cc2-control-network-trial-r3 ] || { echo 'STOP: unexpected staging directory'; exit 1; }
[ ! -L "$BASE" ] && [ -d "$BASE" ] || exit 1
[ "$(sha256sum "$BASE/cc2-control-payload.tar.gz" | awk '{print $1}')" = fcdfb3682893fe4b48605f490b62a087ecfae698c66e49d93de6863c4f78aad4 ] || { echo 'STOP: archive mismatch'; exit 1; }
command -v tee >/dev/null || { echo 'STOP: tee not available; no installation started'; exit 1; }
[ ! -e "$BASE/payload" ] && [ ! -L "$BASE/payload" ] || { echo 'STOP: retained payload; preserve previous attempt'; exit 1; }
mkdir "$BASE/payload"
tar -xzf "$BASE/cc2-control-payload.tar.gz" -C "$BASE/payload"
chmod 755 "$BASE/payload/install-on-printer.sh" "$BASE/payload/cc2-control"
# Preserve the actual child exit code despite tee; all files stay private on disk.
(
    set +e
    sh "$BASE/payload/install-on-printer.sh"
    result=$?
    printf '%s\n' "$result" > "$BASE/exit.code"
    exit "$result"
) 2>&1 | tee "$BASE/install.log"
[ -f "$BASE/exit.code" ] || { echo 'STOP: child exit status unavailable'; exit 1; }
result=$(cat "$BASE/exit.code")
case "$result" in ''|*[!0-9]*) echo 'STOP: invalid child exit status'; exit 1;; esac
printf 'INSTALL_COMMAND_EXIT=%s\nLog: %s/install.log\n' "$result" "$BASE"
exit "$result"
