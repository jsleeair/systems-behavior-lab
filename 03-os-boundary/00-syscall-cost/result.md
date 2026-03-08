# 00-syscall-cost

## Goal

This experiment measures the cost of crossing the **user–kernel boundary** using a minimal system call.

We compare the following four cases:

1. `empty_loop` – baseline loop overhead
2. `function_call` – cost of a normal user-space function call
3. `getpid_wrapper` – libc wrapper (`getpid()`)
4. `direct_syscall_getpid` – direct system call (`syscall(SYS_getpid)`)

The purpose is to understand how expensive a system call is compared to purely user-space execution.

---

# Experimental Setup

The benchmark repeatedly executes a small operation and measures total elapsed time.

Parameters used:

```

iterations = 10,000,000
repeats = 5
CPU pinning = enabled

```

Time measurement uses:

```

clock_gettime(CLOCK_MONOTONIC)

```

Each test reports:

```

elapsed_ns
ns_per_iter

```

Results are recorded in:

```

artifacts/data/syscall_cost.csv

```

---

# Visualization

![syscall cost](plots/syscall_cost.png)

The plot shows the average **nanoseconds per iteration** for each test case in log scale.

---

# Raw Measurement Summary

Across 5 runs, the approximate averages are:

| Mode | Mean (ns per iteration) |
|-----|--------------------------|
| empty_loop | ~0.23 ns |
| function_call | ~0.54 ns |
| getpid_wrapper | ~98 ns |
| direct_syscall_getpid | ~99 ns |

---

# Observations

## 1. User-space execution is extremely cheap

A simple user-space function call costs roughly:

```

~0.5 ns

```

This corresponds to only a few CPU cycles on a modern processor.

---

## 2. System calls are roughly two orders of magnitude slower

A minimal syscall (`getpid`) costs approximately:

```

~100 ns

```

Relative comparison:

```

syscall / function_call ≈ 100 ns / 0.54 ns ≈ 185×

```

Thus, even the simplest syscall is **around two orders of magnitude more expensive** than a normal function call.

---

## 3. libc wrapper overhead is negligible

Comparing:

```

getpid() wrapper
vs
syscall(SYS_getpid)

```

The measured costs are nearly identical:

| Mode | Mean ns |
|-----|---------|
| getpid_wrapper | ~98 |
| direct syscall | ~99 |

This indicates that the dominant cost comes from:

- user → kernel privilege transition
- kernel execution
- kernel → user return

rather than from the libc wrapper itself.

---

## 4. Estimated CPU cycle cost

Assuming a CPU frequency around **3–4 GHz**:

```

1 cycle ≈ 0.25–0.33 ns

```

Therefore:

```

100 ns ≈ 300–400 cycles

```

This aligns with commonly reported syscall entry/exit overhead on modern x86 systems.

---

# Interpretation

The experiment demonstrates a fundamental systems performance principle:

> Crossing the user–kernel boundary is significantly more expensive than executing code entirely within user space.

Even when the kernel performs minimal work, the privilege transition overhead dominates execution time.

Because of this, high-performance systems often attempt to:

- minimize syscall frequency
- batch I/O operations
- buffer reads/writes
- reduce unnecessary kernel interactions

---

# Limitations

This experiment measures the cost of a **minimal syscall (`getpid`)**.

Real system calls may involve additional work such as:

- disk I/O
- memory allocation
- scheduler interaction
- blocking operations

which can significantly increase the total cost.

Additionally, results may vary depending on:

- CPU frequency scaling
- background system activity
- scheduler noise
- kernel version
- hardware architecture

---

# Conclusion

The experiment confirms the large performance gap between user-space execution and system calls:

```

empty loop            ≈ 0.2 ns
function call         ≈ 0.5 ns
minimal syscall       ≈ 100 ns

```

Thus, even the lightest system call is roughly **two orders of magnitude more expensive** than a typical user-space function call.

This overhead originates from the **user–kernel privilege boundary**, making syscall frequency an important consideration in systems performance design.
```

---

