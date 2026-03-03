# 02-false-sharing: When Independent Variables Fight Each Other

Modern CPUs maintain cache coherence at the **cache-line granularity** (typically 64 bytes).
This experiment demonstrates how independent variables can severely interfere with each other — purely due to memory layout.

We will see a **2–3× performance difference** caused by false sharing alone.

---

## What Is False Sharing?

False sharing happens when:

* Multiple threads update **different variables**
* Those variables reside on the **same cache line**
* The hardware coherence protocol repeatedly invalidates the line across cores

Even though the data is logically independent, the hardware treats the entire cache line as a single unit.

The result?

**Cache line ping-pong.**

---

## Experimental Design

Each thread increments its own counter:

* 200,000,000 increments per thread
* Thread pinning enabled
* Cache line assumed: 64 bytes
* Thread counts tested: 1, 2, 4, 8, 16

We compare two memory layouts.

---

## Layout 1: Packed (False Sharing Case)

```c
typedef struct {
    volatile uint64_t value;
} counter_packed_t;
```

Each counter is 8 bytes.

Since cache lines are 64 bytes:

* 8 counters fit in one cache line.
* Threads 0–7 likely share the same line.
* Writes from different cores cause continuous invalidation.

---

## Layout 2: Padded (Cache-Line Isolated)

```c
typedef struct __attribute__((aligned(64))) {
    volatile uint64_t value;
    char padding[64 - sizeof(uint64_t)];
} counter_padded_t;
```

Each counter occupies an entire cache line.

* No cross-thread cache-line sharing.
* No coherence ping-pong.

---

## Results

| Threads | Packed (ns/op) | Padded (ns/op) | Slowdown  |
| ------- | -------------- | -------------- | --------- |
| 1       | 0.382          | 0.379          | ~1×       |
| 2       | 0.640          | 0.203          | **3.15×** |
| 4       | 0.331          | 0.142          | 2.33×     |
| 8       | 0.172          | 0.082          | 2.10×     |
| 16      | 0.170          | 0.084          | 2.02×     |

---

## Key Observations

### 1 Thread

No difference.

With only one core writing, coherence does not matter.

---

### 2 Threads — The Clearest False Sharing Case

```
false  = 0.640 ns/op
padded = 0.203 ns/op
```

Two cores repeatedly invalidate the same cache line.

This is pure hardware-level contention — no locks, no shared data structures.

---

### 4–16 Threads

The penalty remains ~2–3×.

Even though overall throughput increases with parallelism, the packed layout consistently performs worse.

This confirms that:

* Coherence cost dominates
* Memory layout alone can dictate scalability

---

## Why Does ns/op Decrease With More Threads?

The metric is:

```
ns_per_op = elapsed_time / (threads × iters)
```

As thread count increases:

* Total operations increase
* Wall-clock time increases sublinearly
* Therefore, ns/op decreases

This reflects improved throughput — not reduced coherence cost.

The important comparison is:

> Packed vs Padded at the same thread count.

---

## What This Experiment Proves

1. Cache coherence operates at cache-line granularity.
2. Independent variables can still interfere.
3. False sharing easily causes 2–3× slowdown.
4. Proper padding eliminates the issue entirely.

This is a memory-layout problem — not an algorithm problem.

---

## How to Strengthen the Analysis

To deepen validation, try:

### Print Counter Addresses

Verify that packed counters map to the same cache line:

```
line = address / 64
```

Threads 0–7 should share a line in packed mode.

---

### Use `perf`

```
perf stat -e cache-references,cache-misses,LLC-store-misses ./false_sharing ...
```

Expected in packed mode:

* Higher cache misses
* More LLC store traffic
* Increased coherence activity

---

### Disable Pinning

```
--pin 0
```

Results may fluctuate due to scheduler migration.

---

### Test Physical vs SMT Cores

Pin threads:

* Only to physical cores
* Only to sibling hyperthreads

False sharing behavior may differ.

---

## Final Takeaway

False sharing is not theoretical.

A simple memory layout decision can cause a **3× performance difference**.

This insight becomes critical when designing:

* Lock-free data structures
* Concurrent queues
* Thread pools
* Per-thread statistics counters
* High-performance systems

Before optimizing algorithms,
make sure your cache lines are not fighting each other.

---
