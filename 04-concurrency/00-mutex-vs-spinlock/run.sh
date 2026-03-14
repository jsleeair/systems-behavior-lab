#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT_DIR/artifacts/bin/mutex_vs_spinlock"
OUT="$ROOT_DIR/artifacts/data/mutex_vs_spinlock.csv"

mkdir -p "$ROOT_DIR/artifacts/bin" "$ROOT_DIR/artifacts/data"

if [[ ! -x "$BIN" ]]; then
	make -C "$ROOT_DIR"
fi

# Experiment design
# sweep:
#  - mode		: mutex vs spin
#  - threads		: contention level
#  - cs_work		: critical-section length
#  - outside_work	: spacing between lock acquisitions

# Suggested interpretation:
#  cs_work=0, outside_work=0
#    -> extreme lock pressure, very short critical section

# cs_work=100, outside_work=0
#    -> high contention, longer critical setion

# cs_work=0, outside_work=10000
#    -> lower contention because threads spend more time outside the lock

# pin_cpu_base=0 pins threads starting from CPU 0.
# Use -1 if you want scheduler freedom instead.

THREADS_LIST=(1 2 4 8)
CS_WORK_LIST=(0 100 1000)
OUTSIDE_WORK_LIST=(0 1000)
ITERS=1000000
PIN_CPU_BASE=0

echo "mode,threads,iters,cs_work,outside_work,pin_cpu_base,elapsed_ns,ns_per_op,final_count,expected_count" > "$OUT"

for mode in mutex spin; do
	for threads in "${THREADS_LIST[@]}"; do
		for cs_work in "${CS_WORK_LIST[@]}"; do
			for outside_work in "${OUTSIDE_WORK_LIST[@]}"; do
				echo "[run] mode=$mode threads=$threads iters=$ITERS cs_work=$cs_work outside_work=$outside_work pin=$PIN_CPU_BASE" 
				"$BIN" "$mode" "$threads" "$ITERS" "$cs_work" "$outside_work" "$PIN_CPU_BASE" | tee -a "$OUT"
			done
		done
	done
done

echo
echo "[done] wrote results to: $OUT"
