#!/usr/bin/env bash
set -euo pipefail

# This script:
# 1. builds the lab binary
# 2. runs a sweep over multiple working-set sizes
# 3. records results in artifacts/data/cow.csv

# Output CSV columns:
#   mode,pages,page_size,total_bytes,warmup,pin_cpu,elapsed_ns,minflt_delta,majflt_delta,ns_per_page

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

make

OUT="artifacts/data/cow.csv"
: > "$OUT"

echo "mode,pages,page_size,total_bytes,warmup,pin_cpu,elapsed_ns,minflt_delta,majflt_delta,ns_per_page" >> "$OUT"

# Default experiment parameters.
CPU="${CPU:--1}"
WARMUP="${WARMUP:-1}"

# Sweep from 64 pages to 8192 pages by powers of two.
# On a 4 KiB page system, that is 256 KiB to 32 MiB.
for pages in 64 128 256 512 1024 2048 4096 8192; do
	for mode in read write; do
		echo "[run] mode=${mode} pages=${pages} warmup=${WARMUP} cpu=${CPU}"
		PAGES="${pages}" WARMUP="${WARMUP}" CPU="${CPU}" ./artifacts/bin/cow "${mode}" >> "$OUT"
	done
done

echo
echo "[done] wrote ${OUT}"
