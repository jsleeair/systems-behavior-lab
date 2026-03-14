#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

mkdir -p artifacts/bin artifacts/data
make

OUT="artifacts/data/memory_ordering.csv"

ITERS="${ITERS:-200000}"
WARMUP="${WARMUP:-5000}"
REPEATS="${REPEATS:-5}"

CPU_COUNT="$(getconf _NPROCESSORS_ONLN || echo 1)"
CPU0=0
if [[ "${CPU_COUNT}" -ge 2 ]]; then
	CPU1=1
else
	CPU1=0
fi

echo "mode,iterations,warmup,cpu0,cpu1,elapsed_ns,ns_per_trial,count_00,count_01,count_10,count_11" > "$OUT"

for mode in relaxed acqrel seqcst; do
	for rep in $(seq 1 "$REPEATS"); do
		echo "[run] mode=${mode} rep=${rep} iters=${ITERS} warmup=${WARMUP} cpu0=${CPU0} cpu1=${CPU1}"
		./artifacts/bin/memory_ordering \
		--mode "$mode" \
		--iters "$ITERS" \
		--warmup "$WARMUP" \
		--cpu0 "$CPU0" \
		--cpu1 "$CPU1" >> "$OUT"
	done
done

echo
echo "[done] wrote $OUT"
column -s, -t < "$OUT"
