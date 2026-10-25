#!/bin/sh
set -eu
cd "$(dirname "$0")"
command -v ssh >/dev/null || { echo 'OpenSSH client required'; exit 1; }
command -v python3 >/dev/null || { echo 'Python 3.10+ required'; exit 1; }
exec python3 start.py
