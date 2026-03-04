#define _GNU_SOURCE
#include "util.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <sys/resource.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

uint64_t rdtsc_ordered(void) {
#if defined(__x86_64__) || defined(_M_X64)
    // Use RDTSCP to serialize after reading the counter, and LFENCE for ordering.
    // This reduces out-of-order execution effects around timing.
    unsigned int aux = 0;
    _mm_lfence();
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#else
    // Fallback: not supported on non-x86_64 in this lab.
    return 0;
#endif
}

int pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "sched_setaffinity(cpu=%d) failed: %s\n", cpu, strerror(errno));
        return -1;
    }
    return 0;
}

int try_raise_priority(void) {
    // Try nice(-20) and also try real-time policy as a best-effort hint.
    // These may fail without privileges. That's okay.
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, -20) != 0) {
        // Not fatal
    }
    // We intentionally do not set SCHED_FIFO here to avoid requiring root/capabilities.
    return 0;
}

size_t get_page_size(void) {
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) return 4096;
    return (size_t)ps;
}
