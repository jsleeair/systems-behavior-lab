
# Relocation Process: From Object to Executable

In this lab, we investigate how the compiler and linker collaborate to handle symbols whose addresses are unknown at compile time. We focus on the **Relocation** mechanism in the x86_64 ELF format.

---

## 1. Experimental Setup

We use two separate translation units to force an external symbol reference.

=== "src/main.c"
    ```c
    extern int ext_var;
    extern void ext_func(int a);

    int main() {
        ext_func(10);
        int result = ext_var;
        return 0;
    }
    ```

=== "src/ext.c"
    ```c
    int ext_var = 42;
    void ext_func(int a) {
        // Implementation
    }
    ```

---

## 2. The Evidence: Object File (`.o`)

Before linking, the compiler leaves "holes" in the machine code.

### Binary Inspection (`objdump -dr`)
```text
11: e8 00 00 00 00      callq  16 <main+0x16>  
    12: R_X86_64_PLT32  ext_func-0x4

```

> [!IMPORTANT]
> **Observation:** The `callq` instruction is followed by `00 00 00 00`. This is the **placeholder**. The relocation entry `R_X86_64_PLT32` acts as a sticky note for the linker.

---

## 3. The Resolution: Final Executable (`a.out`)

After linking, the static linker (`ld`) "consumes" the relocation entries and patches the binary.

### Relocation Mapping

| Symbol | Object File (.o) | Executable (a.out) | Status |
| --- | --- | --- | --- |
| `ext_func` | `00 00 00 00` | `2e 00 00 00` | **Patched** (Static) |
| `ext_var` | `00 00 00 00` | `a8 2e 00 00` | **Patched** (Static) |
| `printf` | `00 00 00 00` | `ca fe ff ff` | **Patched** (Dynamic/PLT) |

---

## 4. Root-Cause Analysis

### The PC-Relative Mathematics

The linker calculates the displacement using the following formula:


In our case, the addend of **-4** compensates for the fact that the `RIP` (Instruction Pointer) already points to the *next* instruction when the current one is being executed.

### Optimization Analysis (`-O0` vs `-O2`)

* **-O0**: High relocation count. Every access is explicit.
* **-O2**: More compact. The linker might use different relocation types if symbols are moved closer or optimized into registers.

---

## 5. Conclusion: "Linkers are the ultimate patchers"

The compiler is short-sighted; it only sees its own file. The linker is the architect that sees the whole map and fills in the blanks left by the compiler. Without **Relocation**, modular programming (multi-file projects) would be impossible.

---
