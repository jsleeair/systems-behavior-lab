#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

CSV="artifacts/data/ring_buffer.csv"

echo "[build]"
make

mkdir -p artifacts/data
rm -f "$CSV"

MESSAGES="${MESSAGES:-1000000}"
REPEATS="${REPEATS:-5}"
WARMUP="${WARMUP:-1}"
PRODUCER_CPU="${PRODUCER_CPU:-0}"
CONSUMER_CPU="${CONSUMER_CPU:-1}"

echo "[run]"
echo "[summary] messages=$MESSAGES repeats=$REPEATS warmup=$WARMUP producer_cpu=$PRODUCER_CPU consumer_cpu=$CONSUMER_CPU"

for mode in packed padded; do
	for capacity in 64 256 1024 4096 16384 65536; do
		echo "== mode=$mode capacity=$capacity =="
		./artifacts/bin/ring_buffer \
		--mode "$mode" \
		--capacity "$capacity" \
		--messages "$MESSAGES" \
		--repeats "$REPEATS" \
		--warmup "$WARMUP" \
		--producer-cpu "$PRODUCER_CPU" \
		--consumer-cpu "$CONSUMER_CPU" \
		--csv "$CSV"
	done
done

echo "[done] output: $CSV"
