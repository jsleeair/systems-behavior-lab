#!/usr/bin/env bash
set -euo pipefail

# run.sh
#
# Runs the build binaries and prints a compact report.
# Also triggers the expected-failure cases to show the linker errors.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "${ROOT_DIR}"

echo "== Build selected cases =="
make clean >/dev/null 2>&1 || true
make all

echo
echo "== Run: weak var vs strong var =="
./artifacts/bin/weak_strong_var

echo
echo "== Run: weak func vs strong func =="
./artifacts/bin/weak_strong_func

echo
echo "== Build & verify expected link failure: strong vs strong =="
make case_strong_strong

echo
echo "== Build & verify expected link failure: tentative defs with -fno-common =="
make case_common_fnocommon

echo
echo "== Where to look =="
echo " - nm outputs:		artifacts/logs/*nm.txt"
echo " - readelf outputs:	artifacts/logs/*.readelf-s.txt"
echo " - stderr (fail):		artifacts/logs/*.stderr"
