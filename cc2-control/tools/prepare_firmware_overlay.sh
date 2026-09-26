#!/bin/sh
set -eu
BASE="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BIN="$BASE/dist/cc2-control/cc2-control"
DEST="$BASE/firmware-integration/overlay/opt/inst/cc2-control/cc2-control"

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
echo "Firmware overlay ready: $DEST"
md5sum "$DEST"
