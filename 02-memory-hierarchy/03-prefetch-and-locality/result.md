# 03 — Prefetch and Locality

## Goal

This experiment investigates how **memory access patterns** influence performance.

Specifically we compare:

- **Sequential access**
- **Random access**

while varying **stride size**.

The experiment highlights the impact of:

- spatial locality
- hardware prefetching
- cache-line utilization

---

# Experimental Setup

CPU: x86_64 laptop  
Compiler: gcc -O3  
Pinned CPU: core 0  

Working set sizes:

| Size | Purpose |
|-----|------|
| 32KB | L1-sized |
| 1MB | L2/L3 region |
| 64MB | DRAM |

Stride sizes:


8B → 4096B


Each experiment performs approximately **64 million accesses**.

---

# Results

## 32KB Working Set (L1 region)

![prefetch](artifacts/plots/prefetch_32768.png)

Observations:

- Latency remains low across most stride values.
- Both sequential and random access perform similarly.
- The entire dataset fits inside the L1 cache.

Interpretation:

When the working set fits inside L1, **memory hierarchy effects are minimal**.

---

## 1MB Working Set (L2/L3 region)

![prefetch](artifacts/plots/prefetch_1048576.png)

Observations:

- Sequential access maintains relatively stable latency.
- Random access begins to show higher latency.
- Larger strides reduce performance even for sequential traversal.

Interpretation:

The dataset exceeds L1 capacity, so **cache-line reuse and prefetch behavior begin to matter**.

---

## 64MB Working Set (DRAM)

![prefetch](artifacts/plots/prefetch_67108864.png)

Observations:

- Sequential access performs significantly better.
- Random access latency increases sharply.
- Large strides dramatically reduce performance.

Interpretation:

At DRAM scale:

- Hardware prefetchers benefit **predictable access patterns**.
- Random access prevents latency hiding.
- Large strides waste cache-line bandwidth.

---

# Key Takeaways

1. **Sequential traversal is critical for performance**

Predictable patterns allow hardware prefetchers to hide memory latency.

---

2. **Random access destroys prefetch efficiency**

Without predictable patterns, every access may incur full DRAM latency.

---

3. **Stride matters**

Large strides reduce spatial locality because each cache line is underutilized.

---

# Limitations

This experiment does not directly measure hardware prefetch events.

Additionally:

- index traversal introduces minor overhead
- results may vary depending on microarchitecture

---

# Conclusion

This lab demonstrates that **memory access patterns strongly influence real-world performance**.

Even when performing the same number of loads:

- sequential traversal can achieve significantly lower latency
- random access may expose full DRAM latency

Understanding these effects is essential when designing high-performance data structures and algorithms.
