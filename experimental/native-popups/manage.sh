#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Long-press pop-ups on the printer: install, remove or report the patched screen
# program. Disk files only; no process is stopped or restarted. A complete reboot
# of the idle printer loads the change.
#
#   sh manage.sh status
#   sh manage.sh install BUILD_DIR    (ec-eeb001-gui + manifest.json from build.py)
#   sh manage.sh remove
set -eu
umask 077
ROOT=${NUI_TEST_ROOT:-}
GUI=$ROOT/opt/bin/ec-eeb001-gui
BASE=$ROOT/opt/usr/cc2-native-popups
STOCK_SHA=db09fa37d378df259427aaac392a341282e08ceb26cd87603542c0dcbcbb91d1
if [ -n "$ROOT" ] && [ -n "${NUI_TEST_STOCK_SHA:-}" ]; then STOCK_SHA=$NUI_TEST_STOCK_SHA; fi
PRINTER_API=${NUI_TEST_PRINTER_API:-http://127.0.0.1:8081/api/printer}

fail(){ echo "STOP: $*" >&2; exit 1; }
sha(){ sha256sum "$1" | awk '{print $1}'; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
manifest_sha(){
    sed -n "s/.*\"$2\": *\"\([0-9a-f]\{64\}\)\".*/\1/p" "$1" | head -n 1
}
# CC2 Control reports the printer Idle. BusyBox 1.27 wget has no timeout option,
# so the request runs in the background and is abandoned after 5 s.
idle(){
    mkdir -p "$BASE"
    file=$BASE/printer.$$
    rm -f "$file"
    wget -qO "$file" "$PRINTER_API" >/dev/null 2>&1 &
    reader=$! seconds=0
    while kill -0 "$reader" 2>/dev/null; do
        [ "$seconds" -lt 5 ] || { kill "$reader" 2>/dev/null; wait "$reader" 2>/dev/null; rm -f "$file"; return 1; }
        sleep 1; seconds=$((seconds + 1))
    done
    wait "$reader" 2>/dev/null || { rm -f "$file"; return 1; }
    grep -q '"status_name": *"Idle"' "$file"; result=$?
    rm -f "$file"
    return $result
}
# Copy src over dst through a temporary file, verifying the hash before the rename.
replace(){
    tmp=$1.nui-tmp
    rm -f "$tmp"
    cp "$2" "$tmp" && chmod 755 "$tmp" || { rm -f "$tmp"; return 1; }
    [ "$(sha "$tmp")" = "$3" ] || { rm -f "$tmp"; return 1; }
    mv "$tmp" "$1" && sync
}

installed_sha(){ regular "$BASE/installed.sha256" && cat "$BASE/installed.sha256" || true; }

status(){
    regular "$GUI" || fail "screen program missing: $GUI"
    now=$(sha "$GUI")
    if [ "$now" = "$STOCK_SHA" ]; then echo "stock screen program"
    elif [ -n "$(installed_sha)" ] && [ "$now" = "$(installed_sha)" ]; then echo "long-press pop-ups installed ($now)"
    else echo "unknown screen program ($now); not managed here"; fi
}

install(){
    dir=$1
    regular "$dir/ec-eeb001-gui" && regular "$dir/manifest.json" || fail "build directory needs ec-eeb001-gui and manifest.json"
    want=$(manifest_sha "$dir/manifest.json" output_sha256)
    input=$(manifest_sha "$dir/manifest.json" input_sha256)
    [ -n "$want" ] && [ "$(sha "$dir/ec-eeb001-gui")" = "$want" ] || fail "build output does not match its manifest"
    [ "$input" = "$STOCK_SHA" ] || fail "build was made for another screen program"
    regular "$GUI" || fail "screen program missing: $GUI"
    now=$(sha "$GUI")
    [ "$now" != "$want" ] || { echo "Already installed."; return 0; }
    [ "$now" = "$STOCK_SHA" ] || fail "the installed screen program is not the stock V4.4.3 one; remove other changes first"
    idle || fail "the printer must be idle"
    mkdir -p "$BASE"
    if ! regular "$BASE/ec-eeb001-gui.stock" || [ "$(sha "$BASE/ec-eeb001-gui.stock")" != "$STOCK_SHA" ]; then
        rm -f "$BASE/ec-eeb001-gui.stock"
        cp "$GUI" "$BASE/ec-eeb001-gui.stock"
        [ "$(sha "$BASE/ec-eeb001-gui.stock")" = "$STOCK_SHA" ] || fail "could not keep a copy of the stock screen program"
    fi
    printf '%s\n' "$want" > "$BASE/installed.sha256"
    replace "$GUI" "$dir/ec-eeb001-gui" "$want" || fail "copy failed; the stock screen program is unchanged"
    echo "INSTALLED. Reboot the idle printer to load the long-press pop-ups."
}

remove(){
    regular "$GUI" || fail "screen program missing: $GUI"
    now=$(sha "$GUI")
    [ "$now" != "$STOCK_SHA" ] || { echo "Already the stock screen program."; return 0; }
    [ -n "$(installed_sha)" ] && [ "$now" = "$(installed_sha)" ] || fail "the screen program was not installed here; nothing changed"
    regular "$BASE/ec-eeb001-gui.stock" && [ "$(sha "$BASE/ec-eeb001-gui.stock")" = "$STOCK_SHA" ] || fail "stock copy missing or changed"
    idle || fail "the printer must be idle"
    replace "$GUI" "$BASE/ec-eeb001-gui.stock" "$STOCK_SHA" || fail "copy failed; nothing changed"
    rm -f "$BASE/installed.sha256"
    echo "REMOVED. Reboot the idle printer to load the stock screen program."
}

case "${1:-}" in
    status) status;;
    install) [ $# -eq 2 ] || fail "usage: manage.sh install BUILD_DIR"; install "$2";;
    remove) remove;;
    *) echo "usage: manage.sh status | install BUILD_DIR | remove" >&2; exit 2;;
esac
