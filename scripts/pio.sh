#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
export PLATFORMIO_CORE_DIR=${PLATFORMIO_CORE_DIR:-/private/tmp/spool-tap-pio-home}
export UV_CACHE_DIR=${UV_CACHE_DIR:-/private/tmp/spool-tap-uv-cache}
export UV_TOOL_DIR=${UV_TOOL_DIR:-/private/tmp/spool-tap-uv-tools}
exec uvx --offline --from platformio==6.2.0 pio "$@"
