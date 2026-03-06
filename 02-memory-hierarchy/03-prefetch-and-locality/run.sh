#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/artifacts/bin/prefetch_locality"
OUT_DIR="$ROOT/artifacts/data"
OUT_CSV="$OUT_DIR/prefetch_locality.csv"

# Build first.
make -C "$ROOT"

mkdir -p "$OUT_DIR"

# You can override these from the shell:
# CPU=2 TARGET_ACCESSES=128000000 ./run.sh
CPU="${CPU:-0}"
TARGET_ACCESSES="${TARGET_ACCESSES:-64000000}"

# Working-set sizes in bytes.
# These intentionally span cache-sized and DRAM-sized regions.
SIZES=(
	32768
	262144
	1048576
	8388608
	67108864
	268435456
)

# Stride in bytes.
# 8B means contiguous uint64_t access.
# 64B roughly means one access per cache line (assuming 64B lines).
STRIDES=(
	8
	16
	32
	64
	128
	256
	512
	1024
	2048
	4096
)

echo "bytes,stride_bytes,pattern,index_count,repeats,accesses,elapsed_ns,ns_per_access,useful_mib_per_s,checksum" > "$OUT_CSV"

for bytes in "${SIZES[@]}"; do
	for stride in "${STRIDES[@]}"; do
		if (( stride > bytes )); then
			continue
		fi

		for pattern in seq random; do
			echo "[run] bytes=$bytes stride=$stride pattern=$pattern cpu=$CPU target_accesses=$TARGET_ACCESSES" >&2

			"$BIN" \
				--bytes "$bytes" \
				--stride "$stride" \
				--pattern "$pattern" \
				--target-accesses "$TARGET_ACCESSES" \
				--cpu "$CPU" \
				>> "$OUT_CSV"
			done
		done
	done

	echo 
	echo "Done."
	echo "CSV written to: $OUT_CSV"
