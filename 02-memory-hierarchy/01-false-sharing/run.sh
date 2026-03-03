#!/usr/bin/env bash
set -euo pipefail

# 01-false-sharing/run.sh
#
# This script runs the experiment across multiple thread counts and writes
# a CSV result file:
#   artifacts/results/results.csv
#
# You can adjust ITERS to change runtime / stability.
# If you want more stable results, increase ITERS.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT_DIR/artifacts/bin/false_sharing"
OUT_DIR="$ROOT_DIR/artifacts/results"
OUT_CSV="$OUT_DIR/results.csv"

mkdir -p "$OUT_DIR"

# Reasonable default: heavy enough to show coherence cost, not too long.
ITERS="${ITERS:-200000000}"
PIN="${PIN:-1}"

# Pick thread counts. We include powers of two up to nproc.
NPROC="$(getconf _NPROCESSORS_ONLN || echo 8)"
THREADS_LIST=()
t=1
while [ "$t" -le "$NPROC" ]; do
  THREADS_LIST+=("$t")
  t=$((t*2))
done

echo "threads,mode,iters,pin,elapsed_ns,ns_per_op,sum" > "$OUT_CSV"

extract_fields() {
  # Input line example:
  # mode=false threads=4 iters=200000000 pin=1 elapsed_ns=123 ns_per_op=0.123 sum=800000000
  local line="$1"
  local mode threads iters pin elapsed ns_per_op sum

  mode="$(echo "$line" | sed -n 's/.*mode=\([^ ]*\).*/\1/p')"
  threads="$(echo "$line" | sed -n 's/.*threads=\([^ ]*\).*/\1/p')"
  iters="$(echo "$line" | sed -n 's/.*iters=\([^ ]*\).*/\1/p')"
  pin="$(echo "$line" | sed -n 's/.*pin=\([^ ]*\).*/\1/p')"
  elapsed="$(echo "$line" | sed -n 's/.*elapsed_ns=\([^ ]*\).*/\1/p')"
  ns_per_op="$(echo "$line" | sed -n 's/.*ns_per_op=\([^ ]*\).*/\1/p')"
  sum="$(echo "$line" | sed -n 's/.*sum=\([^ ]*\).*/\1/p')"

  echo "${threads},${mode},${iters},${pin},${elapsed},${ns_per_op},${sum}"
}

for th in "${THREADS_LIST[@]}"; do
  echo "[run] threads=$th iters=$ITERS pin=$PIN"

  line1="$("$BIN" --threads "$th" --iters "$ITERS" --mode false --pin "$PIN")"
  echo "  $line1"
  extract_fields "$line1" >> "$OUT_CSV"

  line2="$("$BIN" --threads "$th" --iters "$ITERS" --mode padded --pin "$PIN")"
  echo "  $line2"
  extract_fields "$line2" >> "$OUT_CSV"
done

echo
echo "[done] wrote: $OUT_CSV"
