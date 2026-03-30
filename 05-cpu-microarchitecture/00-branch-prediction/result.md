# 00-branch-prediction

## Overview

This experiment measures how branch outcome patterns affect performance on a modern CPU.

We keep the loop body identical and only change the distribution of branch outcomes:

- Fully predictable (always taken / always not taken)
- Deterministic alternating pattern
- Random (50%)
- Biased random (90% taken)
- Branchless equivalent

The goal is to understand whether branches themselves are expensive, or whether the **predictability of branch outcomes** is the dominant factor.

---

## Experiment Setup

- Iterations: 100,000,000
- Warmup: 1,000,000
- CPU pinned: core 0
- Compiler: `-O3 -fno-if-conversion -fno-tree-vectorize`
- Measurement:
  - Wall-clock time (ns)
  - `perf stat` counters:
    - cycles
    - instructions
    - branches
    - branch-misses

---

## Results

### ns per iteration

![ns_per_iter](plots/ns_per_iter.png)

### total elapsed time

![elapsed_ns](plots/elapsed_ns.png)

---

## Raw Data

| mode | ns/iter | elapsed_ns |
|------|--------|-----------|
| alternating | ~0.45 | ~4.48e7 |
| always_taken | ~0.53 | ~5.28e7 |
| always_not_taken | ~0.53 | ~5.27e7 |
| random_90_taken | ~2.06 | ~2.10e8 |
| random_50 | ~5.91 | ~5.91e8 |
| branchless_random_50 | ~6.48 | ~6.48e8 |

---

## Performance Counter Analysis

### predictable branch

```

branch-miss rate ≈ 0.01%
IPC ≈ 3.57

```

### random (50%)

```

branch-miss rate ≈ 15.45%
IPC ≈ 1.10

```

### biased random (90%)

```

branch-miss rate ≈ 3.78%
IPC ≈ 2.34

```

---

## Key Observations

### 1. Predictable branches are extremely cheap

The `always_taken`, `always_not_taken`, and `alternating` modes all execute in ~0.4–0.5 ns per iteration.

This indicates that when the branch predictor can correctly predict outcomes, the branch introduces almost no overhead.

---

### 2. Random branches cause massive slowdown

The `random_50` case is ~10–13× slower than predictable cases.

This is not due to the computation itself, but due to **frequent branch mispredictions**, which:

- flush the pipeline
- waste speculative work
- reduce effective instruction throughput

This is reflected in:

- high branch-miss rate (~15%)
- low IPC (~1.10)

---

### 3. Outcome bias significantly improves performance

The `random_90_taken` case is much faster than `random_50`:

- ~2.06 ns/iter vs ~5.91 ns/iter

Even though the branch is still "random", the predictor can learn the bias and reduce mispredictions.

This demonstrates that **entropy of branch outcomes directly impacts performance**.

---

### 4. Branchless is not always faster

The `branchless_random_50` case is slightly slower than the branch version:

- 6.48 ns vs 5.91 ns

This shows:

> Eliminating branches does not guarantee better performance.

Possible reasons:

- Additional arithmetic and data dependencies
- Less efficient code generation
- Misprediction cost not exceeding branchless overhead on this CPU

---

### 5. IPC strongly correlates with branch predictability

| mode | IPC |
|------|-----|
| predictable | ~3.5 |
| random_90 | ~2.3 |
| random_50 | ~1.1 |

As mispredictions increase, useful work per cycle drops significantly.

---

## Conclusion

This experiment demonstrates that:

> Performance is dominated by branch predictability, not by the existence of branches.

- Predictable branches are nearly free
- Biased branches incur moderate cost
- Unpredictable branches cause severe slowdown

In this benchmark:

- predictable: ~0.45 ns/iter
- biased: ~2.06 ns/iter
- random: ~5.91 ns/iter

This is a **10× performance difference driven purely by control-flow predictability**.

---

## Takeaways

- Do not blindly remove branches
- Focus on making branch outcomes predictable
- Avoid high-entropy conditions in hot loops
- Use branchless techniques only when misprediction cost dominates

---

## Next Step

- `01-ilp-vs-dependency-chain`
- `04-branchless-code`

These experiments extend the analysis into instruction-level parallelism and control-flow elimination tradeoffs.

---
