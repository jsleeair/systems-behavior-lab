#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

mkdir -p artifacts/data

BIN="./artifacts/bin/perf_stat_basics"
CSV_OUT="artifacts/data/perf_stat_basics.csv"
PERF_DIR="artifacts/data/perf"

mkdir -p "$PERF_DIR"

"$BIN" --csv-header > "$CSV_OUT"

EVENTS="cycles,instructions,branches,branch-misses,cache-references,cache-misses"

PIN_CPU=0

echo "[run] program CSV -> $CSV_OUT"
echo "[run] perf raw files -> $PERF_DIR"

run_case() {
    local mode="$1"
    local iters="$2"
    local elements="$3"

    echo "== mode=$mode =="

    "$BIN" \
        --mode "$mode" \
        --iters "$iters" \
        --elements "$elements" \
        --pin-cpu "$PIN_CPU" \
        >> "$CSV_OUT"

    perf stat \
        -e "$EVENTS" \
        -o "$PERF_DIR/${mode}.perf.txt" \
        -- "$BIN" \
            --mode "$mode" \
            --iters "$iters" \
            --elements "$elements" \
            --pin-cpu "$PIN_CPU" \
            > "$PERF_DIR/${mode}.program.csv"
}

run_case dep_add              200000000 8388608
run_case indep_add             200000000 8388608
run_case branch_predictable    200000000 8388608
run_case branch_unpredictable  200000000 8388608
run_case memory_seq            16        16777216
run_case syscall_getpid        5000000   8388608

echo "[done]"
echo "  CSV:  $CSV_OUT"
echo "  perf: $PERF_DIR/*.perf.txt"
