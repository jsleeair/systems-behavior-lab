
# 02-plt-got-analysis — Result

---

## Question

1. How does a call to `printf` in a dynamically linked executable reach libc?
2. What exactly is stored in the GOT slot before and after the first call?
3. What changes when `LD_BIND_NOW=1` is used?
4. Is `printf@plt` a real function or a trampoline?

---

## Hypothesis

* The executable calls `printf@plt`, not libc `printf` directly.
* The PLT stub performs an indirect jump through a GOT slot.
* The GOT slot is initially unresolved (lazy binding).
* After the first call, the dynamic linker patches the GOT with the real libc address.
* With `LD_BIND_NOW=1`, the GOT is resolved before the first call.

---

## Experimental Setup

* Build flags: `-O0 -g -no-pie -fno-pie`
* Static inspection tools:

  * `readelf -r`
  * `readelf -sW`
  * `objdump -d`
  * `objdump -R`
* Runtime inspection:

  * GDB breakpoint at PLT entry (`0x401040`)
  * Memory inspection of GOT slot (`0x404018`)
  * Comparison between default (lazy) and `LD_BIND_NOW=1`

---

## Build Flags

```
gcc -O0 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -no-pie -fno-pie src/main.c -o build/main
```

Rationale:

* `-O0` for predictable assembly.
* `-g` for debug symbols.
* `-no-pie` for fixed addresses to simplify reasoning.
* `-fno-omit-frame-pointer` for cleaner disassembly.

---

## Measurement

### 1. Relocation entry

From:

```
grep -n "printf" artifacts/static/readelf_r.txt
```

Output:

```
000000404018  000100000007 R_X86_64_JUMP_SLO ... printf@GLIBC_2.2.5
```

Interpretation:

* Relocation type: `R_X86_64_JUMP_SLOT`
* Relocation offset: `0x404018`
* This offset corresponds to the GOT slot for `printf`.

This indicates:

> The dynamic linker must resolve `printf` and write its final address into `0x404018`.

---

### 2. PLT stub disassembly

From:

```
artifacts/disasm/printf_plt_snippet.txt
```

Key section:

```
0000000000401040 <printf@plt>:
  401040: f3 0f 1e fa          endbr64
  401044: f2 ff 25 cd 2f 00 00 bnd jmpq *0x2fcd(%rip)  # 404018 <printf@GLIBC_2.2.5>
```

Interpretation:

* `printf@plt` is located at `0x401040`
* It performs:

```
jmp *0x404018
```

So:

> The PLT entry is an indirect jump via the GOT slot.

It does NOT contain the implementation of `printf`.

---

### 3. all site

From disassembly:

```
401154: e8 e7 fe ff ff   callq 401040 <printf@plt>
```

Meaning:

* `foo()` calls `printf@plt`
* The executable never calls libc directly

---

## Runtime Observation (GDB)

### Lazy binding (default behavior)

GDB breakpoint at `0x401040` (`printf@plt`)
GOT slot inspected at `0x404018`

### First hit (before first call resolution)

```
0x404018 <printf@got.plt>: 0x0000000000401030
```

This value points back into the PLT region (resolver path).

Interpretation:

* The GOT entry is NOT the libc address yet.
* It still points to the PLT resolver mechanism.

---

### Second hit (after first call)

```
0x404018 <printf@got.plt>: 0x00007ffff7e2dc90
```

Interpretation:

* The GOT entry now contains a libc address.
* The dynamic linker resolved `printf` and patched the slot.
* Subsequent calls jump directly to libc.

Observed behavior:

```
hello (1)
hello (2)
```

But the internal control flow differs between first and second call.

---

### Eager binding (`LD_BIND_NOW=1`)

On first hit:

```
0x404018 <printf@got.plt>: 0x00007ffff7e2dc90
```

Interpretation:

* GOT is already resolved before the first call.
* No lazy resolution occurs at first invocation.

---

## Binary Inspection Summary

| Component       | Address        | Role                                |
| --------------- | -------------- | ----------------------------------- |
| printf@plt      | 0x401040       | Indirect trampoline                 |
| printf GOT slot | 0x404018       | Writable pointer patched at runtime |
| libc printf     | 0x7ffff7e2dc90 | Final resolved function             |

---

## Root-Cause Analysis

### Why does this mechanism exist?

The static linker cannot embed the final address of `printf` because:

* `printf` resides in a shared library (`libc.so`)
* The shared library load address is determined at runtime
* ASLR randomizes base addresses

Therefore:

1. The linker creates:

   * A PLT stub (`printf@plt`)
   * A GOT entry (`0x404018`)
   * A `R_X86_64_JUMP_SLOT` relocation

2. At runtime:

   * First call triggers resolver
   * Dynamic linker computes actual address
   * GOT slot is patched
   * Future calls jump directly to libc

Lazy binding is therefore:

> A runtime self-modifying patch of a function pointer inside the GOT.

---

## Instrumentation Pitfall Discovered

Attempting:

```
break *printf@plt
```

failed in GDB because:

* `@plt` labels shown by objdump are synthetic labels.
* They are not guaranteed to be debugger-resolvable symbols.

Correct method:

* Extract concrete addresses from static tools.
* Use address-based breakpoints:

```
break *0x401040
x/gx 0x404018
```

Key methodological lesson:

> Labels are tool interpretations.
> Addresses are ground truth.

---

## Final Insight

This experiment proves concretely:

* The executable does not contain `printf`.
* The PLT is not an implementation, but a trampoline.
* The GOT is a writable function pointer slot.
* Lazy binding is observable as a mutation of the GOT entry from a PLT resolver address to a libc address.

---

## One-Sentence Conclusion

> Dynamic linking is not magic — it is a runtime patch of an indirect jump target stored in the GOT.

---
