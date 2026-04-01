// stride_vs_cache_miss.c
//
// Lab goal:
//   Measure how access stride affects memory access cost.
//
// Why this version is better than the simple one-pass benchmark:
//   1) It keeps the total number of memory accesses roughly constant
//      across different stride values.
//   2) It tests multiple working-set sizes so we can observe behavior
//      around different cache levels and DRAM.
//   3) It reports stride both in elements and in bytes.
//   4) It supports CPU pinning to reduce scheduling noise.
//   5) It uses warmup + repeats and reports the best run.
//
// Important interpretation note:
//   This benchmark is not a "pure cache miss only" test.
//   For linear strided access, modern hardware prefetchers can help.
//   So the result reflects a combination of:
//     - cache line utilization
//     - spatial locality
//     - hardware prefetch effectiveness
//     - TLB/page effects
//     - DRAM latency/bandwidth effects
//
// Build example:
//   gcc -O2 -march=native -Wall -Wextra -std=c11
//       src/stride_vs_cache_miss.c -o artifacts/bin/stride_vs_cache_miss
//
// Example:
//   ./artifacts/bin/stride_vs_cache_miss --pin-cpu 0
//
// Output CSV columns:
//   working_set_bytes
//   working_set_kb
//   stride_elems
//   stride_bytes
//   total_accesses
//   repeats
//   best_elapsed_ns
//   ns_per_access

#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

// Prevent the compiler from optimizing away the benchmarked loads.
static volatile uint64_t g_sink = 0;

// Use CLOCK_MONOTONIC_RAW when available to reduce time adjustment noise.
static uint64_t now_ns(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu_or_die(int cpu) {
    if (cpu < 0) {
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        fprintf(stderr, "sched_setaffinity(cpu=%d) failed: %s\n",
                cpu, strerror(errno));
        exit(1);
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --pin-cpu N           Pin benchmark to CPU N (default: -1, no pinning)\n"
        "  --repeats N           Number of measured repeats per case (default: 5)\n"
        "  --warmup N            Number of warmup repeats per case (default: 1)\n"
        "  --target-accesses N   Target number of accesses per test case (default: 100000000)\n"
        "  --max-stride-bytes N  Maximum stride in bytes (default: 4096)\n"
        "  --help                Show this help message\n",
        prog);
}

// Parse unsigned 64-bit integer from argv.
static uint64_t parse_u64_or_die(const char *s, const char *flag_name) {
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", flag_name, s);
        exit(1);
    }
    return (uint64_t)v;
}

// Parse signed int from argv.
static int parse_int_or_die(const char *s, const char *flag_name) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", flag_name, s);
        exit(1);
    }
    return (int)v;
}

// Round x up to the next multiple of align, where align is power-of-two.
static size_t round_up_pow2(size_t x, size_t align) {
    return (x + align - 1u) & ~(align - 1u);
}

int main(int argc, char **argv) {
    int pin_cpu = -1;
    int repeats = 5;
    int warmup = 1;
    uint64_t target_accesses = 100000000ull;
    size_t max_stride_bytes = 4096;

    // Working-set sizes chosen to roughly span:
    //   L1-ish, L2-ish, LLC-ish, DRAM-ish
    //
    // You can tune these later per machine.
    const size_t working_set_bytes_list[] = {
        32ull * 1024ull,          // 32 KB
        256ull * 1024ull,         // 256 KB
        4ull * 1024ull * 1024ull, // 4 MB
        64ull * 1024ull * 1024ull // 64 MB
    };
    const size_t num_working_sets =
        sizeof(working_set_bytes_list) / sizeof(working_set_bytes_list[0]);

    // Command-line parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (i + 1 >= argc) usage(argv[0]), exit(1);
            pin_cpu = parse_int_or_die(argv[++i], "--pin-cpu");
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (i + 1 >= argc) usage(argv[0]), exit(1);
            repeats = parse_int_or_die(argv[++i], "--repeats");
            if (repeats <= 0) {
                fprintf(stderr, "--repeats must be > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 >= argc) usage(argv[0]), exit(1);
            warmup = parse_int_or_die(argv[++i], "--warmup");
            if (warmup < 0) {
                fprintf(stderr, "--warmup must be >= 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--target-accesses") == 0) {
            if (i + 1 >= argc) usage(argv[0]), exit(1);
            target_accesses = parse_u64_or_die(argv[++i], "--target-accesses");
            if (target_accesses == 0) {
                fprintf(stderr, "--target-accesses must be > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--max-stride-bytes") == 0) {
            if (i + 1 >= argc) usage(argv[0]), exit(1);
            max_stride_bytes = (size_t)parse_u64_or_die(argv[++i], "--max-stride-bytes");
            if (max_stride_bytes == 0) {
                fprintf(stderr, "--max-stride-bytes must be > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    pin_to_cpu_or_die(pin_cpu);

    // We benchmark uint64_t loads.
    const size_t elem_size = sizeof(uint64_t);

    // Allocate once using the largest working-set size.
    size_t max_working_set_bytes = 0;
    for (size_t i = 0; i < num_working_sets; i++) {
        if (working_set_bytes_list[i] > max_working_set_bytes) {
            max_working_set_bytes = working_set_bytes_list[i];
        }
    }

    max_working_set_bytes = round_up_pow2(max_working_set_bytes, CACHE_LINE_SIZE);

    uint64_t *buf = NULL;
    if (posix_memalign((void **)&buf, CACHE_LINE_SIZE, max_working_set_bytes) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        return 1;
    }

    const size_t max_elems = max_working_set_bytes / elem_size;

    // Initialize once to ensure pages are mapped and memory is touched.
    for (size_t i = 0; i < max_elems; i++) {
        buf[i] = (uint64_t)i * 11400714819323198485ull;
    }

    // Print a CSV header.
    printf("working_set_bytes,working_set_kb,stride_elems,stride_bytes,"
           "total_accesses,repeats,best_elapsed_ns,ns_per_access\n");

    // Benchmark each working-set size independently.
    for (size_t ws_idx = 0; ws_idx < num_working_sets; ws_idx++) {
        const size_t working_set_bytes = working_set_bytes_list[ws_idx];
        const size_t working_set_elems = working_set_bytes / elem_size;

        if (working_set_elems == 0) {
            continue;
        }

        // Stride is reported in both elements and bytes.
        // We start at 1 element (8 bytes).
        // We double stride each step until we reach max_stride_bytes
        // or until stride exceeds the working set.
        for (size_t stride_elems = 1; ; stride_elems *= 2) {
            const size_t stride_bytes = stride_elems * elem_size;

            if (stride_bytes > max_stride_bytes) {
                break;
            }
            if (stride_elems > working_set_elems) {
                break;
            }

            // Number of unique positions touched in one full pass over the working set.
            //
            // Example:
            //   working_set_elems = 1024, stride_elems = 16
            //   accesses_per_pass = 64
            //
            // We then repeat enough passes so that the total accesses are
            // roughly target_accesses for every stride.
            const size_t accesses_per_pass =
                (working_set_elems + stride_elems - 1u) / stride_elems;

            if (accesses_per_pass == 0) {
                continue;
            }

            // Ceiling division to decide how many passes we need to reach
            // roughly target_accesses total accesses.
            const uint64_t passes =
                (target_accesses + accesses_per_pass - 1u) / accesses_per_pass;

            const uint64_t total_accesses = passes * (uint64_t)accesses_per_pass;

            // Optional warmup runs. We execute the same access pattern so that:
            //   - pages are already touched
            //   - branch predictor and loop state are warmed
            //   - cold-start noise is reduced
            for (int w = 0; w < warmup; w++) {
                uint64_t sum = 0;
                for (uint64_t p = 0; p < passes; p++) {
                    for (size_t i = 0; i < working_set_elems; i += stride_elems) {
                        sum += buf[i];
                    }
                }
                g_sink += sum;
            }

            uint64_t best_elapsed_ns = UINT64_MAX;

            for (int r = 0; r < repeats; r++) {
                uint64_t sum = 0;
                const uint64_t t0 = now_ns();

                // Main measured region.
                for (uint64_t p = 0; p < passes; p++) {
                    for (size_t i = 0; i < working_set_elems; i += stride_elems) {
                        sum += buf[i];
                    }
                }

                const uint64_t t1 = now_ns();
                const uint64_t elapsed = t1 - t0;

                if (elapsed < best_elapsed_ns) {
                    best_elapsed_ns = elapsed;
                }

                g_sink += sum;
            }

            const double ns_per_access =
                (double)best_elapsed_ns / (double)total_accesses;

            printf("%zu,%zu,%zu,%zu,%" PRIu64 ",%d,%" PRIu64 ",%.4f\n",
                   working_set_bytes,
                   working_set_bytes / 1024u,
                   stride_elems,
                   stride_bytes,
                   total_accesses,
                   repeats,
                   best_elapsed_ns,
                   ns_per_access);
        }
    }

    free(buf);

    fprintf(stderr,
            "[summary] pin_cpu=%d repeats=%d warmup=%d target_accesses=%" PRIu64
            " max_stride_bytes=%zu\n",
            pin_cpu, repeats, warmup, target_accesses, max_stride_bytes);
    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    return 0;
}
