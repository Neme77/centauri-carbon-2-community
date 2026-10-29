# Bounded local health read for BusyBox without timeout or working wget -T.
# Called only while the private installer lock directory exists.
installer_http_get() (
    umask 077
    http_file="$LOCK/http-read.$$"
    http_reader=
    [ ! -e "$http_file" ] && [ ! -L "$http_file" ] || exit 1
    http_cleanup() {
        trap - 0 1 2 15
        if [ -n "$http_reader" ]; then kill -KILL "$http_reader" 2>/dev/null || true; fi
        rm -f "$http_file"
    }
    trap http_cleanup 0
    trap 'exit 130' 1 2 15
    wget -qO- "$1" > "$http_file" 2>/dev/null &
    http_reader=$!
    http_seconds=0
    while kill -0 "$http_reader" 2>/dev/null; do
        if [ "$http_seconds" -ge 5 ]; then
            echo 'Local status request exceeded five seconds.' >&2
            exit 124
        fi
        sleep 1
        http_seconds=$((http_seconds + 1))
    done
    if wait "$http_reader"; then
        http_reader=
        cat "$http_file"
    else
        http_reader=
        exit 1
    fi
)
