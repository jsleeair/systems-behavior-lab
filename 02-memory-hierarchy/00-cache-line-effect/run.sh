#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${ROOT_DIR}/artifacts/bin/cache_line_effect"
OUT="${ROOT_DIR}/artifacts/results.csv"

# Build
make -C "${ROOT_DIR}" all >/dev/null

# Header
echo "size_bytes,stride,mode,dense_bytes,reps,write,elapsed_ns,steps,bytes_touched,checksum,useful_GBps,traffic_GBps" > "${OUT}"
# Pin to a single CPU core to reduce noise (Linux). If taskset is unavailable, it just runs normally.

PIN_CMD=()
if command -v perf >/dev/null 2>&1; then
	USE_PERF=1
fi

# pef (optional)
USE_PERF=0
if command -v perf >/dev/null 2>&1; then
	USE_PERF=1
fi

WRITE_MODE="${WRITE_MODE:-0}"

SIZE_MB="${SIZE_MB:-256}"
REPS="${REPS:-8}"

# Strides to sweep (bytes)
STRIDES=(1 2 4 8 16 32 64 128 256 512 1024 2048 4096)

# Two modes:
#  - sparse: 1 byte per step
#  - dense: 64 bytes per step (roughly "use the whole cache line" if line=64B)
MODES=("sparse" "dense")

echo "[run] SIZE_MB=${SIZE_MB}, REPS=${REPS}"
echo "[run] writing ${OUT}"

for mode in "${MODES[@]}"; do
	for stride in "${STRIDES[@]}"; do
		if [[ "${mode}" == "sparse" ]]; then
			CMD=("${PIN_CMD[@]}" "${BIN}" --size-mb "${SIZE_MB}" --reps "${REPS}" --stride "${stride}" --mode sparse --write "${WRITE_MODE}" --flush once)
		else
			CMD=("${PIN_CMD[@]}" "${BIN}" --size-mb "${SIZE_MB}" --reps "${REPS}" --stride "${stride}" --mode dense --dense-bytes 64 --write "${WRITE_MODE}" --flush once)
		fi

		if [[ "${USE_PERF}" -eq 1 ]]; then
			# Best-effort perf stat. If some events fail, perf may exit non-zero; so we tolerate failures.
			# You can adjust events per your CPU with: perf list | grep -i cache
			PERF_EVENTS="cycles,instructions,cache-references,cache-misses,LLC-loads,LLC-load-misses"
			{
				echo "---- perf: mode=${mode}, stride=${stride} ----" >&2
				perf stat -e "${PERF_EVENTS}" "${CMD[@]}" 2>&1 >/dev/null || true
			} | sed 's/^/# /' >&2
		fi

		# Append CSV line
		echo "[case] mode=${mode}, stride=${stride}" >&2
		"${CMD[@]}" >> "${OUT}"
	done
done

echo "[done] ${OUT}"
echo "Tip: open results.csv and plot elapsed_ns or compute GB/s = bytes_touched / elapsed_ns."

