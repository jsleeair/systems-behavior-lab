#!/usr/bin/env bash
set -euo pipefail

mkdir -p artifacts/data

echo "[build]"
make

OUT=artifacts/data/stride_vs_cache_miss.csv

echo "[run]"
./artifacts/bin/stride_vs_cache_miss \
  --pin-cpu 0 \
  --repeats 5 \
  --warmup 1 \
  --target-accesses 100000000 \
  --max-stride-bytes 4096 \
  > "$OUT"

echo "[done] output: $OUT"
