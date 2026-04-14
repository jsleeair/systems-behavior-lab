#define _POSIX_C_SOURCE 200809L
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

/*
 * loop_unrolling.c
 *
 * Goal:
 *   Measure how manual loop unrolling changes throughput for a simple
 *   array-summing kernel when SIMD auto-vectorization is disabled.
 *
 * Why this benchmark exists:
 *   This lab is meant to isolate the effect of loop unrolling itself.
 *   We want to see how reducing loop-control overhead and exposing a
 *   longer straight-line instruction sequence affects performance.
 *
 * Important noe:
 *   The Makefile intentionally uses -fno-tree-vectorize so that the
 *   compiler does not hide experiment behind automatic SIMD.
 */

typedef struct {
	size_t array_bytes;
	uint64_t repeats;
	int unroll;
	int warmup;
	int pin_cpu;
	bool csv_header_only;
} options_t;

static volatile uint64_t g_sink = 0;

/* Return monotonic time in nanoseconds. */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Pin the process to one CPU core if pin_cpu >= 0.
 *
 * Pinning reduces run-to-run noise caused by migration between cores.
 * This is especially useful for microbencmarks.
 */
static void pin_to_cpu_if_requested(int pin_cpu) {
	if (pin_cpu < 0) {
		return;
	}

#if defined(__linux__)
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(pin_cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		fprintf(stderr, "warning: sched_setaffinity(%d) failed: %s\n",
			pin_cpu, strerror(errno));
	}
#else
	(void)pin_cpu;
#endif
}

/*
 * Fill the array with deterministic values.
 *
 * We do not want all zeros because that can sometimes encourage overly
 * aggressive optimization patterns. A simple cheap pattern is enough.
 */
static void init_array(uint64_t *arr, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		arr[i] = (uint64_t)i * 1315423911ull + 0x9e3779b97f4a7c15ull;
	}
}

/*
 * Baseline scalar sum with no manual unrolling.
 *
 * This version gives us the reference point:
 * one loop branch, one index increment, one load, one add per element.
 */
__attribute__((noinline))
static uint64_t sum_u1(const uint64_t *arr, size_t n, uint64_t repeats) {
	uint64_t total = 0;

	for (uint64_t r = 0; r < repeats; ++r) {
		uint64_t acc = total;
		for (size_t i = 0; i < n; ++i) {
			acc += arr[i];
		}
		total = acc;
	}
	return total;
}

/*
 * Manual unroll by 2.
 *
 * The body now handles two elements per loop iteration.
 * This reduces loop-control overhead per element compared to u1.
 */
__attribute__((noinline))
static uint64_t sum_u2(const uint64_t *arr, size_t n, uint64_t repeats) {
	uint64_t total = 0;

	for (uint64_t r = 0; r < repeats; ++r) {
		uint64_t acc = total;
		size_t i = 0;
		size_t limit = n / 2 * 2;

		for (; i < limit; i += 2) {
			acc += arr[i + 0];
			acc += arr[i + 1];
		}

		for (; i < n; ++i) {
			acc += arr[i];
		}

		total = acc;
	}

	return total;
}

/* Manual unroll by 4. */
__attribute__((noinline))
static uint64_t sum_u4(const uint64_t *arr, size_t n, uint64_t repeats) {
	uint64_t total = 0;

	for (uint64_t r = 0; r < repeats; ++r) {
		uint64_t acc = total;
		size_t i = 0;
		size_t limit = n / 4 * 4;

		for (; i < limit; i += 4) {
			acc += arr[i + 0];
			acc += arr[i + 1];
			acc += arr[i + 2];
			acc += arr[i + 3];
		}

		for (; i < n; ++i) {
			acc += arr[i];
		}

		total = acc;
	}

	return total;
}

/* Manual unroll by 8. */
__attribute__((noinline))
static uint64_t sum_u8(const uint64_t *arr, size_t n, uint64_t repeats) {
	uint64_t total = 0;

	for (uint64_t r = 0; r < repeats; ++r) {
		uint64_t acc = total;
		size_t i = 0;
		size_t limit = n / 8 * 8;

		for (; i < limit; i += 8) {
			acc += arr[i + 0];
			acc += arr[i + 1];
			acc += arr[i + 2];
			acc += arr[i + 3];
			acc += arr[i + 4];
			acc += arr[i + 5];
			acc += arr[i + 6];
			acc += arr[i + 7];
		}

		for (; i < n; ++i) {
			acc += arr[i];
		}

		total = acc;
	}

	return total;
}

/* Manual unroll by 16. */
__attribute__((noinline))
static uint64_t sum_u16(const uint64_t *arr, size_t n, uint64_t repeats) {
    uint64_t total = 0;

    for (uint64_t r = 0; r < repeats; ++r) {
        uint64_t acc = total;
        size_t i = 0;
        size_t limit = n / 16 * 16;

        for (; i < limit; i += 16) {
            acc += arr[i + 0];
            acc += arr[i + 1];
            acc += arr[i + 2];
            acc += arr[i + 3];
            acc += arr[i + 4];
            acc += arr[i + 5];
            acc += arr[i + 6];
            acc += arr[i + 7];
            acc += arr[i + 8];
            acc += arr[i + 9];
            acc += arr[i + 10];
            acc += arr[i + 11];
            acc += arr[i + 12];
            acc += arr[i + 13];
            acc += arr[i + 14];
            acc += arr[i + 15];
        }

        for (; i < n; ++i) {
            acc += arr[i];
        }

	total = acc;
    }

    return total;
}

typedef uint64_t (*sum_fn_t)(const uint64_t *, size_t, uint64_t);

static sum_fn_t pick_sum_fn(int unroll) {
	switch (unroll) {
		case 1: return sum_u1;
		case 2: return sum_u2;
		case 4: return sum_u4;
		case 8: return sum_u8;
		case 16: return sum_u16;
		default:
			 fprintf(stderr, "unsupported --unroll=%d (allowed: 1,2,4,8,16)\n", unroll);
			 exit(EXIT_FAILURE);
	}
}

static void print_csv_header(void) {
    printf("unroll,array_bytes,elements,repeats,warmup,pin_cpu,elapsed_ns,ns_per_element,elements_per_sec,checksum\n");
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --array-bytes N       Working-set size in bytes (default: 32768)\n"
        "  --repeats N           Number of full-array passes (default: 200000)\n"
        "  --unroll N            Unroll factor: 1,2,4,8,16 (default: 1)\n"
        "  --warmup N            Warmup runs before measurement (default: 1)\n"
        "  --pin-cpu N           Pin to CPU core N, -1 disables pinning (default: 0)\n"
        "  --csv-header          Print CSV header only\n"
        "  --help                Show this message\n",
        prog);
}

static uint64_t parse_u64(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (uint64_t)v;
}

static int parse_int(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    if (v < -2147483647L || v > 2147483647L) {
        fprintf(stderr, "%s out of range: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static options_t parse_args(int argc, char **argv) {
    options_t opt;
    opt.array_bytes = 32 * 1024;
    opt.repeats = 200000;
    opt.unroll = 1;
    opt.warmup = 1;
    opt.pin_cpu = 0;
    opt.csv_header_only = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--array-bytes") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--array-bytes needs a value\n");
                exit(EXIT_FAILURE);
            }
            opt.array_bytes = (size_t)parse_u64(argv[++i], "array-bytes");
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--repeats needs a value\n");
                exit(EXIT_FAILURE);
            }
            opt.repeats = parse_u64(argv[++i], "repeats");
        } else if (strcmp(argv[i], "--unroll") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--unroll needs a value\n");
                exit(EXIT_FAILURE);
            }
            opt.unroll = parse_int(argv[++i], "unroll");
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--warmup needs a value\n");
                exit(EXIT_FAILURE);
            }
            opt.warmup = parse_int(argv[++i], "warmup");
        } else if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--pin-cpu needs a value\n");
                exit(EXIT_FAILURE);
            }
            opt.pin_cpu = parse_int(argv[++i], "pin-cpu");
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            opt.csv_header_only = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (opt.array_bytes < sizeof(uint64_t)) {
        fprintf(stderr, "--array-bytes must be at least %zu\n", sizeof(uint64_t));
        exit(EXIT_FAILURE);
    }

    return opt;
}

int main(int argc, char **argv) {
    options_t opt = parse_args(argc, argv);

    if (opt.csv_header_only) {
        print_csv_header();
        return 0;
    }

    pin_to_cpu_if_requested(opt.pin_cpu);

    size_t elements = opt.array_bytes / sizeof(uint64_t);
    if (elements == 0) {
        fprintf(stderr, "array size too small after element conversion\n");
        return EXIT_FAILURE;
    }

    /*
     * aligned_alloc requires the size to be a multiple of alignment.
     * We round up to 64-byte alignment because it is cache-line friendly.
     */
    size_t alloc_bytes = elements * sizeof(uint64_t);
    size_t alignment = 64;
    size_t rounded = (alloc_bytes + alignment - 1) / alignment * alignment;

    uint64_t *arr = aligned_alloc(alignment, rounded);
    if (!arr) {
        perror("aligned_alloc");
        return EXIT_FAILURE;
    }

    init_array(arr, elements);

    sum_fn_t fn = pick_sum_fn(opt.unroll);

    /*
     * Warmup:
     * Run the same kernel a few times before timing. This helps stabilize
     * instruction cache, data cache, and page mappings.
     */
    for (int w = 0; w < opt.warmup; ++w) {
        g_sink ^= fn(arr, elements, 1);
    }

    uint64_t start_ns = now_ns();
    uint64_t checksum = fn(arr, elements, opt.repeats);
    uint64_t elapsed_ns = now_ns() - start_ns;

    g_sink ^= checksum;

    double total_elements = (double)elements * (double)opt.repeats;
    double ns_per_element = (double)elapsed_ns / total_elements;
    double elements_per_sec = total_elements / ((double)elapsed_ns / 1e9);

    printf("%d,%zu,%zu,%" PRIu64 ",%d,%d,%" PRIu64 ",%.6f,%.3f,%" PRIu64 "\n",
           opt.unroll,
           opt.array_bytes,
           elements,
           opt.repeats,
           opt.warmup,
           opt.pin_cpu,
           elapsed_ns,
           ns_per_element,
           elements_per_sec,
           checksum);

    fprintf(stderr,
            "[summary] unroll=%d array_bytes=%zu elements=%zu repeats=%" PRIu64
            " elapsed_ns=%" PRIu64 " ns_per_element=%.6f checksum=%" PRIu64 "\n",
            opt.unroll,
            opt.array_bytes,
            elements,
            opt.repeats,
            elapsed_ns,
            ns_per_element,
            checksum);

    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    free(arr);
    return 0;
}
					
