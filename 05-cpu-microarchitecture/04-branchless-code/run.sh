#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

OUT="artifacts/data/branchless_code.csv"

./artifacts/bin/branchless_code --csv-header > "$OUT"

echo "[run] writing to $OUT"

for pattern in random50 mostly_false mostly_true alternating; do
	for mode in branchy branchless; do
		./artifacts/bin/branchless_code \
			--mode "$mode" \
			--pattern "$pattern" \
			--elements 1048576 \
			--repeats 30 \
			--warmup 3 \
			--pin-cpu 0 \
			--csv >> "$OUT"
	done
done

echo "[done] output: $OUT"
