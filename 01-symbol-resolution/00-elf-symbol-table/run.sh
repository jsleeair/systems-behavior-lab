#!/usr/bin/env bash
set -euo pipefail

# This script is the single entrypoint to reproduce:
# - build
# - run output
# - binary-level inspection artifacts
#
# Output files are placed under ./artifacts/

make clean
make all
make run
make inspect

echo "Done. See ./artifacts/ for outputs."
