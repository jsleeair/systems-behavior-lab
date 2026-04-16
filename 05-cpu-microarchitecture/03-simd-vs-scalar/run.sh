#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

mkdir -p artifacts/data
OUT="artifacts/data/simd_vs_scalar.csv"

./artifacts/bin/simd_vs_scalar --csv-header > "$OUT"

echo "[run] writing to $OUT"

for elements in 4096 16384 65536 262144 1048576 4194304; do
	for misalign in 0 4; do
		for mode in scalar_novec scalar_auto avx2; do
			./artifacts/bin/simd_vs_scalar \
				--mode "$mode" \
				--elements "$elements" \
				--repeats 200 \
				--warmup 1 \
				--pin-cpu 0 \
				--misalign-bytes "$misalign" \
				--csv >> "$OUT"
		done
	done
done

echo "[done] output: $OUT"
