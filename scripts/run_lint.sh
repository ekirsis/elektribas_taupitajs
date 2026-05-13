#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "=== clang-tidy (PlatformIO / ESP32) ==="
pio check 2>&1 | grep -v "CompileFlags\|^$"

echo ""
echo "=== cppcheck (native_test) ==="
pio check -e native_test 2>&1 | grep -v "^$"

echo ""
echo "=== Format check ==="
scripts/run_format_check.sh
