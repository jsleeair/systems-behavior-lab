# 01-context-switch-cost

## Objective

This experiment measures the latency of process hand-off across the operating system scheduling boundary.

Specifically, we estimate the cost of a **context switch** by measuring the round-trip latency of a blocking ping-pong exchange between two processes.

The benchmark uses two pipes to implement a parent–child ping-pong pattern:

```

parent → child → parent

```

Each round-trip requires two task hand-offs:

1. parent blocks and child wakes up
2. child blocks and parent wakes up

Thus we approximate:

```

context_switch_cost ≈ roundtrip_latency / 2

```

This measurement includes not only scheduler switching cost but also pipe syscall overhead and wakeup latency.

---

# Experimental Environment

System information:

```

Architecture: x86_64
CPU: 13th Gen Intel(R) Core(TM) i7-1360P
Cores: 8 cores / 16 threads
Hypervisor vendor: Microsoft
Virtualization type: full
Kernel: 6.6.87.2-microsoft-standard-WSL2

```

The experiment was conducted inside:

- Windows Subsystem for Linux (WSL2)
- Hyper-V virtualized environment

Important implications:

- scheduling crosses a **guest kernel → hypervisor → host scheduler** boundary
- cross-vCPU wakeups may incur additional latency

---

# Benchmark Design

The benchmark implements a blocking ping-pong using two pipes.

```

## Parent                     Child

write → pipe
read
write → pipe
read

```

Each iteration performs:

```

parent write
child wakeup
child write
parent wakeup

```

So one round-trip approximately corresponds to **two context switches**.

---

# Measurement Parameters

Benchmark configuration:

```

iterations = 200000
warmup = 20000
repeats = 5

```

CPU affinity:

```

same mode  : parent_cpu = 0, child_cpu = 0
split mode : parent_cpu = 0, child_cpu = 1

```

Metrics recorded:

```

ns_per_roundtrip
ns_per_context_switch_est

```

Where

```

ns_per_context_switch_est = ns_per_roundtrip / 2

```

---

# Raw Results

## Same-core measurements

```

3281.94 ns
3429.07 ns
3239.27 ns
3498.98 ns
3523.14 ns

```

Average:

```

3394.48 ns roundtrip
1697.24 ns estimated context switch

```

---

## Split-core measurements

```

42388.11 ns
42457.03 ns
44279.63 ns
46564.50 ns
43755.49 ns

```

Average:

```

43889 ns roundtrip
21944 ns estimated context switch

```

---

# Scheduler Validation

To verify that the benchmark actually triggers scheduler context switches, the program was executed with `perf`.

```

sudo perf stat -e context-switches,cpu-migrations ./context_switch

```

Result:

```

context-switches: 440005
cpu-migrations: 2

```

Total round-trips executed:

```

warmup + iterations
= 20000 + 200000
= 220000

```

Expected number of context switches:

```

220000 × 2 = 440000

```

Observed:

```

440005

```

This confirms that each ping-pong round-trip indeed generates approximately **two context switches**.

CPU migrations remained negligible, indicating that CPU affinity was effective.

---

# Visualization

## Round-trip latency

![Roundtrip](plots/context_switch_roundtrip.png)

---

## Estimated context switch cost

![CtxSwitch](plots/context_switch_estimated_ctxswitch.png)

---

## Log-scale latency comparison

![Roundtrip Log](plots/context_switch_roundtrip_logscale.png)

---

# Analysis

## Same-core latency

Measured:

```

~3.4 µs roundtrip
~1.7 µs estimated context switch

```

This value is higher than typical bare-metal Linux systems (often 200–800 ns per switch) but consistent with measurements observed in virtualized environments.

In WSL2, scheduling may involve:

```

Linux guest scheduler
→ Hyper-V hypervisor
→ Windows host scheduler

```

which increases latency.

---

## Split-core latency

Measured:

```

~44 µs roundtrip

```

This is approximately:

```

13× slower than same-core

```

The most likely cause is **cross-vCPU wakeup latency in the Hyper-V virtualization layer**.

When the parent and child run on different virtual CPUs, wakeups must traverse:

```

guest scheduler
→ hypervisor
→ host scheduler
→ target vCPU

```

This additional coordination significantly increases latency.

---

# Key Observations

1. The ping-pong benchmark produced exactly the expected number of context switches.
2. Same-core switching latency is approximately **1.7 µs per switch** in the WSL2 environment.
3. Cross-core wakeups in WSL2 are dramatically slower, reaching **~44 µs round-trip latency**.
4. CPU migration noise was negligible.

---

# Limitations

This experiment does not isolate pure scheduler context-switch cost.

The measurement includes:

- pipe syscall overhead
- scheduler wakeup latency
- virtualization overhead
- cache and TLB effects

Additionally, results obtained inside WSL2 may differ significantly from bare-metal Linux measurements.

---

# Conclusion

The ping-pong microbenchmark successfully measured the cost of process hand-offs across the OS boundary.

Results show that:

- same-core process switching latency is on the order of **microseconds** in WSL2
- cross-vCPU wakeups can increase latency by **more than an order of magnitude**

This experiment highlights how **virtualization and scheduling topology strongly influence context-switch latency**.
