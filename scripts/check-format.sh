#!/usr/bin/env bash
# Fail if any C++ source or example sketch doesn't match .clang-format.
set -euo pipefail

cd "$(dirname "$0")/.."

status=0
while IFS= read -r -d '' file; do
    if [[ "$file" == *.ino ]]; then
        # clang-format doesn't recognise .ino, so format it as C++ through stdin.
        clang-format --dry-run --Werror --assume-filename="${file%.ino}.cpp" < "$file" || status=1
    else
        clang-format --dry-run --Werror "$file" || status=1
    fi
done < <(find src test examples \( -name '*.h' -o -name '*.cpp' -o -name '*.ino' \) -print0)

exit $status
