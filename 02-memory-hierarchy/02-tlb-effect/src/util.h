#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Read Time-Stamp Counter (TSC) for high-resolution cycle timing on x86_64.
// This is not perfectly stable across all CPUs/settings, but it's great
// for "relative" microbenchmarks when combined with CPU pinning.
uint64_t rdtsc_ordered(void);

// Pin the current thread/process to a specific CPU core.
int pin_to_cpu(int cpu);

// Best-effort: raise priority to reduce scheduling noise.
int try_raise_priority(void);

// Returns system page size.
size_t get_page_size(void);

#ifdef __cplusplus
}
#endif
