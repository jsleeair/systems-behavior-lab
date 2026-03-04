#!/usr/bin/env bash
set -euo pipefail

# TLB effect experiment runner
# - Generates CSV and a human-readable header file.
# - Two modes:
#   1) 4KB pages (default)
#   2) THP hint (best-effort) to observe how bigger pages can increase "TLB reach"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT}/artifacts/bin/tlb_effect"
OUTDIR="${ROOT}/artifacts/out"

mkdir -p "${OUTDIR}"

CPU="${CPU:-1}"

# A reasonable sweep:
# - Start small, go up to many pages
# - Step grows moderately so we can see the "knee"
MIN_PAGES="${MIN_PAGES:-16}"
MAX_PAGES="${MAX_PAGES:-65536}"
STEP_PAGES="${STEP_PAGES:-64}"

# Iterations: increase if results look noisy
ITERS="${ITERS:-30000000}"
WARMUP="${WARMUP:-2000000}"

echo "[build]"
make -C "${ROOT}" -s

echo "[run] 4KB-page baseline"
"${BIN}" --cpu "${CPU}" \
  --min-pages "${MIN_PAGES}" --max-pages "${MAX_PAGES}" --step-pages "${STEP_PAGES}" \
  --iters "${ITERS}" --warmup "${WARMUP}" --csv \
  > "${OUTDIR}/tlb_4k.csv"

# Also store with header (non-csv mode prints comments too)
"${BIN}" --cpu "${CPU}" \
  --min-pages "${MIN_PAGES}" --max-pages "${MAX_PAGES}" --step-pages "${STEP_PAGES}" \
  --iters "${ITERS}" --warmup "${WARMUP}" \
  > "${OUTDIR}/tlb_4k.txt"

echo "[run] THP hint (best-effort)"
"${BIN}" --cpu "${CPU}" --thp \
  --min-pages "${MIN_PAGES}" --max-pages "${MAX_PAGES}" --step-pages "${STEP_PAGES}" \
  --iters "${ITERS}" --warmup "${WARMUP}" --csv \
  > "${OUTDIR}/tlb_2m.csv"

"${BIN}" --cpu "${CPU}" --thp \
  --min-pages "${MIN_PAGES}" --max-pages "${MAX_PAGES}" --step-pages "${STEP_PAGES}" \
  --iters "${ITERS}" --warmup "${WARMUP}" \
  > "${OUTDIR}/tlb_2m.txt"

echo "[done]"
echo "Outputs:"
echo "  ${OUTDIR}/tlb_4k.csv"
echo "  ${OUTDIR}/tlb_2m.csv"
