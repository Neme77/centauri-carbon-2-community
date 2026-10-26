#!/bin/sh
set -eu
BASE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "$BASE/cc2-control" \
    --update-verifier "$BASE/cc2-update-worker" \
    --update-downloader "$BASE/cc2-https-download" \
    --update-ca "$BASE/ca-bundle.pem" \
    --update-runner "$BASE/update-runner.sh" \
    --port 8081 \
    --panda-port 7125 \
    --web-root "$BASE/web" \
    --config "$BASE/cc2-control.conf" \
    --presets "$BASE/material-presets.json" \
    --preferences "$BASE/ui-preferences.json" \
    --plates "$BASE/bed-plates.json" \
    --spools "$BASE/spools.json"
