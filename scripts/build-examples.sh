#!/usr/bin/env bash
# Build every sketch in examples/ for one PlatformIO environment.
#
# Usage: scripts/build-examples.sh <env> [output-dir]
#
# With output-dir, each firmware is copied to <output-dir>/<env>/<example>.bin.
set -euo pipefail

env="${1:?usage: $0 <env> [output-dir]}"
out="${2:-}"

cd "$(dirname "$0")/.."

for dir in examples/*/; do
    name="$(basename "$dir")"
    echo "==> $name ($env)"
    PLATFORMIO_SRC_DIR="$dir" pio run -e "$env"
    if [[ -n "$out" ]]; then
        mkdir -p "$out/$env"
        cp ".pio/build/$env/firmware.bin" "$out/$env/$name.bin"
    fi
done
