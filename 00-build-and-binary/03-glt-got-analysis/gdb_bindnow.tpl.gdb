# gdb_bindnow.gdb - observe behavior when lazy binding is disabled
#
# Usage:
#   LD_BIND_NOW=1 gdb -q ./main -x gdb_bindnow.gdb
#
# Expectation:
#   - GOT for printf should already be resolved before the first call.
#   - When you break at printf@plt, the stub's indirect jump should go straight to libc.

set pagination off
set disassemble-next-line on
set print pretty on

break *printf@plt
run

echo \n=== Hit: at printf@plt with LD_BIND_NOW=1 ===\n
info registers rip
x/12i $rip

echo \n--- GOT slot should already contain libc address ---\n
p/x &printf@got.plt
x/gx &printf@got.plt

continue
quit
