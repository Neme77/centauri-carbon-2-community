#!/bin/sh
set -eu
BASE="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BIN="$BASE/dist/cc2-control/cc2-control"
DEST="$BASE/firmware-integration/overlay/opt/inst/cc2-control/cc2-control"
APP="$BASE/firmware-integration/overlay/opt/inst/cc2-control"

if [ ! -f "$BIN" ]; then
    echo "Missing $BIN. Run make first." >&2
    exit 1
fi
case "$(file "$BIN")" in
  *"ELF 32-bit"*"ARM"*) ;;
  *) echo "Refusing non-ARM binary: $(file "$BIN")" >&2; exit 1 ;;
esac
cp "$BIN" "$DEST"
chmod +x "$DEST"
mkdir -p "$APP/web/locales" "$APP/defaults"
cp "$BASE/web/index.html" "$APP/web/index.html"
rm -f "$APP"/web/locales/*.json
cp "$BASE"/web/locales/*.json "$APP/web/locales/"
cp "$BASE/defaults/material-presets.json" "$APP/defaults/material-presets.json"
cp "$BASE/scripts/start-firmware.sh" "$APP/start.sh"
chmod 755 "$APP/start.sh"
chmod 644 "$APP/web/index.html" "$APP"/web/locales/*.json "$APP/defaults/material-presets.json"
echo "Firmware overlay ready: $DEST"
md5sum "$DEST"
