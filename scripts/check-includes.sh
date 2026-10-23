#!/usr/bin/env bash
# Fail if code outside src/platform/ includes Arduino or ESP-IDF headers. Core code reaches the
# hardware only through the interfaces in src/core/, so it can be tested on the host.
set -euo pipefail

cd "$(dirname "$0")/.."

pattern='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](Arduino|WiFi|WebServer|DNSServer|Preferences|esp_[A-Za-z0-9_]*)\.h[>"]'

if grep -rnE --include='*.h' --include='*.cpp' "$pattern" src test --exclude-dir=platform; then
    echo "Only src/platform/ may include the headers above." >&2
    exit 1
fi
