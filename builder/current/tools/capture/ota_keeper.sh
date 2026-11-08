#!/bin/sh
# Keep an official ELEGOO OTA download on your own printer by hard-linking it.
#
# The online update writes the package to /opt/usr/firmware/ and reboots into
# the installer about a minute later, which is too short to copy ~114 MB to a
# USB stick. A hard link in the same file system takes no time and no space;
# when the updater later removes its own name, the data stays reachable under
# KEEP_DIR. /opt/usr survived stock reflashes in issue #309. Copy the kept
# file to the stick after the next Community Firmware flash.
#
# Start it yourself over SSH on Community Firmware, BEFORE tapping
# "Check for updates" (BusyBox on the CC2 has no nohup):
#   sh -c 'trap "" HUP; exec sh /mnt/exUDISK/ota_keeper.sh' >/dev/null 2>/mnt/exUDISK/ota_keeper_err.txt &
#   tail -f /mnt/exUDISK/ota_keeper_log.txt
# Stop it with:  kill "$(cat /opt/usr/ota_keep/.pid)"
#
# It never modifies, moves or deletes files written by the updater. Its only
# writes are hard links and its own log/pid files.
# Overridable for local tests: WATCH_DIR, KEEP_DIR, LOG, INTERVAL.

WATCH_DIR="${WATCH_DIR:-/opt/usr/firmware}"
KEEP_DIR="${KEEP_DIR:-/opt/usr/ota_keep}"
LOG="${LOG:-/mnt/exUDISK/ota_keeper_log.txt}"
INTERVAL="${INTERVAL:-1}"

log() {
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    printf '%s\n' "$line" >> "$KEEP_DIR/keeper.log"
    printf '%s\n' "$line" >> "$LOG" 2>/dev/null
}

mkdir -p "$KEEP_DIR" || { echo "STOP: cannot create $KEEP_DIR" >&2; exit 1; }
if [ -f "$KEEP_DIR/.pid" ] && kill -0 "$(cat "$KEEP_DIR/.pid")" 2>/dev/null; then
    echo "STOP: keeper already running (pid $(cat "$KEEP_DIR/.pid"))" >&2; exit 1
fi
echo $$ > "$KEEP_DIR/.pid"
trap 'rm -f "$KEEP_DIR/.pid"; log "Keeper stopped."; exit 0' INT TERM HUP

# Prove that a hard link from WATCH_DIR to KEEP_DIR works on this file system.
probe_parent="$(dirname "$WATCH_DIR")"
: > "$probe_parent/.ota_keeper_probe" 2>/dev/null
if ! ln "$probe_parent/.ota_keeper_probe" "$KEEP_DIR/.probe" 2>/dev/null; then
    rm -f "$probe_parent/.ota_keeper_probe"
    log "STOP: hard links from $probe_parent to $KEEP_DIR are not possible (different file system?)"
    rm -f "$KEEP_DIR/.pid"; exit 1
fi
rm -f "$probe_parent/.ota_keeper_probe" "$KEEP_DIR/.probe"

log "Keeper started (pid $$). Watching $WATCH_DIR, links in $KEEP_DIR"

while :; do
    for f in "$WATCH_DIR"/* "$WATCH_DIR"/.[!.]*; do
        [ -f "$f" ] && [ ! -L "$f" ] || continue
        name="$(basename "$f")"
        # Already linked? Compare inode numbers of every kept file with this one.
        inode="$(ls -i "$f" 2>/dev/null | awk '{print $1}')"
        [ -n "$inode" ] || continue
        [ -f "$KEEP_DIR/.inode_$inode" ] && continue
        target="$KEEP_DIR/$name"
        [ -e "$target" ] && target="$KEEP_DIR/$(date '+%Y%m%d-%H%M%S')_$name"
        if ln "$f" "$target" 2>/dev/null; then
            : > "$KEEP_DIR/.inode_$inode"
            log "LINKED: $f -> $target (inode $inode, size now $(wc -c < "$f" 2>/dev/null | tr -d ' ') bytes)"
        else
            log "ERROR: could not link $f"
        fi
    done
    sleep "$INTERVAL"
done
