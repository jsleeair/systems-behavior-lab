
# Lab Result: 01-relocation-process

## Root-cause analysis

### Why relocations exist in .o

* **Observation:** In the disassembly of `main_O0.o`, the operand addresses for `callq` and `mov` instructions were filled with zeros (`00 00 00 00`).
* **Analysis:** The compiler processes source code in individual **Translation Units**. When compiling `main.c`, it has no knowledge of the final memory addresses for `ext_func` or `ext_var` defined in `ext.c`.
* **Conclusion:** The compiler leaves these addresses as placeholders and generates a **Relocation Entry**—a "to-do list" for the linker to patch these holes once the final memory layout is determined.

---

### Interpreting relocation fields

Based on the mapping between `readelf -r` and `objdump` outputs:

* **`r_offset`**: The exact byte location within a section that needs modification. In this lab, the offset **`0x12`** (the byte immediately following the `call` opcode at `0x11`) was targeted.
* **`r_info` (type + symbol index)**: Specifies which symbol to use (`ext_func`) and how to apply the patch (`R_X86_64_PLT32`).
* **`r_addend` (RELA only)**: A constant value of **** was observed. This is a correction factor for **PC-relative addressing**. Since the CPU's Instruction Pointer (RIP) points to the *next* instruction during execution, the linker must subtract the length of the current instruction (4–5 bytes) to calculate the correct relative distance.
* **Mapping to Disassembly**:
* **Disasm**: `11: e8 00 00 00 00` (The 4 bytes starting at offset `0x12` are zeros).
* **Reloc**: `Offset: 000000000012, Type: R_X86_64_PLT32, Symbol: ext_func - 4`.
* **Interpretation**: "At offset `0x12`, write a 4-byte value equal to the address of `ext_func` minus the current RIP."


---

### Function call vs global variable access

We observed distinct relocation types based on the nature of the symbol reference:

* **Function Call (`ext_func`)**: Uses **`R_X86_64_PLT32`**. This indicates the call might go through the Procedure Linkage Table (PLT), allowing for dynamic linking with shared libraries.
* **Global Variable (`ext_var`)**: Uses **`R_X86_64_PC32`**. The instruction `mov 0x0(%rip), %eax` is patched so that the data is accessed relative to the current instruction's position in memory.

---

### Optimization effects (O0 vs O2)

* **O0 (Debug/No Opt)**: Preserves every function call and variable access exactly as written, resulting in a high number of relocation entries.
* **O2 (Aggressive Opt)**: The compiler may perform **Constant Folding** or **Dead-code elimination**. If the compiler can predict a value or determines a call is unnecessary, the relocation entry is removed entirely.
* **Lab Observation**: Since our references crossed Translation Unit boundaries, the relocations remained in O2, but the `r_offset` values shifted due to a more compact stack frame and instruction scheduling.

---

### What changed after linking

* **Undefined → Resolved**: Symbols that were marked as `UND` (Undefined) in `main.o` now possess concrete addresses (e.g., `0x118d`) in the final `a.out` executable.
* **Relocation Consumption**:
* **Static Link**: Relocations for `ext_func` and `ext_var` were **consumed** by the linker. The linker calculated the offsets and patched the binary directly; thus, these entries disappeared from the relocation table of `a.out`.
* **Dynamic Link**: Symbols like `printf` remain as **Dynamic Relocations** (e.g., `R_X86_64_JUMP_SLOT`). Because the address of `libc.so` is only known at runtime, the static linker delegates this "patching" task to the **Dynamic Loader**.



---
