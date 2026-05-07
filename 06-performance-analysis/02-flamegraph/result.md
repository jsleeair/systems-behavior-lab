# 02-flamegraph Result

## 1. Goal

This lab extends the previous `perf record` hotspot experiment from flat symbol-level profiling to stack-based profiling.

The main question was:

> Can we see not only which function is hot, but also which call path makes it hot?

A flat `perf report` is useful for finding hot symbols. However, a hot function can be called from multiple different parts of a program. In that case, the function name alone is not enough. We also need to know the caller context.

A Flame Graph helps with this because it visualizes sampled call stacks. The horizontal width of each frame represents how many samples were collected for that stack path.

In this lab, the workload was intentionally designed so that the same leaf function, `mix_u64`, appears under multiple caller paths.

---

## 2. Experiment Setup

The program was built with frame pointers enabled:

```bash
-fno-omit-frame-pointer
-fno-optimize-sibling-calls
```

This makes stack unwinding more reliable for `perf record --call-graph fp`.

The workload was profiled using:

```bash
perf record \
  -F 99 \
  -g \
  --call-graph fp \
  -o artifacts/data/perf.data \
  -- ./artifacts/bin/flamegraph_lab \
      --elements 1048576 \
      --rounds 80 \
      --frontend-weight 2 \
      --backend-weight 3 \
      --cleanup-weight 1
```

The recorded samples were then converted into a Flame Graph through the usual pipeline:

```bash
perf script > artifacts/data/perf.script.txt

tools/FlameGraph/stackcollapse-perf.pl \
  artifacts/data/perf.script.txt \
  > artifacts/data/out.folded

tools/FlameGraph/flamegraph.pl \
  artifacts/data/out.folded \
  > artifacts/data/flamegraph.svg
```

Generated artifacts:

```text
artifacts/data/perf.data
artifacts/reports/perf.report.txt
artifacts/data/perf.script.txt
artifacts/data/out.folded
artifacts/data/flamegraph.svg
```

---

## 3. Perf Report Summary

The final `perf report` showed the following high-level distribution:

```text
run_pipeline      99.89%

frontend_stage    57.05%
backend_stage     42.84%
```

The main call tree was:

```text
run_pipeline
├── frontend_stage 57.05%
│   ├── validate_records 45.19%
│   │   ├── branchy_score 23.33%
│   │   └── mix_u64 20.90%
│   ├── mix_u64 6.71%
│   ├── branchy_score 2.81%
│   └── parse_records 2.34%
│
└── backend_stage 42.84%
    ├── mix_u64 20.01%
    ├── rotate_scramble 10.69%
    ├── hash_index_build 8.85%
    └── compress_blocks 3.28%
```

The workload was mostly split between two major stages:

```text
frontend_stage: about 57%
backend_stage:  about 43%
```

This is a useful result because both sides of the artificial workload are visible in the profile. The profile is not dominated by only one path.

---

## 4. Flame Graph

![CPU Flame Graph](plots/flamegraph.svg)

The Flame Graph shows that most of the execution time is under `run_pipeline`.

The two largest branches are:

```text
run_pipeline -> frontend_stage
run_pipeline -> backend_stage
```

The frontend side is dominated by validation work:

```text
run_pipeline
└── frontend_stage
    └── validate_records
        ├── branchy_score
        └── mix_u64
```

The backend side is dominated by hash-building and compression-like work:

```text
run_pipeline
└── backend_stage
    ├── hash_index_build
    │   └── mix_u64
    └── compress_blocks
        └── rotate_scramble
```

The important observation is that `mix_u64` appears in more than one caller context.

---

## 5. Key Observation: Hot Function vs Hot Call Path

A flat hotspot view may suggest a simple conclusion:

```text
mix_u64 is hot.
```

However, the call graph and Flame Graph show a more precise conclusion:

```text
mix_u64 is hot in multiple caller contexts.
```

In this result, `mix_u64` appears under both frontend and backend paths:

```text
frontend_stage -> validate_records -> mix_u64
backend_stage  -> hash_index_build -> mix_u64
```

The approximate sample percentages were:

```text
frontend-side mix_u64: about 21%
backend-side mix_u64:  about 20%
```

This means that `mix_u64` is not only a single isolated bottleneck. Its cost is split across different higher-level workload paths.

This is the central lesson of the lab:

> A hot leaf function is not a complete explanation by itself. The caller context matters.

---

## 6. Children vs Self Time

The report also shows an important difference between `Children` and `Self`.

For example:

```text
frontend_stage  Children 57.05%, Self 0.00%
backend_stage   Children 42.84%, Self 0.00%
```

This means that `frontend_stage` and `backend_stage` themselves do not spend much CPU time directly in their own function bodies. Instead, the time is spent in the functions they call.

In other words, these stage functions are important because they organize the expensive work below them.

The real CPU-heavy leaf functions are things like:

```text
mix_u64
branchy_score
rotate_scramble
```

The parent functions explain the workload context. The leaf functions explain where the CPU instructions are actually executed.

---

## 7. What the Flame Graph Adds

The previous `perf-record-hotspot` lab answered this kind of question:

> Which symbol received the most CPU samples?

This Flame Graph lab answers a better question:

> Which call path received the most CPU samples?

That distinction matters because the same function can be used by different parts of the program.

In this workload, `mix_u64` is used in multiple places. Without stack context, it would be easy to say that `mix_u64` is the bottleneck. With stack context, we can see that the cost belongs to both:

```text
frontend validation work
backend hash-building work
```

This makes the performance diagnosis more accurate.

---

## 8. Conclusion

This lab demonstrated why stack-based profiling is useful.

The final profile showed that the workload was split between two major stages:

```text
frontend_stage: about 57%
backend_stage:  about 43%
```

The most important finding was that the same hot leaf function, `mix_u64`, appeared under multiple caller contexts. This showed that a flat symbol-level profile is not enough to fully explain the performance behavior.

The main takeaway is:

```text
perf report tells us which function is hot.
Flame Graphs help us understand which call path is hot.
```

A function-level hotspot is useful, but a call-path-level hotspot is more actionable. Flame Graphs preserve that caller context visually, making it easier to understand where the program's time actually flows.
