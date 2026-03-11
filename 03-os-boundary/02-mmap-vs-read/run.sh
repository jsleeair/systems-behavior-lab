#!/usr/bin/env bash
set -euo pipefail

# 03-os-boundary/02-mmap-vs-read/run.sh
#
# This script:
# 1. builds the benchmark
# 2. runs a few configurations
# 3. stores CSV results in artifacts/data/results.csv

# You can override environment variables:
#   FILE_MB=256
#   REPEATS=5
#   CPU=0
#   WARMUP=1

# Example:
#   FILE_MB=512 REPEATS=7 CPU=1 ./run.sh

LAB_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$LAB_ROOT"

mkdir -p artifacts/bin artifacts/data

make all

: "${FILE_MB:=256}"
: "${REPEATS:=5}"
: "${CPU:=0}"
: "${WARMUP:=1}"

OUT_CSV="artifacts/data/results.csv"

echo "mode,iter,file_mb,chunk_kb,stride,elapsed_ns,samples,ns_per_sample,mib_per_sec,checksum" > "$OUT_CSV"

# A few read chunk sizes let us see how syscall frequency changes read() behavior.
# stride is fixed at one page (4096 bytes) for now which keeps user-space work light.
for CHUNK_KB in 4 16 64 256 1024; do
	echo "[run] FILE_MB=$FILE_MB CHUNK_KB=$CHUNK_KB STRIDE_BYTES=4096 REPEATS=$REPEATS CPU=$CPU WARMUP=$WARMUP"
	FILE_MB="$FILE_MB" \
	CHUNK_KB="$CHUNK_KB" \
	STRIDE_BYTES=4096 \
	REPEATS="$REPEATS" \
	CPU="$CPU" \
	WARMUP="$WARMUP" \
	./artifacts/bin/mmap_vs_read | tail -n +2 >> "$OUT_CSV"
done

echo
echo "[done] wrote $OUT_CSV"
echo "[hint] preview:"
tail -n 12 "$OUT_CSV"
