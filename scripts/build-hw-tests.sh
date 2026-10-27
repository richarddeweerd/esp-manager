#!/usr/bin/env bash
# Build the on-device tests (test/test_hw_*) for one board, without flashing them.
#
# Usage: scripts/build-hw-tests.sh <env> <output-dir>
#
# Each test becomes <output-dir>/<env>/<test>.bin, a full flash image (bootloader, partition
# table and app) to write at offset 0, which is what scripts/run-hw-tests.py expects.
set -euo pipefail

env="${1:?usage: $0 <env> <output-dir>}"
out="${2:?usage: $0 <env> <output-dir>}"

cd "$(dirname "$0")/.."
mkdir -p "$out/$env"

for dir in test/test_hw_*/; do
    name="$(basename "$dir")"
    echo "==> $name ($env)"
    pio test -e "$env" -f "$name" --without-uploading --without-testing
    cp ".pio/build/$env/firmware.factory.bin" "$out/$env/$name.bin"
done
