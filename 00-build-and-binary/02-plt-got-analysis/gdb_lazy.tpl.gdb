# gdb_lazy.gdb - observe lazy binding via PLT/GOT
#
# Usage:
#   gdb -q ./main -x gdb_lazy.gdb
#
# What you should see conceptually:
#   - First time you stop at printf@plt, the GOT slot for printf is NOT the final libc address yet.
#   - After continuing and hitting printf@plt again (2nd call), the GOT slot should now contain the resolved libc address.
#
# Notes:
#   - We print key address and a few instructions around the PLT stub.
#   - We avoid relying on hard-coded numeric offsets; we query symbols when possible.

set pagination off
set disassemble-next-line on
set print pretty on

# Start by breaking at the PLT stub.
# "printf@plt" is a linker-generated stub symbol in the main executable.
break *printf@plt

run

# We are now at the first call site's entry into PLT.
echo \n=== First hit: at printf@plt (expected: resolver path) ===\n

# Show current instruction pointer.
info registers rip

# Disassemble around the current point to see the PLT stub shape:
# typically: jmp *GOT(%rip) ; push <reloc_index> ; jmp plt0/resolver
x/12i $rip

# Print the address of the GOT entry for printf, then print its current contents.
# In many toolchains you can reference it as "printf@got.plt".
# In your gdb doesn't recognize that symbol, you'll still have objdump/readelf outputs in out/.
echo \n--- GOT slot (printf@got.plt) address and contents ---\n
p/x &printf@got.plt
x/gx &printf@got.plt

# Continue to next breakpoint hit (second call)
continue

echo \n=== Second hit: at printf@plt (expected: direct jump via patched GOT) ===\n
info registers rip
x/12i $rip

echo \n--- GOT slot after first resolution (should now be libc printf address) ---\n
p/x &printf@got.plt
x/gx &printf@got.plt

# Optional: finish execution
continue
quit
