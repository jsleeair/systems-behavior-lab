# 01 – False Sharing

## Objective

This experiment demonstrates the performance impact of **false sharing** in a multi-threaded environment.

False sharing occurs when multiple threads update different variables that reside on the same cache line. Even though the variables are logically independent, the hardware cache-coherence protocol operates at the granularity of cache lines (typically 64 bytes), causing unnecessary invalidations and cache line transfers between cores.

The goal is to:

* Compare performance between:

  * `false` mode (packed counters, likely sharing cache lines)
  * `padded` mode (each counter aligned to its own cache line)
* Observe how the performance gap changes as the number of threads increases.

---

## Experimental Setup

Each thread repeatedly increments its own counter:

* `iters = 200,000,000`
* Thread counts tested: 1, 2, 4, 8, 16
* Thread pinning enabled (`--pin 1`)
* Cache line size assumed: 64 bytes

Two layouts were compared:

### Packed (False Sharing Case)

```c
typedef struct {
    volatile uint64_t value;
} counter_packed_t;
```

Counters are densely packed in memory.
Since each counter is 8 bytes, a 64-byte cache line holds 8 counters.

Therefore:

* Threads 0–7 likely share the same cache line.
* Writes from different cores cause continuous invalidation traffic.

---

### Padded (No False Sharing)

```c
typedef struct __attribute__((aligned(64))) {
    volatile uint64_t value;
    char padding[64 - sizeof(uint64_t)];
} counter_padded_t;
```

Each counter is aligned and padded to occupy a full cache line.

Thus:

* Each thread writes to a separate cache line.
* No coherence conflict between threads.

---

## Results

```
threads=1
  false:  ns_per_op = 0.382
  padded: ns_per_op = 0.379

threads=2
  false:  ns_per_op = 0.640
  padded: ns_per_op = 0.203

threads=4
  false:  ns_per_op = 0.331
  padded: ns_per_op = 0.142

threads=8
  false:  ns_per_op = 0.172
  padded: ns_per_op = 0.082

threads=16
  false:  ns_per_op = 0.170
  padded: ns_per_op = 0.084
```

---

## Observations

### 1 Thread

No significant difference.

* There is no cross-core coherence traffic.
* Layout does not matter when only one core writes.

---

### 2 Threads — Strong False Sharing Effect

```
false  = 0.640 ns/op
padded = 0.203 ns/op
~3.15× slower
```

This is the clearest manifestation of false sharing.

Two cores are repeatedly invalidating the same cache line, causing heavy coherence traffic and cache line ping-pong between cores.

---

### 4–16 Threads

The false-sharing penalty persists:

| Threads | Slowdown (false / padded) |
| ------- | ------------------------- |
| 4       | ~2.33×                    |
| 8       | ~2.10×                    |
| 16      | ~2.02×                    |

Even as total throughput improves due to parallelism, the packed layout remains consistently ~2–3× slower.

This indicates:

* The coherence bottleneck does not disappear.
* The performance penalty scales with cross-core invalidation traffic.
* Once multiple threads share a cache line, the hardware protocol becomes the dominant cost.

---

## Why Does ns/op Decrease at Higher Thread Counts?

The reported metric is:

```
ns_per_op = total_elapsed_time / (threads × iters)
```

As threads increase:

* Total operations increase proportionally.
* Wall-clock time does not increase proportionally.
* Thus, per-operation time decreases.

This reflects improved overall throughput, not reduced coherence cost.

The correct comparison is **false vs padded at the same thread count**, not across different thread counts.

---

## Interpretation

This experiment confirms:

1. Cache coherence operates at cache-line granularity.
2. Independent variables can interfere if they share a cache line.
3. False sharing can easily cause 2–3× performance degradation.
4. Proper alignment/padding eliminates the penalty.

This is a purely hardware-level phenomenon:

* No locks
* No explicit sharing
* No data races
* Only unintended cache-line overlap

---

## Further Verification & Extensions

To strengthen the analysis, the following checks are recommended:

### Print Counter Addresses

Verify that packed counters share the same cache line:

```
line = address / 64
```

Threads 0–7 should map to the same cache-line index in packed mode.

---

### Measure Hardware Events (perf)

Run:

```
perf stat -e cache-references,cache-misses,LLC-load-misses,LLC-store-misses ./false_sharing ...
```

Expected in `false` mode:

* Higher cache misses
* Increased LLC store traffic
* More coherence activity

---

### Disable Pinning

Run with `--pin 0`:

* Results may fluctuate due to OS scheduling.
* Pinning isolates the coherence effect more cleanly.

---

### Increase Iterations

Higher iteration counts reduce measurement noise and turbo-frequency variance.

---

### Test SMT vs Physical Cores

Pin threads:

* Only on physical cores
* Only on sibling hyperthreads

False sharing impact may differ due to shared L1/L2 behavior.

---

## Conclusion

This lab clearly demonstrates that:

> False sharing is not a theoretical edge case — it is a practical and measurable performance hazard.

By merely changing memory layout (without modifying algorithmic logic), we observed up to **3× performance difference**.

This experiment forms a foundational understanding for:

* Lock contention behavior
* Memory ordering costs
* Scalable concurrent data structures
* NUMA-aware programming
* High-performance systems design

---
