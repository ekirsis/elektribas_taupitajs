#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "Checking C++ formatting with clang-format..."
status=0

while IFS= read -r f; do
    if ! clang-format "$f" | diff -q "$f" - > /dev/null 2>&1; then
        echo "FAIL: $f"
        status=1
    fi
done < <(find src include test -name '*.cpp' -o -name '*.h' \
    ! -name 'doctest.h' \
    ! -name 'wifi_setup_page.h')

if [ $status -eq 0 ]; then
    echo "Formatting OK"
else
    echo ""
    echo "Some files have formatting issues. Run: clang-format -i <file>"
    exit 1
fi
