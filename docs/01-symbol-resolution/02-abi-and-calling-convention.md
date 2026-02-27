# 02 – ABI and Calling Convention (SysV AMD64)

In this lab, we move from *symbol resolution* to what happens **at runtime**:

> How are function arguments actually passed?  
> How are structures returned?  
> What really happens in a variadic call?

Rather than relying on documentation alone, we verify everything through:

- Runtime register snapshots
- `objdump` inspection
- Comparison between `-O0` and `-O2`

Target platform: **Linux x86_64 (System V AMD64 ABI)**

---

## Why This Matters

Calling conventions define:

- Which registers carry arguments
- When the stack is used
- How large structures are returned
- How variadic calls encode floating-point usage
- How stack alignment must be preserved

If you debug crashes, write profilers, reverse engineer binaries, or build compilers,  
you **must** understand this layer.

---

# Experiment 1 — 8 Integer Arguments

### Code

```c
uint64_t sum_u64_8(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
````

---

## Runtime Result

```
sum_u64_8 result = 0x26664
```

Works in both `-O0` and `-O2`.

---

## Binary Evidence (dbg build)

Call site:

```
11e3: pushq  $0x8888
11e8: pushq  $0x7777
11ed: mov    $0x6666,%r9d
11f3: mov    $0x5555,%r8d
11f9: mov    $0x4444,%ecx
11fe: mov    $0x3333,%edx
1203: mov    $0x2222,%esi
1208: mov    $0x1111,%edi
120d: callq  <sum_u64_8>
1212: add    $0x10,%rsp
```

### Interpretation

* Arguments 1–6 → registers
  `RDI, RSI, RDX, RCX, R8, R9`
* Arguments 7–8 → pushed to stack
* Stack cleaned after call

Inside the callee:

```
mov 0x10(%rbp),%rax
mov 0x18(%rbp),%rax
```

These correspond to `a6` and `a7`.

### Conclusion

SysV AMD64 rule confirmed:

> First 6 integer arguments in registers.
> Remaining arguments on the stack.

---

# Experiment 2 — Struct Return

We tested two structs:

```c
typedef struct {
    uint32_t a;
    uint32_t b;
} Pair32;

typedef struct {
    uint64_t x;
    uint64_t y;
    uint64_t z;
} Triple64;
```

---

## A) Small Struct (Pair32)

Call site:

```
callq <make_pair32>
mov %rax,%rcx
mov %rax,%rdx
```

Returned directly in registers.

### Conclusion

Small structs are returned via registers.

---

## B) Larger Struct (Triple64, 24 bytes)

Call site:

```
lea -0x20(%rbp),%rdi   ; return buffer
movabs ..., %rsi
movabs ..., %rdx
movabs ..., %rcx
callq <make_triple64>
```

The caller allocates memory and passes its address in `RDI`.

Inside callee:

```
mov %rdi, ...
mov %rax,(%rcx)
mov %rdx,0x8(%rcx)
mov %rax,0x10(%rcx)
```

### Conclusion

Large structs use **sret (structure return)**:

* Caller allocates memory
* Pointer passed as hidden first argument
* Callee writes into that buffer

---

# Experiment 3 — Variadic Calls and `%al`

This is where ABI gets subtle.

SysV AMD64 rule:

> In variadic calls, `%al` encodes how many XMM registers were used for floating-point arguments.

We implemented an assembly entry wrapper to capture registers before any compiler prologue.

---

## Double Variadic Call

```c
variadic_observer("d", 4, 1.0, 2.0, 3.5, 4.25);
```

Entry snapshot:

```
rsi = 4
al  = 4
```

Call site:

```
mov $0x4,%eax
callq <variadic_observer>
```

### Meaning

4 doubles → XMM0–XMM3 used → `%al = 4`

Confirmed in both `-O0` and `-O2`.

---

## Integer Variadic Call

```c
variadic_observer("u", 4, 0xA, 0xB, 0xC, 0xD);
```

Entry snapshot:

```
rdx = 0xA
rcx = 0xB
r8  = 0xC
r9  = 0xD
al  = 0
```

Call site:

```
xor %eax,%eax
callq <variadic_observer>
```

### Meaning

No floating-point registers used → `%al = 0`

---

# Stack Alignment

All entry snapshots show:

```
rsp % 16 = 8
```

Explanation:

* Caller ensures 16-byte alignment before `call`
* `call` pushes return address (8 bytes)
* Callee sees `%rsp % 16 == 8`

Correct SysV behavior.

---

# Optimization Effects (-O2)

Under `-O2`:

* Fewer spills
* Reordered instructions
* More direct register usage

But unchanged:

* Register argument rules
* Stack passing for argument 7+
* sret mechanism
* `%al` semantics
* Stack alignment rule

Optimization changes code shape — not ABI contract.

---

# Final Summary

We verified the System V AMD64 ABI through direct binary inspection:

* 6 integer registers, then stack
* Large struct return via hidden pointer (sret)
* `%al` encodes XMM usage in variadic calls
* Stack alignment invariant preserved
* `-O2` preserves ABI semantics

---

## Next Directions

Possible extensions:

* Red zone behavior
* Callee-saved register corruption experiment
* Windows x64 ABI comparison
* Tail-call optimization effects
* Variadic + mixed FP/integer classification edge cases

```
