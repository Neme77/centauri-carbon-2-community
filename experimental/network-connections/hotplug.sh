#!/bin/sh
[ "${ACTION:-}" = add ] && [ "${INTERFACE:-${DEVICE:-}}" = cc2lan0 ] || exit 0
# Reconnect only an enabled interface; the saved manual selection is unchanged.
exec /opt/usr/cc2-network-manager/control --hotplug ethernet
