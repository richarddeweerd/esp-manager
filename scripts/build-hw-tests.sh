#!/usr/bin/env bash
# Build the on-device tests (test/test_hw_*) for one board, without flashing them.
#
# Usage: scripts/build-hw-tests.sh <env> <output-dir>
#
# Each test becomes <output-dir>/<env>/<test>.bin, a full flash image (bootloader, partition
# table and app) to write at offset 0, which is what scripts/run-hw-tests.py expects. A test that
# asks for min_spiffs.csv (test_hw_ota) is built with <env>-ota and also leaves its app image in
# <output-dir>/<env>/app/<test>.bin, which run-hw-tests.py serves as the update.
set -euo pipefail

env="${1:?usage: $0 <env> <output-dir>}"
out="${2:?usage: $0 <env> <output-dir>}"

cd "$(dirname "$0")/.."
mkdir -p "$out/$env"

for dir in test/test_hw_*/; do
    name="$(basename "$dir")"
    build_env="$env"
    if grep -q "min_spiffs.csv" "$dir"*.cpp; then
        build_env="$env-ota"
    fi
    echo "==> $name ($build_env)"
    pio test -e "$build_env" -f "$name" --without-uploading --without-testing
    cp ".pio/build/$build_env/firmware.factory.bin" "$out/$env/$name.bin"
    # A test that updates itself also needs its app image alone, to serve as the update. It goes
    # in a folder of its own, so it isn't taken for a test.
    if [[ "$build_env" != "$env" ]]; then
        mkdir -p "$out/$env/app"
        cp ".pio/build/$build_env/firmware.bin" "$out/$env/app/$name.bin"
    fi
done
