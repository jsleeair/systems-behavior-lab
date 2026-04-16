# 03-SIMD-vs-Scalar

## Overview

In this experiment, we compare three implementations of a simple SAXPY-style kernel:

```

y[i] = a * x[i] + y[i]

```

We evaluate:

- `scalar_novec`: Scalar loop with auto-vectorization disabled
- `scalar_auto`: Plain scalar loop (compiler may auto-vectorize)
- `avx2`: Manual SIMD implementation using AVX2 intrinsics

The goal is not just to show that SIMD is faster, but to answer:

> **When does SIMD actually matter, and when does it stop helping?**

---

## Experiment Setup

- Language: C
- Compiler: GCC (`-O3 -march=native -mavx2 -mfma`)
- Data type: `float`
- Kernel: SAXPY (`y = a*x + y`)
- Metrics:
  - `ns_per_element`
  - Speedup over `scalar_novec`

Parameters:

- Elements: 4K → 4M
- Misalignment: 0 bytes (aligned), 4 bytes (misaligned)
- CPU pinned to a single core

---

## Results

### 1. Absolute Performance (Aligned)

![ns_per_element aligned](plots/simd_vs_scalar_ns_per_element_misalign_0.png)

### Observations

- `scalar_novec` is consistently the slowest (~1.0 ns/element)
- `scalar_auto` achieves large speedup (~0.15 ns initially)
- `avx2` is slightly faster than `scalar_auto`

At small sizes (L1/L2 cache):

- `avx2` achieves up to **~10× speedup** over scalar baseline

At large sizes:

- Performance converges (~0.45–0.50 ns)
- SIMD advantage becomes small

---

### 2. Absolute Performance (Misaligned)

![ns_per_element misaligned](plots/simd_vs_scalar_ns_per_element_misalign_4.png)

### Observations

- Misalignment significantly hurts performance in small arrays
- Both `scalar_auto` and `avx2` degrade noticeably

Example:

- AVX2:
  - aligned: ~0.11 ns
  - misaligned: ~0.22 ns (**~2× slower**)

However:

- At large sizes, misalignment effect diminishes

---

### 3. Speedup vs Scalar Baseline (Aligned)

![speedup aligned](plots/simd_vs_scalar_speedup_misalign_0.png)

### Observations

- Small arrays:
  - `avx2`: ~10× speedup
  - `scalar_auto`: ~7× speedup

- Medium arrays:
  - Speedup drops to ~4–5×

- Large arrays:
  - Speedup collapses to ~1.5–2×

---

### 4. Speedup vs Scalar Baseline (Misaligned)

![speedup misaligned](plots/simd_vs_scalar_speedup_misalign_4.png)

### Observations

- Initial speedup is much lower (~3–4×)
- Converges to ~1.5× for large arrays

---

## Key Insights

### 1. SIMD is highly effective in compute-bound regions

For small working sets (L1/L2 cache):

- The kernel is compute-bound
- SIMD dramatically improves throughput
- Up to **10× speedup** is observed

---

### 2. Memory bandwidth quickly becomes the bottleneck

As array size increases:

- The working set exceeds cache
- DRAM access dominates

At this point:

> Performance is limited by memory bandwidth, not compute

As a result:

- SIMD provides diminishing returns
- All implementations converge

---

### 3. Compiler auto-vectorization is already very effective

Comparing:

- `scalar_auto` vs `avx2`

We observe:

- Only small improvements from manual AVX2
- In some regions, nearly identical performance

This implies:

> Modern compilers already capture most SIMD opportunities in simple loops

---

### 4. Alignment matters — but only in cache-resident workloads

- Misalignment causes noticeable slowdown in small arrays
- Particularly affects SIMD loads/stores

However:

- Once memory-bound, alignment has minimal impact

---

## Interpretation

This experiment highlights a key principle in performance engineering:

> **Optimization must target the actual bottleneck.**

- SIMD reduces computation cost
- But if memory access dominates, it cannot help much

In other words:

```

Performance ≈ min(compute throughput, memory bandwidth)

```

SIMD improves only the compute side.

---

## Takeaways

- SIMD can provide large speedups, but only when compute is the bottleneck
- Memory hierarchy plays a critical role in real performance
- Compiler auto-vectorization is often sufficient
- Manual SIMD is most useful when:
  - Compiler fails to vectorize
  - Fine-grained control is required

---
