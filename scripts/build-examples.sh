#!/usr/bin/env bash
# Build every sketch in examples/ for one PlatformIO environment.
#
# Usage: scripts/build-examples.sh <env> [output-dir]
#
# With output-dir, each firmware is copied to <output-dir>/<env>/<example>.bin.
# A sketch that asks for `board_build.partitions = min_spiffs.csv` (such as examples/Updates) is
# built with <env>-ota, which has those bigger app slots.
set -euo pipefail

env="${1:?usage: $0 <env> [output-dir]}"
out="${2:-}"

cd "$(dirname "$0")/.."

for dir in examples/*/; do
    name="$(basename "$dir")"
    build_env="$env"
    if grep -q "board_build.partitions = min_spiffs.csv" "$dir"*.ino; then
        build_env="$env-ota"
    fi
    echo "==> $name ($build_env)"
    PLATFORMIO_SRC_DIR="$dir" pio run -e "$build_env"
    if [[ -n "$out" ]]; then
        mkdir -p "$out/$env"
        cp ".pio/build/$build_env/firmware.bin" "$out/$env/$name.bin"
    fi
done
