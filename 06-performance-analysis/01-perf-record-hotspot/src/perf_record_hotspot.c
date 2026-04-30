#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * This lab is intentionally designed for perf record / perf report.
 *
 * The goal is NOT to build a realistic application.
 * The goal is to create several clearly identifiable hotspots:
 *
 *   1. compute_hotspot()
 *      - arithmetic-heavy workload
 *      - should mostly consume CPU execution time
 *
 *   2. branch_hotspot()
 *      - unpredictable branch-heavy workload
 *      - useful for seeing a branch-heavy symbol in perf report
 *
 *   3. memory_hotspot()
 *      - pseudo-random memory access workload
 *      - useful for creating a memory/locality-sensitive hotspot
 *
 * The functions are marked noinline so that perf can attribute samples
 * to readable function names instead of having everything merged into main().
 *
 * We also compile with -fno-omit-frame-pointer from the Makefile.
 * This makes frame-pointer based call graphs easier for perf to unwind.
 */

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

typedef enum {
	MODE_COMPUTE,
	MODE_BRANCH,
	MODE_MEMORY,
	MODE_MIXED
} Mode;

typedef struct {
	Mode mode;
	uint64_t iters;
	size_t array_bytes;
	int warmup;
	int pin_cpu;
} Config;

/*
 * Global volatile sink.
 *
 * Without a visible side effect, an optimizing compiler may decide that
 * some computations are useless and remove them.
 *
 * Writing the final result into a volatile object makes the result
 * observable from the compiler's point of view.
 */
static volatile uint64_t g_sink = 0;

static uint64_t now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }

    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  --mode compute|branch|memory|mixed\n"
            "  --iters N\n"
            "  --array-bytes N\n"
            "  --warmup 0|1\n"
            "  --pin-cpu N          accepted for CSV compatibility, not used here\n"
            "  --csv-header\n"
            "\n"
            "Examples:\n"
            "  %s --mode compute --iters 200000000\n"
            "  %s --mode mixed --iters 100000000 --array-bytes 67108864\n",
            prog, prog, prog);
}

static int parse_u64(const char *s, uint64_t *out) {
	char *end = NULL;
	errno = 0;

	unsigned long long v = strtoull(s, &end, 10);

	if (errno != 0 || end == s || *end != '\0') {
		return -1;
	}

	*out = (uint64_t)v;
	return 0;
}

static int parse_size(const char *s, size_t *out) {
	uint64_t v = 0;

	if (parse_u64(s, &v) != 0) {
		return -1;
	}

	*out = (size_t)v;
	return 0;
}

static Mode parse_mode(const char *s)
{
	if (strcmp(s, "compute") == 0) {
		return MODE_COMPUTE;
	}

	if (strcmp(s, "branch") == 0) {
		return MODE_BRANCH;
	}

	if (strcmp(s, "memory") == 0) {
		return MODE_MEMORY;
	}

	if (strcmp(s, "mixed") == 0) {
		return MODE_MIXED;
	}

	fprintf(stderr, "Unknown mode: %s\n", s);
	exit(1);
}

static const char *mode_name(Mode mode)
{
    switch (mode) {
    case MODE_COMPUTE:
        return "compute";
    case MODE_BRANCH:
        return "branch";
    case MODE_MEMORY:
        return "memory";
    case MODE_MIXED:
        return "mixed";
    default:
        return "unknown";
    }
}

/*
 * A small integer mixer.
 *
 * This function is used by compute_hotspot() and branch_hotspot().
 * It makes the arithmetic less trivial, preventing the compiler from
 * turning the whole loop into a simple closed-form expression.
 */
static NOINLINE uint64_t mix_u64(uint64_t x)
{
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;
	return x;
}

/*
 * Arithmetic-heavy hotspot.
 *
 * This function repeatedly transforms a small set of dependent values.
 * It is designed to create a symbol that should be easy to find in
 * perf report when --mode compute is used.
 */
static NOINLINE uint64_t compute_hotspot(uint64_t iters)
{
	uint64_t a = 0x123456789abcdef0ull;
	uint64_t b = 0x0fedcba987654321ull;
	uint64_t c = 0x9e3779b97f4a7c15ull;

	for (uint64_t i = 0; i < iters; ++i) {
		a = mix_u64(a + i);
		b = b * 1664525ull + 1013904223ull + (a >> 7);
		c ^= (a + b + i);
		c = (c << 13) | (c >> 51);
	}

	return a ^ b ^ c;
}

/*
 * Branch-heavy hotspot.
 *
 * The branch condition depends on mixed data, so the pattern is hard
 * for the CPU to predict perfectly.
 *
 * This function is not meant to represent a clean algorithm.
 * It is a controlled source of branch-heavy work for profiling.
 */
static NOINLINE uint64_t branch_hotspot(uint64_t iters)
{
	uint64_t x = 0xfeedfacecafebeefull;
	uint64_t acc = 0;

	for (uint64_t i = 0; i < iters; ++i) {
		x = mix_u64(x + i);

		if (x & 1ull) {
			acc += x ^ (i * 3ull);
		} else {
			acc += (x >> 3) + (i * 7ull);
		}

		if (x & 0x100ull) {
			acc ^= x << 1;
		} else {
			acc ^= x >> 1;
		}
	}

	return acc;
}

/*
 * Allocate an aligned array.
 *
 * 64-byte alignment is used because it matches a common cache-line size
 * on many x86-64 systems. The exact hardware may differ, but this is a
 * reasonable alignment for this type of lab.
 */
static uint64_t *allocate_array(size_t array_bytes, size_t *out_count)
{
	if (array_bytes < sizeof(uint64_t)) {
		array_bytes = sizeof(uint64_t);
	}

	size_t count = array_bytes / sizeof(uint64_t);
	size_t rounded_bytes = count * sizeof(uint64_t);

	void *ptr = NULL;
	int rc = posix_memalign(&ptr, 64, rounded_bytes);

	if (rc != 0) {
		fprintf(stderr, "posix_memalign failed: %s\n", strerror(rc));
		exit(1);
	}

	uint64_t *arr = (uint64_t *)ptr;

	for (size_t i = 0; i < count; ++i) {
		arr[i] = mix_u64((uint64_t)i + 0x1234ull);
	}

	*out_count = count;
	return arr;
}

/*
 * Memory-sensitive hotspot.
 *
 * The access index is generated by repeatedly mixing the previous index.
 * This creates a pseudo-random access pattern over the array.
 *
 * Sequential memory access is usually friendly to caches and prefetchers.
 * Pseudo-random access is much less friendly, so this function should
 * produce a hotspot that is more memory/locality-sensitive.
 */
static NOINLINE uint64_t memory_hotspot(uint64_t *arr, size_t count, uint64_t iters)
{
	uint64_t acc = 0xabcdef0123456789ull;
	size_t idx = 0;

	for (uint64_t i = 0; i < iters; ++i) {
		idx = (size_t)(mix_u64((uint64_t)idx + i + acc) % count);

		uint64_t v = arr[idx];

		acc += v;
		acc ^= acc << 7;
		acc ^= acc >> 9;

		arr[idx] = v + acc + i;
	}

	return acc;
}

/*
 * Wrapper functions.
 *
 * These exist to make the call graph easier to read.
 * With perf report -g, you should be able to see main()
 * calling run_compute_mode(), run_memory_mode(), etc.
 */
static NOINLINE uint64_t run_compute_mode(uint64_t iters)
{
	return compute_hotspot(iters);
}

static NOINLINE uint64_t run_branch_mode(uint64_t iters)
{
	return branch_hotspot(iters);
}

static NOINLINE uint64_t run_memory_mode(uint64_t *arr, size_t count, uint64_t iters)
{
	return memory_hotspot(arr, count, iters);
}

static NOINLINE uint64_t run_mixed_mode(uint64_t *arr, size_t count, uint64_t iters)
{
	uint64_t acc = 0;

	/*
	 * Split the work so that multiple symbols appear in the profile.
	 * The exact distrubution does not need to be perfect.
	 * The point is to create a report with several visible hotspots.
	 */
	acc ^= compute_hotspot(iters / 2);
	acc ^= memory_hotspot(arr, count, iters / 4);
	acc ^= branch_hotspot(iters / 4);

	return acc;
}

static Config default_config(void)
{
	Config cfg;

	cfg.mode = MODE_MIXED;
	cfg.iters = 100000000ull;
	cfg.array_bytes = 64ull * 1024ull * 1024ull;
	cfg.warmup = 1;
	cfg.pin_cpu = 0;

	return cfg;
}

int main(int argc, char **argv)
{
	Config cfg = default_config();

	for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--csv-header") == 0) {
            printf("mode,iters,array_bytes,warmup,pin_cpu,elapsed_ns,ns_per_iter,checksum\n");
            return 0;
        }

        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            cfg.mode = parse_mode(argv[++i]);
            continue;
        }

        if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            if (parse_u64(argv[++i], &cfg.iters) != 0) {
                fprintf(stderr, "Invalid --iters value\n");
                return 1;
            }
            continue;
        }

        if (strcmp(argv[i], "--array-bytes") == 0 && i + 1 < argc) {
            if (parse_size(argv[++i], &cfg.array_bytes) != 0) {
                fprintf(stderr, "Invalid --array-bytes value\n");
                return 1;
            }
            continue;
        }

        if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (parse_u64(argv[++i], &v) != 0) {
                fprintf(stderr, "Invalid --warmup value\n");
                return 1;
            }
            cfg.warmup = (int)v;
            continue;
        }

        if (strcmp(argv[i], "--pin-cpu") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (parse_u64(argv[++i], &v) != 0) {
                fprintf(stderr, "Invalid --pin-cpu value\n");
                return 1;
            }
            cfg.pin_cpu = (int)v;
            continue;
        }

        usage(argv[0]);
        return 1;
    }

	size_t count = 0;
	uint64_t *arr = allocate_array(cfg.array_bytes, &count);

	if (cfg.warmup) {
		uint64_t warm = 0;

		switch (cfg.mode) {
			case MODE_COMPUTE:
				warm = run_compute_mode(cfg.iters / 20);
				break;
			case MODE_BRANCH:
				warm = run_branch_mode(cfg.iters / 20);
				break;
			case MODE_MEMORY:
				warm = run_memory_mode(arr, count, cfg.iters / 20);
				break;
			case MODE_MIXED:
				warm = run_mixed_mode(arr, count, cfg.iters / 20);
				break;
		}

		g_sink ^= warm;
	}

	uint64_t start_ns = now_ns();

	uint64_t checksum = 0;

	switch (cfg.mode) {
		case MODE_COMPUTE:
			checksum = run_compute_mode(cfg.iters);
			break;
		case MODE_BRANCH:
			checksum = run_branch_mode(cfg.iters);
			break;
		case MODE_MEMORY:
			checksum = run_memory_mode(arr, count, cfg.iters);
			break;
		case MODE_MIXED:
			checksum = run_mixed_mode(arr, count, cfg.iters);
			break;
	}

	uint64_t end_ns = now_ns();
	uint64_t elapsed_ns = end_ns - start_ns;

	g_sink ^= checksum;

	double ns_per_iter = 0.0;
	if (cfg.iters > 0) {
		ns_per_iter = (double)elapsed_ns / (double)cfg.iters;
	}

	printf("%s,%" PRIu64 ",%zu,%d,%d,%" PRIu64 ",%.6f,%" PRIu64 "\n",
           mode_name(cfg.mode),
           cfg.iters,
           cfg.array_bytes,
           cfg.warmup,
           cfg.pin_cpu,
           elapsed_ns,
           ns_per_iter,
           checksum);

    /*
     * Print the sink to stderr so it does not corrupt CSV output.
     */
    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    free(arr);
    return 0;
}
