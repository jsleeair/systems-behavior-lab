#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts/bin artifacts/data

make

OUT="artifacts/data/ilp_vs_dependency_chain.csv"

# Write a fresh CSV file.
./artifacts/bin/ilp_vs_dependency_chain \
	--mode all \
	--iters 200000000 \
	--repeats 5 \
	--warmup 1 \
	--pin-cpu 0 \
	--csv > "$OUT"

echo
echo "[done] wrote $OUT"
echo
echo "[preview]"
tail -n 20 "$OUT"

