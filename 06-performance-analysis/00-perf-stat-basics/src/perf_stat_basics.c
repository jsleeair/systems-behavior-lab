#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * 06-performance-analysis / 00-perf-stat-basics
 *
 * This program intentionally provides several simple workloads.
 *
 * The goal is not to build the fastest benchmark.
 * The goal is to create workloads with different performance-counter shapes:
 *
 *   1. dep_add:
 *      A dependency chain. Each iteration depends on the previous value.
 *      This tends to limit instruction-level parallelism.
 *
 *   2. indep_add:
 *      Several independent accumulators.
 *      The CPU has more independent work to overlap.
 *
 *   3. branch_predictable:
 *      A branch that is easy for the branch predictor.
 *
 *   4. branch_unpredictable:
 *      A branch driven by pseudo-random bits.
 *      This should increase branch-misses.
 *
 *   5. memory_seq:
 *      Sequential memory traversal.
 *      This gives the CPU/cache/prefetcher a friendly memory pattern.
 *
 *   6. syscall_getpid:
 *      Repeated kernel boundary crossing via getpid().
 *      This is here to show that perf stat can also count non-pure-CPU workloads.
 *
 * Use perf stat outside this program to collect:
 *   cycles,instructions,branches,branch-misses,cache-references,cache-misses
 */

static volatile uint64_t g_sink = 0;

static uint64_t now_ns(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }

    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu(int cpu) {
    if (cpu < 0) {
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "[warn] failed to pin to CPU %d: %s\n", cpu, strerror(errno));
    }
}

static uint64_t xorshift64(uint64_t *state) {
    /*
     * Small deterministic PRNG.
     * Good enough for making branch outcomes hard to predict.
     */
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static uint64_t workload_dep_add(uint64_t iters) {
    uint64_t x = 1;

    for (uint64_t i = 0; i < iters; ++i) {
        /*
         * Dependency chain:
         * next x cannot be computed before current x is ready.
         */
        x = x * 1664525ull + 1013904223ull;
    }

    return x;
}

static uint64_t workload_indep_add(uint64_t iters) {
    uint64_t a = 1;
    uint64_t b = 2;
    uint64_t c = 3;
    uint64_t d = 4;
    uint64_t e = 5;
    uint64_t f = 6;
    uint64_t g = 7;
    uint64_t h = 8;

    for (uint64_t i = 0; i < iters; ++i) {
        /*
         * Independent accumulators:
         * the CPU can overlap these operations more easily than one long chain.
         */
        a = a * 3 + 1;
        b = b * 5 + 1;
        c = c * 7 + 1;
        d = d * 11 + 1;
        e = e * 13 + 1;
        f = f * 17 + 1;
        g = g * 19 + 1;
        h = h * 23 + 1;
    }

    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
}

static uint64_t workload_branch_predictable(uint64_t iters) {
    uint64_t sum = 0;

    for (uint64_t i = 0; i < iters; ++i) {
        /*
         * Very predictable branch:
         * almost every iteration follows the same path.
         */
        if (i < iters) {
            sum += i;
        } else {
            sum -= i;
        }
    }

    return sum;
}

static uint64_t workload_branch_unpredictable(uint64_t iters) {
    uint64_t sum = 0;
    uint64_t state = 0x123456789abcdefull;

    for (uint64_t i = 0; i < iters; ++i) {
        /*
         * Branch outcome depends on pseudo-random low bit.
         * This should be much harder for the branch predictor.
         */
        if (xorshift64(&state) & 1ull) {
            sum += i;
        } else {
            sum -= i;
        }
    }

    return sum ^ state;
}

static uint64_t workload_memory_seq(uint64_t iters, size_t elements) {
    uint64_t *arr = NULL;

    if (posix_memalign((void **)&arr, 64, elements * sizeof(uint64_t)) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        exit(1);
    }

    for (size_t i = 0; i < elements; ++i) {
        arr[i] = (uint64_t)i * 1315423911ull;
    }

    uint64_t sum = 0;

    for (uint64_t r = 0; r < iters; ++r) {
        /*
         * Sequential memory access:
         * this is friendly to cache lines and hardware prefetching.
         */
        for (size_t i = 0; i < elements; ++i) {
            sum += arr[i];
        }
    }

    free(arr);
    return sum;
}

static uint64_t workload_syscall_getpid(uint64_t iters) {
    uint64_t sum = 0;

    for (uint64_t i = 0; i < iters; ++i) {
        /*
         * Cross user/kernel boundary repeatedly.
         * This workload is useful for seeing that not all cost is pure ALU work.
         */
        sum += (uint64_t)getpid();
    }

    return sum;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s --mode MODE [options]\n"
            "\n"
            "Modes:\n"
            "  dep_add\n"
            "  indep_add\n"
            "  branch_predictable\n"
            "  branch_unpredictable\n"
            "  memory_seq\n"
            "  syscall_getpid\n"
            "\n"
            "Options:\n"
            "  --iters N       Iteration count. Default: 100000000\n"
            "  --elements N    Number of uint64_t elements for memory_seq. Default: 8388608\n"
            "  --pin-cpu N     Pin process to CPU N. Default: -1, disabled\n"
            "  --csv-header    Print CSV header and exit\n",
            prog);
}

int main(int argc, char **argv) {
    const char *mode = NULL;
    uint64_t iters = 100000000ull;
    size_t elements = 8388608ull;
    int pin_cpu = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--elements") == 0 && i + 1 < argc) {
            elements = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--pin-cpu") == 0 && i + 1 < argc) {
            pin_cpu = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            printf("mode,iters,elements,pin_cpu,elapsed_ns,ns_per_iter,checksum\n");
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (mode == NULL) {
        usage(argv[0]);
        return 1;
    }

    pin_to_cpu(pin_cpu);

    uint64_t start = now_ns();
    uint64_t checksum = 0;

    if (strcmp(mode, "dep_add") == 0) {
        checksum = workload_dep_add(iters);
    } else if (strcmp(mode, "indep_add") == 0) {
        checksum = workload_indep_add(iters);
    } else if (strcmp(mode, "branch_predictable") == 0) {
        checksum = workload_branch_predictable(iters);
    } else if (strcmp(mode, "branch_unpredictable") == 0) {
        checksum = workload_branch_unpredictable(iters);
    } else if (strcmp(mode, "memory_seq") == 0) {
        checksum = workload_memory_seq(iters, elements);
    } else if (strcmp(mode, "syscall_getpid") == 0) {
        checksum = workload_syscall_getpid(iters);
    } else {
        usage(argv[0]);
        return 1;
    }

    uint64_t end = now_ns();
    uint64_t elapsed_ns = end - start;

    g_sink ^= checksum;

    printf("%s,%" PRIu64 ",%zu,%d,%" PRIu64 ",%.6f,%" PRIu64 "\n",
           mode,
           iters,
           elements,
           pin_cpu,
           elapsed_ns,
           (double)elapsed_ns / (double)iters,
           checksum);

    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);
    return 0;
}
