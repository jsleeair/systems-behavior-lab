# 06-01. `perf record` Hotspot Analysis

## 1. Why This Lab Exists

In the previous lab, I used `perf stat` to collect whole-program performance counters.

That kind of result is useful, but it only answers questions like:

- How many cycles did the program use?
- How many instructions retired?
- How many branches were executed?
- How many branch misses occurred?

That is not enough when the next question is:

> Which function actually consumed the time?

This lab is about moving from **whole-program counters** to **function-level hotspot analysis**.

The main tool is:

```bash
perf record
perf report
```

`perf record` samples the running program and writes profiling data into a `perf.data` file.

`perf report` reads that file and shows where the samples landed.

The difference is simple:

```text
perf stat   -> How expensive was the program overall?
perf record -> Where did the cost happen?
```

---

## 2. Lab Structure

The lab directory is:

```text
06-performance-analysis/01-perf-record-hotspot
├── Makefile
├── run.sh
├── src
│   └── perf_record_hotspot.c
└── artifacts
    ├── bin
    ├── data
    └── perf
```

The program intentionally creates several visible hotspots:

```text
compute_hotspot()  -> arithmetic-heavy work
branch_hotspot()   -> branch-heavy work
memory_hotspot()   -> pseudo-random memory access
mixed              -> combines compute, memory, and branch work
```

The point is not to build a realistic application.

The point is to create a controlled workload where `perf record` can show which function dominates execution.

---

## 3. Build Options

For this lab, the program was compiled with profiling-friendly options:

```makefile
CFLAGS += -fno-omit-frame-pointer
CFLAGS += -fno-optimize-sibling-calls
CFLAGS += -g
```

These options matter because this lab cares about call graphs.

`-fno-omit-frame-pointer` helps `perf` unwind the stack using frame pointers.

`-fno-optimize-sibling-calls` reduces tail-call and sibling-call optimizations that can make the call graph look confusing.

`-g` keeps debug information in the binary.

The final build flags are not only about performance. They are also about making the profiling result easier to read.

---

## 4. Running the Lab

The lab was executed with:

```bash
./run.sh
```

The script performs two jobs.

First, it runs the program directly and writes the timing result to CSV:

```text
artifacts/data/perf_record_hotspot.csv
```

Second, it runs `perf record` for each mode and stores the profiling data:

```text
artifacts/perf/compute.data
artifacts/perf/branch.data
artifacts/perf/memory.data
artifacts/perf/mixed.data
```

It also generates text reports:

```text
artifacts/perf/compute.report.txt
artifacts/perf/branch.report.txt
artifacts/perf/memory.report.txt
artifacts/perf/mixed.report.txt
```

The main inspection commands were:

```bash
head -80 artifacts/perf/compute.report.txt
head -100 artifacts/perf/mixed.report.txt
```

---

## 5. First Result: Compute Mode

The compute-mode report showed this result:

```text
48.45%  mix_u64
48.07%  compute_hotspot
```

This is a useful result.

At first glance, I might expect `compute_hotspot()` to dominate everything. But `mix_u64()` also appears as a major hotspot because `compute_hotspot()` repeatedly calls it inside its loop.

The call graph became clearer after disabling sibling-call optimization:

```text
mix_u64
  run_compute_mode
  main

compute_hotspot
  run_compute_mode
  main
```

This is one of the first important lessons of the lab:

> The expensive code may be inside a helper function, not directly inside the high-level function name.

If I only looked at the source code casually, I might say, "`compute_hotspot()` is the expensive function."

But `perf report` shows a more precise picture:

```text
compute_hotspot() spends a large part of its cost in mix_u64().
```

That is already better than guessing.

---

## 6. Second Result: Branch Mode

The branch-mode report showed that the main cost was split between the branch workload and the mixing helper:

```text
branch_hotspot
mix_u64
```

This makes sense.

`branch_hotspot()` contains the branch-heavy control flow, but the branch condition is driven by values generated through `mix_u64()`.

So the result is not simply:

```text
branch_hotspot is expensive
```

It is more accurate to say:

```text
branch_hotspot dominates the branch-mode workload,
and mix_u64 is also expensive because it feeds the unpredictable branch pattern.
```

This is another useful profiling lesson.

A workload name and a hotspot name are not always identical. A workload can be expensive because of helper routines that support the behavior being tested.

---

## 7. Third Result: Memory Mode

The memory-mode report was the clearest single-mode result:

```text
95.94%  memory_hotspot
 3.44%  mix_u64
```

This means almost all sampled cycles landed in `memory_hotspot()`.

That is exactly what this mode was designed to show.

The function performs pseudo-random accesses over a large array. This is hostile to locality. It is much harder for the cache hierarchy and hardware prefetchers than a simple sequential scan.

The result is very different from a tight arithmetic loop.

The cost is not just "doing instructions."

The cost comes from the memory access pattern.

This is the important distinction:

```text
Sequential memory access     -> cache-friendly
Pseudo-random memory access  -> cache-unfriendly
```

In this mode, `perf record` clearly identifies `memory_hotspot()` as the bottleneck.

---

## 8. Fourth Result: Mixed Mode

The mixed-mode report is the most important part of the lab.

The result was:

```text
78.78%  memory_hotspot
10.58%  mix_u64
 5.81%  branch_hotspot
 3.50%  compute_hotspot
```

The call graph also showed the expected structure:

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

This is the key result.

The mixed workload contains compute work, branch work, and memory work. But the dominant hotspot is still `memory_hotspot()`.

So if this were a real application, the optimization target would be obvious.

I should not start by optimizing `compute_hotspot()`.

I should not start by rewriting the branch logic.

The first target should be the memory access pattern.

The profile says:

```text
The mixed workload is dominated by pseudo-random memory access.
```

This is the practical value of `perf record`.

It turns a vague performance problem into a concrete target.

---

## 9. About the Kernel Symbol Warnings

During the run, `perf` printed warnings like:

```text
Kernel address maps are restricted
Samples in kernel functions may not be resolved
```

For this lab, that warning is not a blocker.

The purpose of the experiment is to analyze user-space symbols from my own binary:

```text
compute_hotspot
branch_hotspot
memory_hotspot
mix_u64
```

Those symbols were resolved correctly.

Some kernel samples appeared as unknown addresses, but they were small and not central to this experiment.

So the warning means:

```text
Kernel symbols may not be readable.
```

It does not mean:

```text
The user-space profile failed.
```

For this lab, the user-space result is enough.

---

## 10. About Hybrid CPU Events

The report contained sections such as:

```text
cpu_atom/cycles/P
cpu_core/cycles/P
```

This is because the machine appears to expose separate performance events for different CPU core types.

The important part is that the `cpu_core` section had the meaningful sample count.

For example, in mixed mode, the useful section was:

```text
Samples: 631 of event 'cpu_core/cycles/P'
```

The `cpu_atom` section had very few samples, so I did not use it as the main basis for interpretation.

For this lab, the practical rule is:

```text
Use the section with enough samples to make a meaningful judgment.
```

---

## 11. What Changed After Adding `-fno-optimize-sibling-calls`

Before adding this option, some call paths were slightly confusing.

For example, `mix_u64()` could appear too directly under `main`, even though conceptually it was called through a workload function.

After adding:

```makefile
CFLAGS += -fno-optimize-sibling-calls
```

the call graph became easier to read:

```text
mix_u64
  run_compute_mode
  main
```

and:

```text
compute_hotspot
  run_compute_mode
  main
```

This does not necessarily change the real runtime behavior in a way that matters for this lab.

But it improves the educational value of the profile.

The call stack becomes closer to the source-level structure I want to study.

---

## 12. Main Takeaways

`perf stat` is useful when I want whole-program numbers:

```text
cycles
instructions
branches
branch-misses
```

But `perf stat` does not directly tell me where the cost happened.

`perf record` and `perf report` answer the next question:

```text
Which function received the samples?
Which function is the hotspot?
Which call path led there?
```

In this lab, the final result was:

```text
compute mode -> mix_u64 and compute_hotspot
branch mode  -> branch_hotspot and mix_u64
memory mode  -> memory_hotspot
mixed mode   -> memory_hotspot
```

The most important conclusion is:

```text
In the mixed workload, memory_hotspot dominated the profile.
```

That means the bottleneck was not the arithmetic section.

It was not primarily the branch section.

It was the pseudo-random memory access section.

---

## 13. Final Conclusion

This lab demonstrated why sampling profilers are necessary.

Whole-program counters can tell me that a program is expensive.

But they cannot directly tell me where to optimize.

`perf record` gives me that missing link.

In this experiment, the profiler showed that the mixed workload was dominated by `memory_hotspot()`, taking about 79% of sampled cycles.

That changes the optimization strategy.

Instead of guessing, I can follow the profile:

```text
Record the workload.
Report the samples.
Find the dominant function.
Optimize the real hotspot.
```
