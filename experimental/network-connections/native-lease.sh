#!/bin/sh
ROOT=/opt/usr/cc2-network-manager
if [ -x "$ROOT/control" ] && "$ROOT/control" --managed;then
    case "${1:-}" in
        disable_wlan|enable_wlan) exec /bin/sh "$ROOT/native-wan.sh" "$@";;
        deconfig|renew|bound) exit 0;; # An old native callback cannot overwrite managed routes.
        *) exit 1;;
    esac
fi
exec /bin/sh "$ROOT/native-set_lan.sh" "$@"
