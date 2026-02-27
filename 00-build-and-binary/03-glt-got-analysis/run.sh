#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT}/build/main"
ART="${ROOT}/artifacts"

make -C "${ROOT}" clean all

echo "== Static analysis =="
readelf -r "${BIN}" > "${ART}/static/readelf_r.txt"
readelf -sW "${BIN}" > "${ART}/static/readelf_s.txt"
objdump -R "${BIN}" > "${ART}/static/objdump_R.txt"

echo "== Disassembly =="
objdump -d "${BIN}" > "${ART}/disasm/full_disasm.txt"
objdump -d "${BIN}" | grep -n "<printf@plt>" -A25 -B5 \
	> "${ART}/disasm/printf_plt_snippet.txt"

echo "== Extract addresses (printf@plt and GOT slot) =="

# 1. Extract printf@plt address from objdump disassembly.
# We look for a line like: "0000000000401040 <printf@plt>:"
PRINTF_PLT_ADDR="$(objdump -d "${BIN}" \
	| awk '/<printf@plt>:/ {print "0x"$1; exit}')"

# 2. Extract GOT slot address for printf from relocation table.
# We look for a line containing "R_X86_64_JUMP_SLOT" and "printf".
# readelf -r typically prints offset in hex without 0x prefix.
PRINTF_GOT_ADDR="$(readelf -rW "${BIN}" \
	| awk '/JUMP_SLOT/ && /printf/ {print "0x"$1; exit}')"

if [[ -z "${PRINTF_PLT_ADDR}" || -z "${PRINTF_GOT_ADDR}" ]]; then
	echo "Failed to extract addresses."
	echo "PRINTF_PLT_ADDR='${PRINTF_PLT_ADDR}'"
	echo "PRINTF_GOT_ADDR='${PRINTF_GOT_ADDR}'"
	exit 1
fi

echo "printf@plt = ${PRINTF_PLT_ADDR}" | tee "${ART}/static/addr_printf_plt.txt"
echo "printf@GOT = ${PRINTF_GOT_ADDR}" | tee "${ART}/static/addr_printf_got.txt"

echo "== Runtime (lazy binding default) =="
"${BIN}" > "${ART}/runtime/run_normal.txt"

echo "== Runtime (LD_BIND_NOW=1) =="
LD_BIND_NOW=1 "${BIN}" > "${ART}/runtime/run_bindnow.txt"

echo "Artifacts stored in ${ART}"

echo "== GDB (lazy binding) log =="

gdb -q "${BIN}" -x "${ROOT}/gdb_lazy.gdb" \
	-ex "set pagination off" \
	-ex "quit" \
	> "${ART}/runtime/gdb_lazy.log" 2>&1 || true

echo "== GDB (bind-now) log =="

LD_BIND_NOW=1 gdb -q "${BIN}" -x "${ROOT}/gdb_bindnow.gdb" \
	-ex "set pagination off" \
	-ex "quit" \
	> "${ART}/runtime/gdb_bindnow.log" 2>&1 || true

echo "== Generate GDB scripts (address-based) =="

cat > "${ART}/runtime/gdb_lazy.gdb" <<EOF
set pagination off
set disassemble-next-line on
set print pretty on

break *${PRINTF_PLT_ADDR}
run

echo \\n=== First hit at printf@plt (${PRINTF_PLT_ADDR}) ===\\n
info registers rip
x/8i \$rip
echo \\n--- GOT slot (${PRINTF_GOT_ADDR}) before/at first call ---\\n
x/gx ${PRINTF_GOT_ADDR}

continue

echo \\n=== Second hit at printf@plt (${PRINTF_PLT_ADDR}) ===\\n
info registers rip
x/8i \$rip
echo \\n--- GOT slot (${PRINTF_GOT_ADDR}) after first call ---\\n
x/gx ${PRINTF_GOT_ADDR}

continue
quit
EOF

cat > "${ART}/runtime/gdb_bindnow.gdb" <<EOF
set pagination off
set disassemble-next-line on
set print pretty on

break *${PRINTF_PLT_ADDR}
run

echo \\n=== Hit at printf@plt (${PRINTF_PLT_ADDR}) with LD_BIND_NOW=1 ===\\n
info registers rip
x/8i \$rip
echo \\n--- GOT slot (${PRINTF_GOT_ADDR}) (should already be resolved) ---\\n
x/gx ${PRINTF_GOT_ADDR}

continue
quit
EOF

echo "== GDB (lazy binding) log =="
gdb -q "${BIN}" -x "${ART}/runtime/gdb_lazy.gdb" \
  > "${ART}/runtime/gdb_lazy.log" 2>&1 || true

echo "== GDB (bind-now) log =="
LD_BIND_NOW=1 gdb -q "${BIN}" -x "${ART}/runtime/gdb_bindnow.gdb" \
  > "${ART}/runtime/gdb_bindnow.log" 2>&1 || true
