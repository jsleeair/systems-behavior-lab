# 02-abi-and-calling-convention — Results (x86_64 SysV ABI)

## Environment

* Platform: Linux x86_64
* ABI: System V AMD64
* Builds:

  * `abi_dbg`: `-O0 -g3 -fno-omit-frame-pointer`
  * `abi_opt`: `-O2 -g -fno-omit-frame-pointer`

Artifacts examined:

* `artifacts/objdump/abi_dbg.objdump.txt`
* `artifacts/objdump/abi_opt.objdump.txt`
* Runtime logs (`run_dbg.out.txt`, `run_opt.out.txt`)

---

# Experiment 1 — 8 Integer Arguments

### (6 registers + stack)

## Runtime Observation

```
sum_u64_8 result = 0x26664
```

Identical in both `-O0` and `-O2`.

---

## Evidence — Call Site (dbg build)

From `abi_dbg.objdump.txt`:

```
11e3: pushq  $0x8888
11e8: pushq  $0x7777
11ed: mov    $0x6666,%r9d
11f3: mov    $0x5555,%r8d
11f9: mov    $0x4444,%ecx
11fe: mov    $0x3333,%edx
1203: mov    $0x2222,%esi
1208: mov    $0x1111,%edi
120d: callq  13d0 <sum_u64_8>
1212: add    $0x10,%rsp
```

### Interpretation

* First 6 integer arguments → registers
  `RDI, RSI, RDX, RCX, R8, R9`
* 7th and 8th arguments → stack via `push`
* Stack cleaned after call (`add $0x10,%rsp`)

---

## Evidence — Callee (dbg build)

Inside `sum_u64_8`:

```
1417: mov 0x10(%rbp),%rax
141e: mov 0x18(%rbp),%rax
```

These loads correspond to the 7th and 8th arguments.

### Conclusion

SysV AMD64 rule confirmed:

* Integer arguments 1–6 → registers
* Arguments 7+ → stack

---

## Optimized Build Comparison (-O2)

From `abi_opt.objdump.txt`:

```
109f: pushq $0x8888
10b0: pushq $0x7777
10c4: mov    $0x1111,%edi
...
10c9: callq  1350 <sum_u64_8>
```

Even under `-O2`:

* 7th and 8th arguments are still passed via stack.
* ABI rule is preserved.
* Only instruction scheduling order differs.

### Inference

Optimization changes instruction ordering but **does not change ABI contract**.

---

# Experiment 2 — Struct Return

### (Register return vs sret hidden pointer)

We tested:

* `Pair32` (8 bytes)
* `Triple64` (24 bytes)

---

## A) `make_pair32` (small struct)

Call site (opt build):

```
1101: callq 1320 <make_pair32>
1112: mov %rax,%rcx
1115: mov %rax,%rdx
```

Returned value is in `RAX`, split later for printing.

### Conclusion

Small struct returned via register(s).

---

## B) `make_triple64` (24 bytes)

### Call Site (opt build)

```
1123: lea -0x20(%rbp),%rdi
113b: movabs ...,%rsi
1131: movabs ...,%rdx
1127: movabs ...,%rcx
1145: callq 1330 <make_triple64>
```

Key observation:

* `RDI` contains address of stack-allocated buffer.
* That buffer holds the return object.

---

## Callee (opt build)

From `make_triple64`:

```
mov %rdi,-0x28(%rbp)
...
mov %rax,(%rcx)
mov %rdx,0x8(%rcx)
mov %rax,0x10(%rcx)
```

The function writes directly into memory pointed to by `RDI`.

---

## Conclusion

`Triple64` uses **sret (structure return)**:

* Caller allocates return storage.
* Address passed as hidden first argument in `RDI`.
* Callee writes into that memory.

This matches SysV aggregate return rules.

---

# Experiment 3 — Variadic Calls and `%al`

## Design Fix

To observe true entry state:

* Assembly wrapper captures registers immediately at function entry.
* Wrapper tail-jumps to C implementation.
* Prevents probe-induced register corruption.

---

## A) Double Variadic Call

```
variadic_observer("d", 4, 1.0, 2.0, 3.5, 4.25)
```

### Entry Snapshot

```
rsi = 4
rax(entry) = 4
al = 0x04
```

### Call Site Evidence (-O2)

```
11b4: mov $0x4,%eax
11c0: callq 1608 <variadic_observer>
```

### Interpretation

SysV AMD64 rule:

> For variadic calls, `%al` encodes the number of XMM registers used for FP arguments.

4 doubles → XMM0–XMM3 used → `al = 4`

Confirmed in both dbg and opt builds.

---

## B) Integer Variadic Call

```
variadic_observer("u", 4, 0xA, 0xB, 0xC, 0xD)
```

### Entry Snapshot

```
rdx = 0xA
rcx = 0xB
r8  = 0xC
r9  = 0xD
al  = 0
```

### Call Site Evidence (-O2)

```
117d: xor %eax,%eax
11fc: callq 1608 <variadic_observer>
```

### Interpretation

No floating-point registers used → `%al = 0`

---

## Stack Alignment Observation

All calls show:

```
rsp % 16 = 8
```

Explanation:

* Caller ensures 16-byte alignment before `call`.
* `call` pushes return address (8 bytes).
* Callee entry sees `%rsp % 16 == 8`.

Correct SysV behavior.

---

# Optimized Build Differences (-O2)

Observed changes:

* Fewer spills to stack.
* More direct register usage.
* Reordered instruction scheduling.
* Some arithmetic or register moves simplified.

Unchanged:

* Register assignment for arguments.
* Stack passing for arguments 7+.
* sret hidden pointer mechanism.
* `%al` semantics for variadic FP calls.
* Stack alignment rule.

---

# Final Conclusions

This lab confirms, through both runtime inspection and binary analysis:

1. SysV AMD64 integer argument passing:

   * 1–6 → registers
   * 7+ → stack

2. Struct return:

   * Small aggregates → registers
   * Large aggregates → sret (hidden pointer in RDI)

3. Variadic + floating point:

   * `%al` = number of XMM registers used
   * Verified (`4` for 4 doubles, `0` for integer-only)

4. Optimization (`-O2`) alters instruction structure but **does not change ABI semantics**.

---
