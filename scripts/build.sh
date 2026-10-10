#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
umask 077
op run --env-file=.env.tpl -- python3 scripts/gen_secrets.py
unset WIFI_SSID WIFI_PASSWORD OTA_PASSWORD
if [ "$#" -eq 0 ]; then set -- run -e cores3; fi
exec scripts/pio.sh "$@"
