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

/*
 * This lab measures how access order and stride affect memory performance.
 *
 * We compare:
 *   1) Sequential order over a strided subset of an array
 *   2) Random order over the exact same subset
 *
 * The key idea is:
 *   - Sequential order preserves predictability and helps hardware prefetchers
 *   - Random order breaks predictability and usually hurts locality
 *
 * The benchmark reports one CSV line so that run.sh can append results into
 * a machine-readable file.
 */

typedef enum {
    PATTERN_SEQ = 0,
    PATTERN_RANDOM = 1
} pattern_t;

typedef struct {
    size_t bytes;                /* Total array size in bytes */
    size_t stride_bytes;         /* Distance between accessed elements in bytes */
    uint64_t target_accesses;    /* Total accesses we try to perform */
    int cpu;                     /* CPU core to pin to; -1 means no pinning */
    pattern_t pattern;           /* Access order: sequential or random */
} config_t;

/* Global sink to prevent the compiler from optimizing away memory loads. */
static volatile uint64_t g_sink = 0;

/*
 * Return a monotonic timestamp in nanoseconds.
 *
 * CLOCK_MONOTONIC is preferred for benchmarking because it is not affected
 * by wall-clock adjustments.
 */
static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Pin the current process to a single CPU core.
 *
 * Pinning reduces measurement noise caused by scheduler migration.
 * If cpu < 0, we skip pinning.
 */
static void maybe_pin_cpu(int cpu) {
#ifdef __linux__
    if (cpu < 0) {
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
#else
    (void)cpu;
#endif
}

/*
 * Parse an unsigned 64-bit integer from a string.
 *
 * We use this helper to validate command line arguments cleanly.
 */
static uint64_t parse_u64(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    return (uint64_t)v;
}

/*
 * Parse a signed integer from a string.
 */
static int parse_int(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    return (int)v;
}

/*
 * Convert a pattern name into an enum.
 */
static pattern_t parse_pattern(const char *s) {
    if (strcmp(s, "seq") == 0) {
        return PATTERN_SEQ;
    }
    if (strcmp(s, "random") == 0) {
        return PATTERN_RANDOM;
    }

    fprintf(stderr, "Invalid pattern: %s (expected 'seq' or 'random')\n", s);
    exit(EXIT_FAILURE);
}

/*
 * Print usage and exit.
 */
static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s --bytes N --stride N --pattern seq|random "
            "[--target-accesses N] [--cpu N]\n",
            prog);
    exit(EXIT_FAILURE);
}

/*
 * Read command line arguments into a config structure.
 *
 * Defaults:
 *   target_accesses = 64 million
 *   cpu = -1 (no pinning)
 */
static config_t parse_args(int argc, char **argv) {
    config_t cfg;
    cfg.bytes = 0;
    cfg.stride_bytes = 0;
    cfg.target_accesses = 64000000ull;
    cfg.cpu = -1;
    cfg.pattern = PATTERN_SEQ;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bytes") == 0) {
            if (++i >= argc) usage(argv[0]);
            cfg.bytes = (size_t)parse_u64(argv[i], "--bytes");
        } else if (strcmp(argv[i], "--stride") == 0) {
            if (++i >= argc) usage(argv[0]);
            cfg.stride_bytes = (size_t)parse_u64(argv[i], "--stride");
        } else if (strcmp(argv[i], "--pattern") == 0) {
            if (++i >= argc) usage(argv[0]);
            cfg.pattern = parse_pattern(argv[i]);
        } else if (strcmp(argv[i], "--target-accesses") == 0) {
            if (++i >= argc) usage(argv[0]);
            cfg.target_accesses = parse_u64(argv[i], "--target-accesses");
        } else if (strcmp(argv[i], "--cpu") == 0) {
            if (++i >= argc) usage(argv[0]);
            cfg.cpu = parse_int(argv[i], "--cpu");
        } else {
            usage(argv[0]);
        }
    }

    if (cfg.bytes == 0 || cfg.stride_bytes == 0) {
        usage(argv[0]);
    }

    if (cfg.bytes < sizeof(uint64_t)) {
        fprintf(stderr, "--bytes must be at least %zu\n", sizeof(uint64_t));
        exit(EXIT_FAILURE);
    }

    if (cfg.stride_bytes < sizeof(uint64_t)) {
        fprintf(stderr, "--stride must be at least %zu\n", sizeof(uint64_t));
        exit(EXIT_FAILURE);
    }

    return cfg;
}

/*
 * Allocate memory aligned to 64 bytes.
 *
 * 64-byte alignment matches a common cache-line size and makes the test
 * setup cleaner.
 */
static uint64_t *alloc_aligned_u64(size_t bytes) {
    void *ptr = NULL;
    int rc = posix_memalign(&ptr, 64, bytes);
    if (rc != 0) {
        fprintf(stderr, "posix_memalign failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
    return (uint64_t *)ptr;
}

/*
 * Initialize the data array.
 *
 * The exact values do not matter much. We simply want every page to be
 * committed and every cache line to contain non-trivial data.
 */
static void init_array(uint64_t *arr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        arr[i] = (uint64_t)i * 1315423911u + 0x9e3779b97f4a7c15ull;
    }
}

/*
 * Build an array of element indices we will visit.
 *
 * Example:
 *   bytes = 1024, stride = 64, element size = 8
 *   -> indices = {0, 8, 16, 24, ...}
 *
 * The number of visited positions depends on the stride.
 */
static size_t *build_indices(size_t elem_count,
                             size_t stride_elems,
                             size_t *out_count) {
    size_t count = 0;
    for (size_t i = 0; i < elem_count; i += stride_elems) {
        count++;
    }

    size_t *idx = (size_t *)malloc(count * sizeof(size_t));
    if (!idx) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    size_t k = 0;
    for (size_t i = 0; i < elem_count; i += stride_elems) {
        idx[k++] = i;
    }

    *out_count = count;
    return idx;
}

/*
 * A small xorshift random number generator for in-place shuffling.
 *
 * This is not cryptographically secure, which is completely fine here.
 * We only need a deterministic, fast pseudo-random generator.
 */
static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/*
 * Shuffle the index array with Fisher-Yates.
 *
 * Randomizing the order keeps the same set of accessed locations but removes
 * the regular traversal pattern that hardware prefetchers like.
 */
static void shuffle_indices(size_t *idx, size_t count, uint64_t seed) {
    if (count <= 1) {
        return;
    }

    uint64_t state = seed ? seed : 0x12345678abcdefull;

    for (size_t i = count - 1; i > 0; i--) {
        size_t j = (size_t)(xorshift64(&state) % (i + 1));
        size_t tmp = idx[i];
        idx[i] = idx[j];
        idx[j] = tmp;
    }
}

/*
 * Run one benchmark.
 *
 * We warm up once before timing to reduce first-touch and one-time effects.
 * Then we repeat enough times to reach approximately target_accesses.
 *
 * We return:
 *   - elapsed time
 *   - actual access count
 *   - repeat count
 *   - checksum through g_sink
 */
static void run_benchmark(const uint64_t *arr,
                          const size_t *indices,
                          size_t index_count,
                          uint64_t target_accesses,
                          uint64_t *elapsed_ns,
                          uint64_t *actual_accesses,
                          uint64_t *repeats,
                          uint64_t *checksum) {
    if (index_count == 0) {
        fprintf(stderr, "index_count must be > 0\n");
        exit(EXIT_FAILURE);
    }

    uint64_t reps = target_accesses / (uint64_t)index_count;
    if (reps == 0) {
        reps = 1;
    }

    /*
     * Warmup pass.
     *
     * This makes the measured section less sensitive to one-time page faults
     * or extremely cold-start behavior. We still do not try to eliminate
     * cache effects entirely, because cache behavior is part of the point.
     */
    volatile uint64_t warm = 0;
    for (size_t i = 0; i < index_count; i++) {
        warm += arr[indices[i]];
    }
    g_sink ^= warm;

    volatile uint64_t sum = 0;
    uint64_t start = now_ns();

    for (uint64_t r = 0; r < reps; r++) {
        for (size_t i = 0; i < index_count; i++) {
            sum += arr[indices[i]];
        }
    }

    uint64_t end = now_ns();

    g_sink ^= sum;

    *elapsed_ns = end - start;
    *actual_accesses = (uint64_t)index_count * reps;
    *repeats = reps;
    *checksum = (uint64_t)sum;
}

/*
 * Convert pattern enum back to a readable string for CSV output.
 */
static const char *pattern_name(pattern_t p) {
    switch (p) {
        case PATTERN_SEQ:    return "seq";
        case PATTERN_RANDOM: return "random";
        default:             return "unknown";
    }
}

int main(int argc, char **argv) {
    config_t cfg = parse_args(argc, argv);

    maybe_pin_cpu(cfg.cpu);

    size_t elem_count = cfg.bytes / sizeof(uint64_t);
    size_t stride_elems = cfg.stride_bytes / sizeof(uint64_t);

    if (stride_elems == 0) {
        fprintf(stderr, "stride_elems became 0; check stride size\n");
        return EXIT_FAILURE;
    }

    uint64_t *arr = alloc_aligned_u64(elem_count * sizeof(uint64_t));
    init_array(arr, elem_count);

    size_t index_count = 0;
    size_t *indices = build_indices(elem_count, stride_elems, &index_count);

    if (cfg.pattern == PATTERN_RANDOM) {
        /*
         * We shuffle the visit order, but we keep the same subset of array
         * elements. This isolates "order / predictability" from "which
         * elements are accessed".
         */
        shuffle_indices(indices, index_count, 0xdeadbeefcafebabeull);
    }

    uint64_t elapsed_ns = 0;
    uint64_t accesses = 0;
    uint64_t repeats = 0;
    uint64_t checksum = 0;

    run_benchmark(arr,
                  indices,
                  index_count,
                  cfg.target_accesses,
                  &elapsed_ns,
                  &accesses,
                  &repeats,
                  &checksum);

    /*
     * Useful-bytes throughput:
     * We count only the bytes the program logically requested
     * (one uint64_t per access), not the hidden cache-line traffic.
     *
     * This is not actual memory-bus bandwidth. It is simply a useful metric
     * for comparing access patterns consistently.
     */
    double seconds = (double)elapsed_ns / 1e9;
    double ns_per_access = (double)elapsed_ns / (double)accesses;
    double mib_per_s =
        ((double)accesses * (double)sizeof(uint64_t)) / (1024.0 * 1024.0) / seconds;

    /*
     * Output a single CSV row.
     *
     * Columns:
     *   bytes,stride_bytes,pattern,index_count,repeats,accesses,
     *   elapsed_ns,ns_per_access,useful_mib_per_s,checksum
     */
    printf("%zu,%zu,%s,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.4f,%.2f,%" PRIu64 "\n",
           cfg.bytes,
           cfg.stride_bytes,
           pattern_name(cfg.pattern),
           index_count,
           repeats,
           accesses,
           elapsed_ns,
           ns_per_access,
           mib_per_s,
           checksum);

    free(indices);
    free(arr);
    return 0;
}
