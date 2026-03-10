#!/usr/bin/env bash
set -euo pipefail

# run.sh
#
# This script builds the benchmark and runs a small sweep for:
#   - same-core parent/child placement
#   - split-core parent/child placement
#
# The output is saved as CSV-like text for later plotting / analysis.
#
# You can override defaults from the environment:
#   ITERS=300000 WARMUP=20000 BASE_CPU=0 REPEATS=5 ./run.sh

ITERS="${ITERS:-200000}"
WARMUP="${WARMUP:-20000}"
BASE_CPU="${BASE_CPU:-0}"
REPEATS="${REPEATS:-5}"

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT_DIR/artifacts/bin/context_switch"
OUT="$ROOT_DIR/artifacts/data/context_switch.csv"

make -C "$ROOT_DIR" all >/dev/null

echo "mode,iterations,warmup,parent_cpu,child_cpu,elapsed_ns,ns_per_roundtrip,ns_per_context_switch_est" > "$OUT"

run_one() {
    local mode="$1"

    local line
    line="$("$BIN" -n "$ITERS" -w "$WARMUP" -c "$BASE_CPU" -m "$mode")"

    # Convert "key=value,key=value,..." into a plain CSV row.
    # Expected order is fixed by the program output format.
    echo "$line" | awk -F',' '
    {
        for (i = 1; i <= NF; i++) {
            split($i, kv, "=")
            gsub(/^[ \t]+|[ \t]+$/, "", kv[2])
            vals[i] = kv[2]
        }
        printf "%s,%s,%s,%s,%s,%s,%s,%s\n",
               vals[1], vals[2], vals[3], vals[4],
               vals[5], vals[6], vals[7], vals[8]
    }' >> "$OUT"
}

echo "[run] ITERS=$ITERS WARMUP=$WARMUP BASE_CPU=$BASE_CPU REPEATS=$REPEATS"

for mode in same split; do
    for ((i = 1; i <= REPEATS; i++)); do
        echo "[run] mode=$mode repeat=$i"
        run_one "$mode"
    done
done

echo "[done] wrote $OUT"
