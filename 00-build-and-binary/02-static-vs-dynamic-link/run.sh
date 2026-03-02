#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="${ROOT_DIR}/artifacts/bin"
LIB_DIR="${ROOT_DIR}/artifacts/lib"
LOG_DIR="${ROOT_DIR}/artifacts/logs"

DYN_BIN="${BIN_DIR}/main_dynamic"
STATICL_BIN="${BIN_DIR}/main_staticlib"

mkdir -p "${LOG_DIR}"

echo "[run] Running binaries..."
echo "---- main_dynamic ----"
"${DYN_BIN}" "dynamic"
echo
echo "---- main_staticlib ----"
"${STATICL_BIN}" "staticlib"
echo

echo "[run] Collecting quick outputs..."
{
	echo "### size"
	size "${DYN_BIN}" "${STATICL_BIN}"
	echo
	echo "### ldd main_dynamic"
	ldd "${DYN_BIN}"
	echo
	echo "### readelf -d main_dynamic"
	readelf -d "${DYN_BIN}"
	echo
	echo "### readelf -d main_staticlib"
	readelf -d "${STATICL_BIN}"
} > "${LOG_DIR}/run_summary.txt"

echo "[run] Wrote ${LOG_DIR}/run_summary.txt"

echo
echo "[run] Extra experiment: prove the dynamic binary depends on libgreet.so at runtime."
echo "      We'll temporarily hide libgreet.so and show that main_dynamic fails."
TMP_SO="${LIB_DIR}/libgreet.so.__hidden__"

if [[ -f "${LIB_DIR}/libgreet.so" ]]; then
	mv "${LIB_DIR}/libgreet.so" "${TMP_SO}"
	set +e
	"${DYN_BIN}" "should-fail" 1> "${LOG_DIR}/dyn_fail_stdout.txt" 2> "${LOG_DIR}/dyn_fail_stderr.txt"
	RET=$?
	set -e
	echo "[run] main_dynamic exit code (expected non-zero): ${RET}"
	echo "[run] stdout/stderr saved to logs/dyn_fail_*.txt"
	mv "${TMP_SO}" "${LIB_DIR}/libgreet.so"
else
	echo "[run] WARNING: libgreet.so not found; skipping hide test."
fi

echo
echo "[run] Static-lib-linked binary should still work even if libgreet.so is missing."
# To emphasize, we do NOT need libgreet.so for main_staticlib.
"${STATICL_BIN}" "works-without-so" | tee "${LOG_DIR}/staticlib_works.txt" >/dev/null

echo "[run] Done."
