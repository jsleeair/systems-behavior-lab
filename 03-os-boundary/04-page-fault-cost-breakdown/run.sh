#!/usr/bin/env bash
set -euo pipefail

echo "[build]"
make

mkdir -p artifacts/data

echo "[run]"
./artifacts/bin/page_fault_cost_breakdown \
	--pages 4096 \
	--repeats 5 \
	--warmup 1 \
	--pin-cpu 0 \
	--csv artifacts/data/page_fault_cost_breakdown.csv

echo
echo "[preview]"
head -n 20 artifacts/data/page_fault_cost_breakdown.csv
