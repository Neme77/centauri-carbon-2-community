#!/bin/sh
# Launched from the installed application in an independent session.
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STAGE=${1:?Missing private staging directory}
case "$STAGE" in /opt/usr/cc2-control-updates/job-*) ;; *) exit 2;; esac
TOKEN=${STAGE##*/job-}
case "$TOKEN" in ''|*[!0-9a-f]*) exit 2;; esac
[ "${#TOKEN}" -eq 32 ] || exit 2
[ -d "$STAGE" ] && [ ! -L "$STAGE" ] || exit 2
umask 077
phase() {
    printf '{"phase":"%s","pid":%s}\n' "$1" "$$" > "$STAGE/status.new"
    mv "$STAGE/status.new" "$STAGE/status.json"
}
finished=0
finish() {
    code=$?
    trap - 0 1 2 15
    [ "$finished" -eq 1 ] || phase failed
    exit "$code"
}
trap finish 0
trap 'exit 130' 1 2 15
phase verifying
# Recheck the uploaded package before the installed service can be stopped.
# Authenticate and extract a fresh private copy. Never execute a staging file
# merely because a different copy of its archive passed an earlier check.
mkdir "$STAGE/verified-payload"
if ! "$BASE/cc2-update-worker" --extract "$STAGE/package.cc2update" "$STAGE/verified-payload" > "$STAGE/verified.json" 2> "$STAGE/install.log"; then
    exit 1
fi
phase installing
# The authenticated installer rechecks actual firmware fingerprints and a
# fresh Idle/cold/zero-target state before backup or service stop. It owns
# rollback and checks the running executable, MQTT registration and snapshot.
if sh "$STAGE/verified-payload/install-on-printer.sh" >> "$STAGE/install.log" 2>&1; then
    phase complete
    finished=1
else
    phase failed
    finished=1
    exit 1
fi
