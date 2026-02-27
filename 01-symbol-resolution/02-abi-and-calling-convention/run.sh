#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[1/3] Build + generate artifacts"
make clean
make artifacts

echo
echo "[2/3] Run dbg binary"
./artifacts/bin/abi_dbg | tee ./artifacts/run_dbg.out.txt

echo
echo "[3/3] Run opt binary"
./artifacts/bin/abi_opt | tee ./artifacts/run_opt.out.txt

echo
echo "Done. Check artifacts/:"
echo " - artifacts/run_*.out.txt"
echo " - artifacts/asm/*.s"
echo " - artifacts/objdump/*.txt"
echo " - artifacts/readelf/*.txt"
echo " - artifacts/nm/*.txt"
