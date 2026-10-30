#!/bin/sh
# Keep the native service's entry point, replace only its global DHCP kill.
ROOT=/opt/usr/cc2-network-manager
if [ -x "$ROOT/control" ] && "$ROOT/control" --managed;then
    case "${1:-}" in
        start|boot) exec "$ROOT/control" --client start wifi;;
        restart|reload) exec "$ROOT/control" --client restart wifi;;
        stop) exec "$ROOT/control" --client stop wifi;;
        enabled|enable|disable) exit 0;;
        *) exit 1;;
    esac
fi
exec /bin/sh "$ROOT/native-udhcpc_wlan0" "$@"
