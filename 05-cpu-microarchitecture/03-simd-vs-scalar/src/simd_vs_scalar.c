/*
 * simd_vs_scalar.c
 *
 * A microbenchmark that compares three implementations of a simple SAXPY-style kernel:
 *
 * 	y[i] = a * x[i] + y[i]
 *
 * Modes:
 *   1. scalar_novec : scalar loop with auto-vectorization explicitly disabled
 *   2. scalar_auto  : plain scalar loop, compiler may auto-vectorize
 *   3. avx2	     : manual AVX2 implementation using intrinsics
 *
 * Why this lab exists:
 *   - To show what SIMD actually accelerates
 *   - To compare compiler auto-vectorization vs manual SIMD
 *   - To observe when memory bandwidth starts to dominate
 *   - To study the effect of alignment / misalignment
 *
 * Build assumptions:
 *   - x86-64 Linux
 *   - GCC or Clang with AVX2 support
 *
 * Example:
 *   ./artifacts/bin/simd_vs_scalar --mode avx2 --elements 1048576 --repeats 200 --misalign-bytes 0
 *
 * CSV example:
 *   ./artifacts/bin/simd_vs_scalar --csv
 *
 * Notes:
 *   - We use checksum reduction at the end so the optimizer cannot discard work.
 *   - We keep initialization deterministic for reproducibility.
 *   - The scalar_novec version is important because otherwise the compiler may
 *   silently vectorize "scalar" code and blur the comparison.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <errno.h>
#include <immintrin.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef likely
#define likely(x) __buildtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __buildin_expect(!!(x), 0)
#endif

typedef enum Mode {
	MODE_SCALAR_NOVEC = 0,
	MODE_SCALAR_AUTO = 1,
	MODE_AVX2	= 2
} Mode;

typedef struct Config {
    Mode mode;
    size_t elements;
    int repeats;
    int warmup;
    int pin_cpu;
    size_t misalign_bytes;
    int csv;
    int csv_header;
} Config;

static volatile double g_sink = 0.0;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --mode <scalar_novec|scalar_auto|avx2>\n"
        "  --elements <N>               Number of float elements per array\n"
        "  --repeats <N>                Timed repeats\n"
        "  --warmup <N>                 Warmup repeats before timing\n"
        "  --pin-cpu <cpu_id>           Pin process to a CPU core; -1 disables pinning\n"
        "  --misalign-bytes <N>         Add byte offset to x and y pointers (e.g. 0 or 4)\n"
        "  --csv                        Print one CSV row\n"
        "  --csv-header                 Print CSV header\n"
        "  --help\n"
        "\n"
        "Defaults:\n"
        "  --mode scalar_novec\n"
        "  --elements 1048576\n"
        "  --repeats 200\n"
        "  --warmup 1\n"
        "  --pin-cpu 0\n"
        "  --misalign-bytes 0\n",
        prog
    );
}

static int parse_int(const char *s, int *out) {
    char *end = NULL;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    if (v < -2147483647L || v > 2147483647L) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_size(const char *s, size_t *out) {
    char *end = NULL;
    unsigned long long v;

    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (size_t)v;
    return 0;
}

static int parse_mode(const char *s, Mode *out) {
    if (strcmp(s, "scalar_novec") == 0) {
        *out = MODE_SCALAR_NOVEC;
        return 0;
    }
    if (strcmp(s, "scalar_auto") == 0) {
        *out = MODE_SCALAR_AUTO;
        return 0;
    }
    if (strcmp(s, "avx2") == 0) {
        *out = MODE_AVX2;
        return 0;
    }
    return -1;
}

static const char *mode_name(Mode m) {
    switch (m) {
        case MODE_SCALAR_NOVEC: return "scalar_novec";
        case MODE_SCALAR_AUTO:  return "scalar_auto";
        case MODE_AVX2:         return "avx2";
        default:                return "unknown";
    }
}

static int pin_to_cpu_if_requested(int cpu_id) {
    if (cpu_id < 0) {
        return 0;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        return -1;
    }
    return 0;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void *aligned_alloc_or_die(size_t alignment, size_t size) {
    void *p = NULL;
    int rc = posix_memalign(&p, alignment, size);
    if (rc != 0) {
        fprintf(stderr, "posix_memalign failed: %s\n", strerror(rc));
        exit(1);
    }
    return p;
}

static void init_arrays(float *x, float *y, size_t n) {
	/*
	 * Deterministic initialization.
	 * We avoid all-zero or overly simple patterns so the compiler cannot
	 * simplify the math too aggressively.
	 */
	for (size_t i = 0; i < n; i++) {
		x[i] = (float)((i % 251) * 0.25f + 1.0f);
		y[i] = (float)((i % 127) * 0.50f + 0.5f);
	}
}

static double checksum_array(const float *y, size_t n) {
	/*
	 * Simple reduction into double for stable output and to keep the
	 * benchmarked work observable.
	 */
	double sum = 0.0;
	for (size_t i = 0; i < n; i++) {
		sum += (double)y[i];
	}
	return sum;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline,optimize("no-tree-vectorize")))
#endif
static void saxpy_scalar_novec(float *restrict y, const float*restrict x, float a, size_t n) {
	/*
	 * Explicitly disable tree-vectorization for this function.
	 * This is our "honest" scalar baseline.
	 */
	for (size_t i = 0; i < n; i++) {
		y[i] = a * x[i] + y[i];
	}
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void saxpy_scalar_auto(float *restrict y, const float *restrict x, float a, size_t n) {
	/*
	 * Plain scalar-looking loop.
	 * The compiler is free to auto-vectorize this.
	 */
	for (size_t i = 0; i < n; i++) {
		y[i] = a * x[i] + y[i];
	}
}

#if defined(__GNUC__) || defined(__clang)
__attribute__((noinline))
#endif
static void saxpy_avx2(float *restrict y, const float *restrict x, float a, size_t n) {
	/*
	 * Manual AVX2 implementation.
	 *
	 * We process 8 floats per iteration because __m256 holds 8 float lanes.
	 * Tail elements are handled with a scalar cleanup loop.
	 */
	size_t i = 0;
	__m256 va = _mm256_set1_ps(a);

	for (; i + 8 <= n; i+= 8) {
		__m256 vx = _mm256_loadu_ps(x + i);
		__m256 vy = _mm256_loadu_ps(y + i);

		/*
		 * Use FMA if the compiler/hardware supports it via flags.
		 * The intrinsic communicates the intended fused multiply-add operation.
		 */
		__m256 vr = _mm256_fmadd_ps(va, vx, vy);

		_mm256_storeu_ps(y + i, vr);
	}

	for (; i < n; i++) {
		y[i] = a * x[i] + y[i];
	}
}

static void run_kernel(Mode mode, float *y, const float *x, float a, size_t n) {
	switch (mode) {
		case MODE_SCALAR_NOVEC:
			saxpy_scalar_novec(y, x, a, n);
			break;
		case MODE_SCALAR_AUTO:
			saxpy_scalar_auto(y, x, a, n);
			break;
		case MODE_AVX2:
			saxpy_avx2(y, x, a, n);
			break;
		default:
			fprintf(stderr, "unknown mode\n");
			exit(1);
	}
}

static void parse_args(int argc, char **argv, Config *cfg) {
	cfg->mode = MODE_SCALAR_NOVEC;
	cfg->elements = 1024 * 1024;
	cfg->repeats = 200;
	cfg->warmup = 1;
	cfg->pin_cpu = 0;
	cfg->misalign_bytes = 0;
	cfg->csv = 0;
	cfg->csv_header = 0;

	for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 >= argc || parse_mode(argv[i + 1], &cfg->mode) != 0) {
                fprintf(stderr, "invalid --mode\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--elements") == 0) {
            if (i + 1 >= argc || parse_size(argv[i + 1], &cfg->elements) != 0) {
                fprintf(stderr, "invalid --elements\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (i + 1 >= argc || parse_int(argv[i + 1], &cfg->repeats) != 0 || cfg->repeats <= 0) {
                fprintf(stderr, "invalid --repeats\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 >= argc || parse_int(argv[i + 1], &cfg->warmup) != 0 || cfg->warmup < 0) {
                fprintf(stderr, "invalid --warmup\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (i + 1 >= argc || parse_int(argv[i + 1], &cfg->pin_cpu) != 0) {
                fprintf(stderr, "invalid --pin-cpu\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--misalign-bytes") == 0) {
            if (i + 1 >= argc || parse_size(argv[i + 1], &cfg->misalign_bytes) != 0) {
                fprintf(stderr, "invalid --misalign-bytes\n");
                usage(argv[0]);
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "--csv") == 0) {
            cfg->csv = 1;
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            cfg->csv_header = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            exit(1);
        }
    }

    /*
     * We operate on float arrays, so odd byte offsets are allowed technically,
     * but meaningful test values are usually 0 or 4. We do not forbid others,
     * but note that arbitrary offsets may produce less interpretable results.
     */
	if (cfg->elements == 0) {
		fprintf(stderr, "elements must be > 0\n");
		exit(1);
	}
}

int main(int argc, char **argv) {
	Config cfg;
	parse_args(argc, argv, &cfg);

	if (cfg.csv_header) {
		printf("mode,elements,repeats,warmup,pin_cpu,misalign_bytes,elapsed_ns,ns_per_element,checksum\n");
		return 0;
	}

	if (pin_to_cpu_if_requested(cfg.pin_cpu) != 0) {
		return 1;
	}

	/*
	 * We intentionally over-allocate so we can apply byte offset to the
	 * returned aligned pointer while still keeping the suable region large enough.
	 *
	 * 64-byte alignment is a reasonable choice for modern CPUs and aligns with
	 * cache line size.
	 */
	size_t bytes = cfg.elements * sizeof(float);
	size_t extra = 64 + cfg.misalign_bytes;

	uint8_t *x_raw = (uint8_t *)aligned_alloc_or_die(64, bytes + extra);
	uint8_t *y_raw = (uint8_t *)aligned_alloc_or_die(64, bytes + extra);

	float *x = (float *)(x_raw + cfg.misalign_bytes);
	float *y = (float *)(y_raw + cfg.misalign_bytes);

	init_arrays(x, y, cfg.elements);

	const float a = 1.001f;

	/* Warmup phase. */
	for (int r = 0; r < cfg.warmup; r++) {
		run_kernel(cfg.mode, y, x, a, cfg.elements);
	}

	uint64_t t0 = now_ns();
	for (int r = 0; r < cfg.repeats; r++) {
		run_kernel(cfg.mode, y, x, a, cfg.elements);
	}
	uint64_t t1 = now_ns();

	double checksum = checksum_array(y, cfg.elements);
	g_sink += checksum;

	uint64_t elapsed_ns = t1 - t0;
	double total_elements_processed = (double)cfg.elements * (double)cfg.repeats;
	double ns_per_element = (double)elapsed_ns / total_elements_processed;

	if (cfg.csv) {
        printf("%s,%zu,%d,%d,%d,%zu,%" PRIu64 ",%.6f,%.6f\n",
               mode_name(cfg.mode),
               cfg.elements,
               cfg.repeats,
               cfg.warmup,
               cfg.pin_cpu,
               cfg.misalign_bytes,
               elapsed_ns,
               ns_per_element,
               checksum);
    } else {
        printf("mode=%s,elements=%zu,repeats=%d,warmup=%d,pin_cpu=%d,misalign_bytes=%zu,"
               "elapsed_ns=%" PRIu64 ",ns_per_element=%.6f,checksum=%.6f\n",
               mode_name(cfg.mode),
               cfg.elements,
               cfg.repeats,
               cfg.warmup,
               cfg.pin_cpu,
               cfg.misalign_bytes,
               elapsed_ns,
               ns_per_element,
               checksum);

        fprintf(stderr,
                "[summary] mode=%s elements=%zu repeats=%d misalign_bytes=%zu "
                "elapsed_ns=%" PRIu64 " ns_per_element=%.6f checksum=%.6f\n",
                mode_name(cfg.mode),
                cfg.elements,
                cfg.repeats,
                cfg.misalign_bytes,
                elapsed_ns,
                ns_per_element,
                checksum);

        fprintf(stderr, "[sink] %.6f\n", g_sink);
    }

    free(x_raw);
    free(y_raw);
    return 0;
}

