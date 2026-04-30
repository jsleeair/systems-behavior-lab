# 06-performance-analysis / 01-perf-record-hotspot

## Overview

This lab demonstrates how to move from whole-program performance counters to function-level hotspot analysis.

In the previous lab, `perf stat` showed aggregate metrics such as cycles, instructions, branches, and branch misses. That is useful for understanding the overall behavior of a program, but it does not directly answer the next important question:

> Where did the CPU time actually go?

This lab uses `perf record` and `perf report` to answer that question. Instead of only collecting whole-program counter totals, we sample execution and inspect which functions receive the most samples.

The test program intentionally creates several identifiable workloads:

- `compute_hotspot()`: arithmetic-heavy work
- `branch_hotspot()`: branch-heavy work
- `memory_hotspot()`: pseudo-random memory access
- `mixed`: a combined workload that calls compute, memory, and branch routines

The goal is not to build a realistic application. The goal is to create controlled hotspots and verify that `perf record` can identify them.

---

## Build Configuration

The program was compiled with debug symbols and frame-pointer-friendly options:

```makefile
CFLAGS += -fno-omit-frame-pointer
CFLAGS += -fno-optimize-sibling-calls
CFLAGS += -g
```

These options are useful for this lab because the goal is not only to see flat symbols, but also to inspect call paths.

`-fno-omit-frame-pointer` helps frame-pointer based stack unwinding.

`-fno-optimize-sibling-calls` reduces tail-call and sibling-call transformations that can make call graphs harder to read.

After adding this option, the call graph became clearer. For example, `mix_u64` and `compute_hotspot` were shown under `run_compute_mode -> main`, instead of being attributed too directly to `main`.

---

## Experiment

The script ran four modes:

```bash
./run.sh
```

The tested modes were:

```text
compute
branch
memory
mixed
```

Each run produced:

```text
artifacts/data/perf_record_hotspot.csv
artifacts/perf/compute.data
artifacts/perf/compute.report.txt
artifacts/perf/branch.data
artifacts/perf/branch.report.txt
artifacts/perf/memory.data
artifacts/perf/memory.report.txt
artifacts/perf/mixed.data
artifacts/perf/mixed.report.txt
```

The main report inspection commands were:

```bash
head -80 artifacts/perf/compute.report.txt
head -100 artifacts/perf/mixed.report.txt
```

---

## Result: Compute Mode

The compute-mode report showed that most samples were split between `mix_u64` and `compute_hotspot`:

```text
48.45%  mix_u64
48.07%  compute_hotspot
```

The call graph also showed the clearer call path:

```text
mix_u64
  run_compute_mode
  main

compute_hotspot
  run_compute_mode
  main
```

This means the compute workload was successfully attributed to the intended compute path. The helper function `mix_u64()` also became a major hotspot because `compute_hotspot()` repeatedly calls it inside the loop.

This is an important profiling lesson: the top symbol is not always the high-level workload function. Sometimes the cost is inside a helper function called by that workload.

---

## Result: Mixed Mode

The mixed-mode report showed the following major hotspots:

```text
78.78%  memory_hotspot
10.58%  mix_u64
 5.81%  branch_hotspot
 3.50%  compute_hotspot
```

The call graph showed that the main workload functions were called from `run_mixed_mode`:

```text
memory_hotspot
  run_mixed_mode
  main

branch_hotspot
  run_mixed_mode
  main

compute_hotspot
  run_mixed_mode
  main
```

This result is the clearest part of the lab. Even though the mixed workload includes compute, branch, and memory work, the dominant hotspot was `memory_hotspot()`.

The pseudo-random memory access pattern was much more expensive than the arithmetic and branch-heavy sections. Therefore, if this were a real optimization task, the first target would not be `compute_hotspot()` or `branch_hotspot()`. The first target would be the memory access pattern inside `memory_hotspot()`.

---

## Kernel Symbol Warnings

During the run, `perf` printed warnings about restricted kernel address maps and unresolved kernel symbols.

This did not invalidate the lab result.

The goal of this lab was to inspect user-space symbols from the test binary, such as:

```text
compute_hotspot
branch_hotspot
memory_hotspot
mix_u64
```

Those symbols were resolved correctly. Some kernel samples appeared as unknown addresses, but they were small and not central to this experiment.

---

## Observation About Hybrid CPU Events

The report contained separate sections for events such as:

```text
cpu_atom/cycles/P
cpu_core/cycles/P
```

The `cpu_atom` sections had very few samples, while the `cpu_core` sections contained the meaningful data. For example, the mixed-mode `cpu_core` section had 631 samples, while the `cpu_atom` section had only 11 samples.

For this lab, the practical interpretation should be based on the `cpu_core/cycles/P` section.

---

## Key Takeaways

`perf stat` answers:

```text
How many cycles, instructions, branches, and branch misses did the whole program use?
```

`perf record` and `perf report` answer:

```text
Which functions received the samples?
Where did the execution time actually go?
Which call path led to the expensive function?
```

In this lab, the profiling result showed that:

```text
compute mode -> mix_u64 and compute_hotspot
branch mode  -> branch_hotspot and mix_u64
memory mode  -> memory_hotspot
mixed mode   -> memory_hotspot
```

The most important conclusion is:

> In the mixed workload, the dominant bottleneck was pseudo-random memory access, not arithmetic or branch logic.

This is exactly why sampling profilers matter. Whole-program counters can say that a program is expensive, but `perf record` can point to the function that is responsible for that cost.

---

## Conclusion

This lab successfully demonstrated function-level hotspot analysis with `perf record`.

The experiment confirmed that `perf report` can identify both flat hotspots and call paths. After compiling with frame-pointer-friendly options and disabling sibling-call optimization, the call graph became easier to interpret.

The final mixed-mode result showed `memory_hotspot()` as the dominant bottleneck, taking about 79% of sampled cycles. This provides a clear example of how profiling changes the optimization process:

```text
Do not guess where the bottleneck is.
Record it.
Report it.
Then optimize the function that actually dominates the profile.
```
