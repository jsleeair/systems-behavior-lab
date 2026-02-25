# 03-plt-got-analysis

In this lab, we investigate how a dynamically linked function call is resolved at runtime.
We focus on the **PLT (Procedure Linkage Table)** and **GOT (Global Offset Table)** mechanism in x86_64 ELF.

---

## Experimental Setup

We deliberately call an external function from libc (`printf`) twice to observe how its address is resolved.

`"src/main.c"`

```c
#include <stdio.h>

static void foo(int n) {
    printf("hello (%d)\n", n);
}

int main(void) {
    foo(1);
    foo(2);
    return 0;
}
```

Build flags:

```
gcc -O0 -g -fno-omit-frame-pointer -no-pie -fno-pie
```

> We disable PIE to keep addresses stable for inspection.

---

## The Evidence: Static Inspection

### 1. Relocation Entry (`readelf -r`)

```text
000000404018  ... R_X86_64_JUMP_SLO ... printf@GLIBC_2.2.5
```

> [!IMPORTANT]
> **Observation:** `printf` has a `R_X86_64_JUMP_SLOT` relocation at offset `0x404018`.
> This offset corresponds to the GOT entry that will be patched at runtime.

---

### 2. PLT Stub (`objdump -d`)

```text
0000000000401040 <printf@plt>:
  401044: jmp *0x404018
```

> [!IMPORTANT]
> **Observation:** `printf@plt` is not the implementation of `printf`.
> It performs an **indirect jump through the GOT slot at 0x404018**.

---

### 3. Call Site

```text
callq 401040 <printf@plt>
```

The executable calls the PLT stub — never libc directly.

---

## The Evidence: Runtime Behavior (GDB)

We set a breakpoint at:

```
break *0x401040
```

and inspect the GOT slot:

```
x/gx 0x404018
```

---

### First Invocation (Lazy Binding)

```
0x404018: 0x0000000000401030
```

The GOT slot does **not** contain the libc address.

It points back into the PLT region (resolver path).

---

### Second Invocation

```
0x404018: 0x00007ffff7e2dc90
```

Now the GOT slot contains the actual libc `printf` address.

This confirms:

1. First call triggers dynamic resolution.
2. Dynamic linker patches the GOT.
3. Subsequent calls jump directly to libc.

---

### Eager Binding (`LD_BIND_NOW=1`)

```
LD_BIND_NOW=1 ./main
```

At the first breakpoint:

```
0x404018: 0x00007ffff7e2dc90
```

The symbol is already resolved before the first call.

---

## Relocation Mapping

| Component     | Address          | Role                      | When Patched      |
| ------------- | ---------------- | ------------------------- | ----------------- |
| `printf@plt`  | `0x401040`       | Indirect trampoline       | Never             |
| GOT slot      | `0x404018`       | Writable function pointer | First call (lazy) |
| libc `printf` | `0x7ffff7e2dc90` | Actual implementation     | N/A               |

---

## Root-Cause Analysis

### Why is this necessary?

The static linker cannot embed the final address of `printf` because:

* `printf` lives in a shared object (`libc.so`)
* The shared library base address is unknown at link time
* ASLR randomizes load addresses

Therefore, the linker generates:

1. A PLT stub (`printf@plt`)
2. A GOT entry (`0x404018`)
3. A `R_X86_64_JUMP_SLOT` relocation

---

### Lazy Binding Mechanism

1. Initial GOT value → points to PLT resolver.
2. First call → dynamic linker resolves symbol.
3. GOT slot is patched with actual libc address.
4. Future calls → direct jump to libc.

This is observable as a **mutation of the GOT slot value**.

---

## Instrumentation Pitfall Discovered

`objdump` shows labels like:

```
<printf@plt>
```

However, GDB does not always recognize `printf@plt` as a resolvable symbol.

Correct method:

* Extract concrete addresses from static tools.
* Use address-based breakpoints:

```
break *0x401040
x/gx 0x404018
```

> Tool labels are representations.
> Addresses are ground truth.

---

## Conclusion: PLT is a Trampoline, GOT is a Patch Point

The executable does not contain `printf`.
The PLT performs an indirect jump.
The GOT is patched at runtime.
Lazy binding is directly observable as a change in the GOT entry.

---

## Final Insight

> Dynamic linking is not magic — it is a runtime patch of an indirect jump target stored in the GOT.

---
