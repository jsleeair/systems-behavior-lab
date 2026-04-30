#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

echo "[build]"
make

mkdir -p artifacts/data
mkdir -p artifacts/perf

BIN="./artifacts/bin/perf_record_hotspot"
CSV_OUT="artifacts/data/perf_record_hotspot.csv"

ITERS="${ITERS:-100000000}"
ARRAY_BYTES="${ARRAY_BYTES:-67108864}"
FREQ="${FREQ:-99}"

echo "[run] program CSV -> ${CSV_OUT}"
"${BIN}" --csv-header > "${CSV_OUT}"

for mode in compute branch memory mixed; do
	echo "== mode=${mode} =="

	"${BIN}" \
		--mode "${mode}" \
		--iters "${ITERS}" \
		--array-bytes "${ARRAY_BYTES}" \
		--warmup 1 \
		--pin-cpu 0 >> "${CSV_OUT}"
done

echo "[run] perf raw files -> artifacts/perf"

if ! command -v perf >/dev/null 2>&1; then
	echo "[warn] perf command not found. Install linux perf tools first."
	echo "[done] CSV only: ${CSV_OUT}"
	exit 0
fi

for mode in compute branch memory mixed; do
	echo "== perf record mode=${mode} =="

	PERF_DATA="artifacts/perf/${mode}.data"
	REPORT_TXT="artifacts/perf/${mode}.report.txt"
	REPORT_CHILDREN_TXT="artifacts/perf/${mode}.children.report.txt"

	perf record \
		-F "${FREQ}" \
		--call-graph fp \
		-o "${PERF_DATA}" \
		-- "${BIN}" \
			--mode "${mode}" \
			--iters "${ITERS}" \
			--array-bytes "${ARRAY_BYTES}" \
			--warmup 1 \
			--pin-cpu 0

	perf report \
		--stdio \
		-i "${PERF_DATA}" \
		--sort comm,dso,symbol \
		--no-children > "${REPORT_TXT}"

	perf report \
		--stdio \
		-i "${PERF_DATA}" \
		--sort comm,dso,symbol \
		--children > "${REPORT_CHILDREN_TXT}"

	echo "[saved] ${PERF_DATA}"
	echo "[saved] ${REPORT_TXT}"
	echo "[saved] ${REPORT_CHILDREN_TXT}"
done

echo "[done]"
echo "CSV: ${CSV_OUT}"
echo "Perf data/report files: artifacts/perf/"
