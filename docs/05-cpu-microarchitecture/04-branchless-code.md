# 04-branchless-code: Beyond Branch Prediction

## TL;DR

- Branchless code was faster across all input patterns  
- The main benefit was **not just branch misprediction avoidance**  
- The largest gain came from **auto-vectorization (SIMD)**  
- Branchless code creates a **more optimization-friendly execution structure**

---

## Background

A typical conditional loop:

```c
if (data[i] >= threshold) {
    sum += weights[i];
}
```

Branchless version:

```c
sum += (data[i] >= threshold) * weights[i];
```

The common belief is:

> Branchless code is faster because it avoids branch misprediction.

This experiment shows that the real reason is deeper.

---

## Experiment Setup

We evaluated four input patterns:

| Pattern      | Description            |
| ------------ | ---------------------- |
| random50     | ~50% true              |
| mostly_false | rarely true            |
| mostly_true  | almost always true     |
| alternating  | alternating true/false |

Configuration:

* 1,048,576 elements
* 30 repeats
* 3 warmup runs
* CPU pinned
* `-O3 -march=native`

---

## Result 1: Branchy vs Branchless

![Branchy vs Branchless](plots/pattern_vs_mode.png)

### Observations

Branchless outperformed branchy in **all patterns**:

| Pattern      |  Branchy | Branchless |
| ------------ | -------: | ---------: |
| alternating  | ~12.0 ms |   ~10.6 ms |
| mostly_false | ~13.1 ms |   ~10.3 ms |
| mostly_true  | ~12.8 ms |   ~10.7 ms |
| random50     | ~15.8 ms |   ~10.1 ms |

---

### Key Insight

Branchless wins even when branch prediction should be easy:

* mostly_true
* mostly_false
* alternating

👉 This means:

> The performance gain is not only from avoiding misprediction.

---

## Result 2: Vectorization Impact

We disabled vectorization:

```bash
-fno-tree-vectorize
```

![Vectorization Impact](plots/vec_vs_novec.png)

---

### Observations

| Build        | Mode       |      Time |
| ------------ | ---------- | --------: |
| vectorized   | branchy    |  ~15.8 ms |
| vectorized   | branchless |  ~10.6 ms |
| no-vectorize | branchy    | ~198.0 ms |
| no-vectorize | branchless |  ~20.8 ms |

---

## Critical Findings

### 1. Vectorization is a dominant factor

* branchy: **15.8 → 198 ms (12× slower)**
* branchless: **10.6 → 20.8 ms (~2× slower)**

👉 SIMD clearly plays a major role.

---

### 2. Branchless advantage is not only SIMD

Even without vectorization:

```text
branchless ≈ 20.8 ms
branchy    ≈ 198.0 ms
```

👉 ~10× difference remains

---

## Why This Happens

### Branchy code

```text
control dependency
→ unpredictable execution
→ pipeline disruption
→ difficult to vectorize
```

---

### Branchless code

```text
data dependency (mask)
→ straight-line execution
→ pipeline-friendly
→ SIMD-friendly
```

---

## Assembly Evidence

Vectorized branchless code produced instructions like:

```asm
vpcmpeqd
vpand
vpaddq
vpmaskmovd
```

These are classic SIMD mask operations.

---

## Deeper Interpretation

Branchless code transforms:

```text
control flow → data flow
```

This has two effects:

1. Removes branch-based control hazards
2. Enables aggressive compiler optimizations

---

## Key Takeaways

### ❌ Misconception

> Branchless is only about avoiding branch misprediction

---

### ✅ Reality

> Branchless is a structural transformation that enables better execution

---

### Summary

* Branchless removes control-flow dependency
* It enables straight-line execution
* It allows SIMD vectorization
* It improves pipeline utilization

---

## Final Conclusion

> Branchless code is not just a trick to avoid branches —
> it is a transformation that enables SIMD and more efficient execution.

---

