/*
 * Branch prediction microbenchmark.
 *
 * Goal:
 *   Measure how different branch outcome patterns affect runtime.
 *   We keep the loop body nearly identical and only change the data pattern
 *   that drives the branch.
 *
 *   Notes:
 *     - This is a microbenchmark, so exact numbers depend on CPU, compiler, frequency scaling, thermal state, and OS noise.
 *     - We intentinally disable some compiler transformations in Makefile
 *     (such as if-conversion and vectorization) so that the "brancy"
 *     veersion remains branchy.
 *     - The branchless mode provides a reference point, but wheter it wins
 *     depends on the machine and generated code.
 *
 *     Suggestd usage:
 *       make
 *       ./artifacts/bin/branch_prediction --mode random_50 --iters 100000000
 *
 *     Optional perf usage:
 *       perf state -e cycles,instructions,branches,branch-misses \
 *       ./artifacts/bin/branch_prediction --mode random_50 --iters 100000000
 */

#define _POSIX_C_SOURCE 200809L
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

typedef enum {
	MODE_ALWAYS_TAKEN = 0,
	MODE_ALWAYS_NOT_TAKEN,
	MODE_ALTERNATING,
	MODE_RANDOM_50,
	MODE_RANDOM_90_TAKEN,
	MODE_BRANCHLESS_RANDOM_50,
} mode_t_bp;

typedef struct {
	mode_t_bp mode;
	size_t iters;
	size_t warmup;
	int threshold;
	uint32_t seed;
	int pin_cpu;
	int csv;
} config_t;

static volatile uint64_t g_sink = 0;

static void die(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		die("clock_gettime");
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu_if_requested(int cpu) {
	if (cpu < 0) {
		return;
	}


#ifdef __linux__
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET((unsigned)cpu, &set);

	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		die("sched_setaffinity");
	}
#else
	(void)cpu;
#endif
}

static uint32_t xorshift32(uint32_t *state) {
	// Small, fast pseudo-random generator.
	// Good enough for benchmark input generation.
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static const char *mode_to_string(mode_t_bp mode) {
	switch (mode) {
		case MODE_ALWAYS_TAKEN:		return "always_taken";
		case MODE_ALWAYS_NOT_TAKEN:	return "always_not_taken";
		case MODE_ALTERNATING:		return "alternating";
		case MODE_RANDOM_50:		return "random_50";
		case MODE_RANDOM_90_TAKEN:	return "random_90_taken";
		case MODE_BRANCHLESS_RANDOM_50:	return "branchless_random_50";
		default:			return "unknown";
	}
}

static mode_t_bp parse_mode(const char *s) {
	if (strcmp(s, "always_taken") == 0)		return MODE_ALWAYS_TAKEN;
	if (strcmp(s, "always_not_taken") == 0)		return MODE_ALWAYS_NOT_TAKEN;
	if (strcmp(s, "alternating") == 0)		return MODE_ALTERNATING;
	if (strcmp(s, "random_50") == 0)		return MODE_RANDOM_50;
	if (strcmp(s, "random_90_taken") == 0)		return MODE_RANDOM_90_TAKEN;
	if (strcmp(s, "branchless_random_50") == 0)	return MODE_BRANCHLESS_RANDOM_50;

	fprintf(stderr, "Unknown mode: %s\n", s);
	exit(EXIT_FAILURE);
}

static void print_usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s [option]\n"
		"\n"
		"Option:\n"
		"  --mode <name>	Benchmark mode\n"
		"			always_taken\n"
		"			alwyas_not_taken\n"
		"			alternating\n"
		"			random_50\n"
		"			random_90_taken\n"
		"			branchless_random_50\n"
		"  --iters <N>		Number of iterations (default: 100000000)\n"
		"  --warmup <N>		Warmup iterations (default: 1000000)\n"
		"  --threshold <N>	Threshold for branch condition (default: 128)\n"
		"  --seed <N>		RNG seed (default: 12345)\n"
		"  --pin-cpu <cpu>	Pin process to a CPU core (default: -1 = no pin)\n"
		"  --csv		Output CSV header + one data row\n"
		"  --help		Show this help\n",
		prog
	       );
}

static config_t parse_args(int argc, char **argv) {
	config_t cfg;
	cfg.mode = MODE_RANDOM_50;
	cfg.iters = 1000000000ull;
	cfg.warmup = 1000000ull;
	cfg.threshold = 128;
	cfg.seed = 12345u;
	cfg.pin_cpu = -1;
	cfg.csv = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--mode requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.mode = parse_mode(argv[i]);
		} else if (strcmp(argv[i], "--iters") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--iters requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.iters = strtoull(argv[i], NULL, 10);
		} else if (strcmp(argv[i], "--warmup") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--warmup requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.warmup = strtoull(argv[i], NULL, 10);
		} else if (strcmp(argv[i], "--threshold") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--threshold requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.threshold = atoi(argv[i]);
		} else if (strcmp(argv[i], "--seed") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--seed requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.seed = (uint32_t)strtoul(argv[i], NULL, 10);
		} else if (strcmp(argv[i], "--pin-cpu") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "--pin-cpu requires a value\n");
				exit(EXIT_FAILURE);
			}
			cfg.pin_cpu = atoi(argv[i]);
		} else if (strcmp(argv[i], "--csv") == 0) {
				cfg.csv = 1;
		} else if (strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			exit(EXIT_SUCCESS);
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	return cfg;
}

static uint8_t *alloc_data(size_t n) {
	// 64-byte alignment is a good default for cache-line alignment.
	void *ptr = NULL;
	if (posix_memalign(&ptr, 64, n * sizeof(uint8_t)) != 0) {
		die("posix_memalign");
	}
	return (uint8_t *)ptr;
}

static void fill_pattern(uint8_t *data, size_t n, mode_t_bp mode, int threshold, uint32_t seed) {
	(void)threshold;

	uint32_t state = seed;

	switch (mode) {
		case MODE_ALWAYS_TAKEN:
		// All values greater than threshold.
		for (size_t i = 0; i < n; i++) {
			data[i] = 255;
		}
		break;

		case MODE_ALWAYS_NOT_TAKEN:
		// All values less than or equal to threshold.
		for (size_t i = 0; i < n; i++) {
			data[i] = 0;
		}
		break;

		case MODE_ALTERNATING:
		// Deterministic T/N/T/N or the reverse, depending on the threshold.
		for (size_t i =0; i < n; i++) {
			data[i] = (i & 1) ? 255 : 0;
		}
		break;

		case MODE_RANDOM_50:
		case MODE_BRANCHLESS_RANDOM_50:
			// About half above threshold, half below.
			for (size_t i = 0; i < n; i++) {
				uint32_t r = xorshift32(&state);
				data[i] = (r & 1u) ? 255 : 0;
			}
			break;

		case MODE_RANDOM_90_TAKEN:
			// About 90% of elements above threshold, 10% below.
			for (size_t i = 0; i < n; i++) {
				uint32_t r = xorshift32(&state) % 10u;
				data[i] = (r < 9u) ? 255 : 0;
			}
			break;

		default:
			fprintf(stderr, "Unhandled mode in fill_pattern\n");
			exit(EXIT_FAILURE);
	}
}

__attribute__((noinline))
static uint64_t run_branchy(const uint8_t *data, size_t n, int threshold){
	/* Branchy benchmark:
	 * The branch outcome depends on the input pattern.
	 *
	 * We keep a tiny amount of useful work in both paths so the branch
	 * remains meaningful and the compiler cannot trivially remove it. */
	uint64_t sum = 0;

	for (size_t i = 0; i < n; i++) {
		if (data[i] > threshold) {
			sum += 3;
		} else {
			sum += 1;
		}
	}
	
	return sum;
}

__attribute__((noinline))
static uint64_t run_branchless(const uint8_t *data, size_t n, int threshold) {
	/* Branchless reference:
	 * 	Convert the condition into arithmetic.
	 *
	 * If data[i] > threshold:
	 *  add 3
	 * else:
	 *  add1
	 *
	 * Equivalent arithmetic:
	 *  1 + 2 * (data[i] > threshold)
	 *
	 * The compiler may still choose different instruction sequences on
	 * different targets, but conceptually this version avoids a control-flow
	 * split int the source */
	uint64_t sum = 0;

	for (size_t i = 0; i < n; i++) {
		sum += 1u + 2u * (uint64_t)(data[i] > threshold);
	}

	return sum;
}

static void print_text_result(const config_t *cfg, uint64_t elapsed_ns, uint64_t sum) {
	double ns_per_iter = (double)elapsed_ns / (double)cfg->iters;

	printf("[configuration]\n");
	printf("  mode		= %s\n", mode_to_string(cfg->mode));
	printf("  iters		= %zu\n", cfg->iters);
	printf("  warmup	= %zu\n", cfg->warmup);
	printf("  threshold	= %d\n", cfg->threshold);
	printf("  seed		= %" PRIu32 "\n", cfg->seed);
	printf("  pin_cpu	= %d\n", cfg->pin_cpu);
	printf("\n");

	printf("[result]\n");
	printf("  elapsed_ns	= %" PRIu64 "\n", elapsed_ns);
	printf("  ns_per_iter	= %.4f\n", ns_per_iter);
	printf("  sum		= %" PRIu64 "\n", sum);
}

static void print_csv_header(void) {
    printf("mode,iters,warmup,threshold,seed,pin_cpu,elapsed_ns,ns_per_iter,sum\n");
}

static void print_csv_row(const config_t *cfg, uint64_t elapsed_ns, uint64_t sum) {
    double ns_per_iter = (double)elapsed_ns / (double)cfg->iters;

    printf("%s,%zu,%zu,%d,%" PRIu32 ",%d,%" PRIu64 ",%.6f,%" PRIu64 "\n",
           mode_to_string(cfg->mode),
           cfg->iters,
           cfg->warmup,
           cfg->threshold,
           cfg->seed,
           cfg->pin_cpu,
           elapsed_ns,
           ns_per_iter,
           sum);
}

int main(int argc, char **argv) {
	config_t cfg = parse_args(argc, argv);

	pin_to_cpu_if_requested(cfg.pin_cpu);

	uint8_t *data = alloc_data(cfg.iters);
	fill_pattern(data, cfg.iters, cfg.mode, cfg.threshold, cfg.seed);

	/*
	 * Warmup:
	 *   We execute a smaller run before timing to reduce cold-start effects.
	 *   We reuse the first warmup elements from the same data buffer.
	 */
	size_t warmup_n = cfg.warmup < cfg.iters ? cfg.warmup : cfg.iters;
	uint64_t warm = 0;

	if (cfg.mode == MODE_BRANCHLESS_RANDOM_50) {
		warm = run_branchless(data, warmup_n, cfg.threshold);
	} else {
		warm = run_branchy(data, warmup_n, cfg.threshold);
	}
	g_sink ^= warm;

	uint64_t start = now_ns();
	uint64_t sum = 0;

	if (cfg.mode == MODE_BRANCHLESS_RANDOM_50) {
		sum = run_branchless(data, cfg.iters, cfg.threshold);
	} else {
		sum = run_branchy(data, cfg.iters, cfg.threshold);
	}

	uint64_t end = now_ns();
	uint64_t elapsed_ns = end - start;

	// Touch global volatile sink so the compiler cannot ignore results.
	g_sink ^= sum;

	if (cfg.csv) {
		print_csv_header();
		print_csv_row(&cfg, elapsed_ns, sum);
	} else {
		print_text_result(&cfg, elapsed_ns, sum);
	}

	free(data);
	return 0;
}
