#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

BIN="artifacts/bin/flamegraph_lab"
DATA_DIR="artifacts/data"
REPORT_DIR="artifacts/reports"
TOOLS_DIR="tools"
FG_DIR="$TOOLS_DIR/FlameGraph"

PERF_DATA="$DATA_DIR/perf.data"
PERF_SCRIPT="$DATA_DIR/perf.script.txt"
FOLDED="$DATA_DIR/out.folded"
SVG="$DATA_DIR/flamegraph.svg"
REPORT="$REPORT_DIR/perf.report.txt"

echo "[build]"
make

mkdir -p "$DATA_DIR" "$REPORT_DIR" "$TOOLS_DIR"

if ! command -v perf >/dev/null 2>&1; then
	echo "[error] perf not found. Install linux perf tools for your kernel."
	exit 1
fi

if [ ! -d "$FG_DIR" ]; then
	echo "[setup] FlameGraph tools not found; cloning into $FG_DIR"
	git clone --depth 1 https://github.com/brendangregg/FlameGraph.git "$FG_DIR"
fi

echo "[run] sanity run"
"$BIN" \
	--elements 1048576 \
	--rounds 150 \
	--frontend-weight 2 \
	--backend-weight 3 \
	--cleanup-weight 1

echo "[perf] recording stack samples"
perf record \
	-F 99 \
	-g \
	--call-graph fp \
	-o "$PERF_DATA" \
	-- "$BIN" \
		--elements 1048576 \
		--rounds 80 \
		--frontend-weight 2 \
		--backend-weight 3 \
		--cleanup-weight 1

echo "[perf] writing text report -> $REPORT"
perf report \
	--stdio \
	-i "$PERF_DATA" \
	> "$REPORT"

echo "[perf] expanding samples into stack script -> $PERF_SCRIPT"
perf script \
	-i "$PERF_DATA" \
	> "$PERF_SCRIPT"

echo "[flamegraph] folding stacks -> $FOLDED"
"$FG_DIR/stackcollapse-perf.pl" "$PERF_SCRIPT" > "$FOLDED"

echo "[flamegraph] rendering svg -> $SVG"
"$FG_DIR/flamegraph.pl" \
	--title "CPU Flame Graph: flamegraph_lab" \
	--subtitle "perf record -F 99 -g --call-graph fp" \
	"$FOLDED" > "$SVG"

echo "[done]"
echo "  perf data:     $PERF_DATA"
echo "  perf report:   $REPORT"
echo "  perf script:   $PERF_SCRIPT"
echo "  folded stacks: $FOLDED"
echo "  flame graph:   $SVG"
