// false_sharing.c
//
// Goal:
//   Demonstrate "false sharing" by making multiple threads update different
//   counters that may accidentally reside on the same cache line.
//
// What to observe:
//   - MODE=false: counters are packed densely -> many counters share a cache line.
//                 Multiple cores repeatedly invalidate the same line -> slow.
//   - MODE=padded: each counter is padded/aligned to 64 bytes -> minimal sharing -> faster.
//
// Build:
//   make
//
// Run examples:
//   ./artifacts/bin/false_sharing --threads 8 --iters 200000000 --mode false --pin 1
//   ./artifacts/bin/false_sharing --threads 8 --iters 200000000 --mode padded --pin 1
//
// Notes:
//   - Cache line size is assumed 64 bytes (common on x86_64, ARM64, etc.).
//   - We intentionally write to shared memory each iteration to trigger coherence.
//   - Thread pinning reduces noise by keeping each thread on a stable CPU core.

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

typedef enum {
    MODE_FALSE = 0,
    MODE_PADDED = 1
} my_mode_t;

typedef struct {
    // Packed counter: many of these will be neighbors in memory,
    // thus multiple counters land in the same cache line.
    volatile uint64_t value;
} counter_packed_t;

typedef struct __attribute__((aligned(CACHELINE_SIZE))) {
    // Padded/aligned counter: each element starts at a cache-line boundary.
    // Padding ensures the next element is on a different cache line.
    volatile uint64_t value;
    char padding[CACHELINE_SIZE - sizeof(uint64_t)];
} counter_padded_t;

typedef struct {
    int tid;
    int threads;
    uint64_t iters;
    bool pin;
    my_mode_t mode;

    // We keep both pointers and use the one for the chosen mode.
    counter_packed_t *packed;
    counter_padded_t *padded;
} worker_arg_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    // CLOCK_MONOTONIC_RAW is usually less subject to NTP adjustments on Linux.
    // If unavailable on some systems, CLOCK_MONOTONIC also works.
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_thread_to_cpu(int cpu_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);

    int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        // Pinning is best-effort; do not fail the whole program.
        fprintf(stderr, "[warn] pthread_setaffinity_np(cpu=%d) failed: %s\n",
                cpu_id, strerror(rc));
    }
}

static void *worker(void *p) {
    worker_arg_t *arg = (worker_arg_t *)p;

    if (arg->pin) {
        int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpu > 0) {
            // Simple mapping: tid -> cpu (wrap around).
            pin_thread_to_cpu(arg->tid % ncpu);
        }
    }

    // Each thread writes its own counter location repeatedly.
    // This is exactly where false sharing hurts if those locations share a line.
    if (arg->mode == MODE_FALSE) {
        volatile uint64_t *c = &arg->packed[arg->tid].value;
        for (uint64_t i = 0; i < arg->iters; i++) {
            // A plain increment triggers a write, forcing cache-coherence actions.
            (*c)++;
        }
    } else {
        volatile uint64_t *c = &arg->padded[arg->tid].value;
        for (uint64_t i = 0; i < arg->iters; i++) {
            (*c)++;
        }
    }

    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s --threads N --iters M --mode false|padded [--pin 0|1]\n"
        "\n"
        "Options:\n"
        "  --threads N   Number of worker threads\n"
        "  --iters M     Increments per thread\n"
        "  --mode        false (packed counters) or padded (cacheline-separated)\n"
        "  --pin         Pin threads to CPUs (default: 1)\n"
        "\n",
        prog);
}

static bool streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

int main(int argc, char **argv) {
    int threads = 0;
    uint64_t iters = 0;
    my_mode_t mode = MODE_FALSE;
    bool pin = true;

	
    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "--threads") && i + 1 < argc) {
            threads = atoi(argv[++i]);
        } else if (streq(argv[i], "--iters") && i + 1 < argc) {
            iters = strtoull(argv[++i], NULL, 10);
        } else if (streq(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (streq(m, "false")) mode = MODE_FALSE;
            else if (streq(m, "padded")) mode = MODE_PADDED;
            else {
                fprintf(stderr, "Unknown mode: %s\n", m);
                usage(argv[0]);
                return 2;
            }
        } else if (streq(argv[i], "--pin") && i + 1 < argc) {
            pin = (atoi(argv[++i]) != 0);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (threads <= 0 || iters == 0) {
        usage(argv[0]);
        return 2;
    }

    // Allocate counters.
    // We allocate both arrays to keep code simple; only one is used in a run.
    counter_packed_t *packed = calloc((size_t)threads, sizeof(counter_packed_t));
    counter_padded_t *padded = NULL;

    if (!packed) {
        fprintf(stderr, "calloc(packed) failed\n");
        return 1;
    }

    // For padded, alignment matters. calloc usually gives good alignment, but we’ll be explicit.
    // posix_memalign ensures base alignment; the struct itself is aligned too.
    void *pbuf = NULL;
    int rc = posix_memalign(&pbuf, CACHELINE_SIZE, (size_t)threads * sizeof(counter_padded_t));
    if (rc != 0) {
        fprintf(stderr, "posix_memalign(padded) failed: %s\n", strerror(rc));
        free(packed);
        return 1;
    }
    memset(pbuf, 0, (size_t)threads * sizeof(counter_padded_t));
    padded = (counter_padded_t *)pbuf;

    pthread_t *tids = calloc((size_t)threads, sizeof(pthread_t));
    worker_arg_t *args = calloc((size_t)threads, sizeof(worker_arg_t));
    if (!tids || !args) {
        fprintf(stderr, "calloc(tids/args) failed\n");
        free(packed);
        free(padded);
        free(tids);
        free(args);
        return 1;
    }

    uint64_t t0 = now_ns();

    for (int t = 0; t < threads; t++) {
        args[t].tid = t;
        args[t].threads = threads;
        args[t].iters = iters;
        args[t].pin = pin;
        args[t].mode = mode;
        args[t].packed = packed;
        args[t].padded = padded;

        int prc = pthread_create(&tids[t], NULL, worker, &args[t]);
        if (prc != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(prc));
            return 1;
        }
    }

    for (int t = 0; t < threads; t++) {
        pthread_join(tids[t], NULL);
    }

    uint64_t t1 = now_ns();
    uint64_t elapsed = t1 - t0;

    // Sum results to prevent the compiler from optimizing "everything away".
    // (The counters are volatile anyway, but we also compute a final sum.)
    uint64_t sum = 0;
    if (mode == MODE_FALSE) {
        for (int t = 0; t < threads; t++) sum += packed[t].value;
    } else {
        for (int t = 0; t < threads; t++) sum += padded[t].value;
    }

    // Metrics.
    // Total operations = threads * iters.
    __uint128_t ops = (__uint128_t)threads * (__uint128_t)iters;
    double ns_per_op = (double)elapsed / (double)ops;

    const char *mode_str = (mode == MODE_FALSE) ? "false" : "padded";
    printf("mode=%s threads=%d iters=%" PRIu64 " pin=%d elapsed_ns=%" PRIu64 " ns_per_op=%.3f sum=%" PRIu64 "\n",
           mode_str, threads, iters, pin ? 1 : 0, elapsed, ns_per_op, sum);

    free(packed);
    free(padded);
    free(tids);
    free(args);
    return 0;
}
