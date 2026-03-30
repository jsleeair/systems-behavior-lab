#!/usr/bin/env bash
set -euo pipefail

# run.sh
#
# Runs a small benchmark sweep and stores results as CSV.
#
# Output:
#   artifacts/data/branch_prediction.csv
#
# Optional:
#   PIN_CPU=0 ./run.sh
#   ITERS=50000000 ./run.sh

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT_DIR/artifacts/bin/branch_prediction"
OUT_DIR="$ROOT_DIR/artifacts/data"
OUT_CSV="$OUT_DIR/branch_prediction.csv"

ITERS="${ITERS:-100000000}"
WARMUP="${WARMUP:-1000000}"
PIN_CPU="${PIN_CPU:--1}"
SEED="${SEED:-12345}"
THRESHOLD="${THRESHHOLD:-128}"

mkdir -p "$OUT_DIR"

if [[ ! -x "$BIN" ]]; then
	echo "[build] binary not found, building first..."
	make -C "$ROOT_DIR"
fi

MODES=(
	always_taken
	always_not_taken
	alternating
	random_50
	random_90_taken
	branchless_random_50
)

echo "mode,iters,warmup,threshold,seed,pin_cpu,elapsed_ns,ns_per_iter,sum" > "$OUT_CSV"

for mode in "${MODES[@]}"; do
	echo "[run] mode=$mode iters=$ITERS warmup=$WARMUP pin_cpu=$PIN_CPU"

	# The program prints header+row in --csv mode.
	# We only append the data row (tail -n 1) to avoid duplicate headers.
	"$BIN" \
	  --mode "$mode" \
	  --iters "$ITERS" \
	  --warmup "$WARMUP" \
	  --threshold "$THRESHOLD" \
	  --seed "$SEED" \
	  --pin-cpu "$PIN_CPU" \
	  --csv | tail -n 1 >> "$OUT_CSV"
  done

  echo
  echo "[done] wrote: $OUT_CSV"
  column -s, -t < "$OUT_CSV"
