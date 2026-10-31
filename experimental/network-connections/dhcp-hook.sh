#!/bin/sh
# Only the private per-interface generation from the managed client is accepted.
exec /opt/usr/cc2-network-manager/control --lease "${1:-}"
