#!/usr/bin/env bash
set -eo pipefail

echo "=== Format check ==="
scripts/run_format_check.sh
echo ""

echo "=== Unit tests ==="
pio test -e native_test -v 2>&1 \
  | sed '/^=======.*SUMMARY/,$d'

