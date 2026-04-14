#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

mkdir -p artifacts/data

OUT="artifacts/data/loop_unrolling.csv"

./artifacts/bin/loop_unrolling --csv-header > "$OUT"

echo "[run] writing to $OUT"

# Working-set sizes:
# 32 KiB	-> roughly L1-sized
# 256 KiB	-> L2-ish
# 4 MiB		-> much larger, likely LLC / memory-side effects begin to matter
#
# Repeats are adjusted so total work does not explode too much for larger
for array_bytes in 32768 262144 4194304; do
	for unroll in 1 2 4 8 16; do
		repeats=200000

		if [[ "$array_bytes" -eq 262144 ]]; then
			repeats=80000
		elif [[ "$array_bytes" -eq 4194304 ]]; then
			repeats=12000
		fi

		./artifacts/bin/loop_unrolling \
			--array-bytes "$array_bytes" \
			--repeats "$repeats" \
			--unroll "$unroll" \
			--warmup 1 \
			--pin-cpu 0 \
			>> "$OUT"
	done
done

echo "[done] output: $OUT"
