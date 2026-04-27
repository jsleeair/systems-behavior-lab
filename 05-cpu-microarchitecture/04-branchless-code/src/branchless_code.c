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
	MODE_BRANCHY = 0,
	MODE_BRANCHLESS = 1
} my_mode_t;

typedef enum {
	PATTERN_RANDOM50 = 0,
	PATTERN_MOSTLY_FALSE = 1,
	PATTERN_MOSTLY_TRUE = 2,
	PATTERN_ALTERNATING = 3
} pattern_t;

typedef struct {
	my_mode_t mode;
	pattern_t pattern;
	size_t elements;
	uint32_t threshold;
	int repeats;
	int warmup;
	int pin_cpu;
	int csv;
	int csv_header;
} options_t;

/*
 * Prevent the compiler from optimizing away the measured result.
 * This is intentionally global and volatile.
 */
static volatile uint64_t g_sink = 0;

/*
 * Return monotonic time in nanoseconds.
 * CLOCK_MONOTONIC is appropriate for interval measurement.
 */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(1);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Pin the current process to a single CPU core.
 * This helps reduce scheduler noise.
 *
 * If pin_cpu < 0, do nothing.
 */
static void maybe_pin_cpu(int pin_cpu) {
#ifdef __linux__
    if (pin_cpu < 0) {
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((unsigned)pin_cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        exit(1);
    }
#else
    (void)pin_cpu;
#endif
}

/*
 * Small deterministic PRNG.
 * We want reproducible data generation across runs.
 */
static uint32_t xorshift32(uint32_t *state) {
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static const char *mode_name(my_mode_t mode) {
	switch (mode) {
		case MODE_BRANCHY: return "branchy";
		case MODE_BRANCHLESS: return "branchless";
		default: return "unknown";
	}
}

static const char *pattern_name(pattern_t pattern) {
	switch (pattern) {
		case PATTERN_RANDOM50: return "random50";
		case PATTERN_MOSTLY_FALSE: return "mostly_false";
		case PATTERN_MOSTLY_TRUE: return "mostly_true";
		case PATTERN_ALTERNATING: return "alternating";
		default: return "unknown";
	}
}

static my_mode_t parse_mode(const char *s) {
	if (strcmp(s, "branchy") == 0) return MODE_BRANCHY;
	if (strcmp(s, "branchless") == 0) return MODE_BRANCHLESS;

	fprintf(stderr, "invalid mode: %s\n", s);
	exit(1);

	/* Unreachable, but keeps the compiler happy. */
    	return MODE_BRANCHY;
}

static pattern_t parse_pattern(const char *s) {
	if (strcmp(s, "random50") == 0) return PATTERN_RANDOM50;
	if (strcmp(s, "mostly_false") == 0) return PATTERN_MOSTLY_FALSE;
	if (strcmp(s, "mostly_true") == 0) return PATTERN_MOSTLY_TRUE;
	if (strcmp(s, "alternating") == 0) return PATTERN_ALTERNATING;

	fprintf(stderr, "invalid pattern: %s\n", s);
	exit(1);

	/* Unreachable, but keeps the compiler happy. */
    	return PATTERN_RANDOM50;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --mode <branchy|branchless>\n"
        "  --pattern <random50|mostly_false|mostly_true|alternating>\n"
        "  --elements <N>\n"
        "  --threshold <u32>\n"
        "  --repeats <N>\n"
        "  --warmup <N>\n"
        "  --pin-cpu <cpu_id>   (-1 means do not pin)\n"
        "  --csv                output CSV row only\n"
        "  --csv-header         output CSV header only\n"
        "\n",
        prog
    );
}

/*
 * Parse unsigned integer from a string with validation.
 */
static uint64_t parse_u64(const char *s, const char *what) {
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", what, s);
        exit(1);
    }
    return (uint64_t)v;
}

static void parse_args(int argc, char **argv, options_t *opt) {
    int i;

    /* Reasonable defaults for a CPU microbenchmark. */
    opt->mode = MODE_BRANCHY;
    opt->pattern = PATTERN_RANDOM50;
    opt->elements = 1 << 20;
    opt->threshold = 2147483648u; /* roughly 50% split for uniform uint32_t */
    opt->repeats = 10;
    opt->warmup = 2;
    opt->pin_cpu = -1;
    opt->csv = 0;
    opt->csv_header = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->mode = parse_mode(argv[i]);
        } else if (strcmp(argv[i], "--pattern") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->pattern = parse_pattern(argv[i]);
        } else if (strcmp(argv[i], "--elements") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->elements = (size_t)parse_u64(argv[i], "elements");
        } else if (strcmp(argv[i], "--threshold") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->threshold = (uint32_t)parse_u64(argv[i], "threshold");
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->repeats = (int)parse_u64(argv[i], "repeats");
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->warmup = (int)parse_u64(argv[i], "warmup");
        } else if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (++i >= argc) usage(argv[0]), exit(1);
            opt->pin_cpu = (int)strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "--csv") == 0) {
            opt->csv = 1;
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            opt->csv_header = 1;
        } else {
            usage(argv[0]);
            exit(1);
        }
    }
}

/*
 * Fill data[] so that the condition (data[i] >= threshold) follows the requested pattern.
 *
 * We intentionally generate values on the correct side of the threshold instead of relying on
 * probability after random sampling. That gives us tighter control over hit rate.
 */
static void fill_pattern(uint32_t *data, uint32_t *weights, size_t n, uint32_t threshold, pattern_t pattern) {
	uint32_t rng = 0x12345678u;
	size_t i;

	for (i = 0; i < n; ++i) {
		int take = 0;

		switch (pattern) {
			case PATTERN_RANDOM50:
				take = (int)(xorshift32(&rng) & 1u);
				break;

			case PATTERN_MOSTLY_FALSE:
				/* About 1/64 true. */
				take = ((xorshift32(&rng) & 63u) == 0u);
				break;

			case PATTERN_MOSTLY_TRUE:
				/* About 63/64 true. */
				take = ((xorshift32(&rng) & 63u) != 0u);
				break;

			case PATTERN_ALTERNATING:
				take = (int)(i & 1u);
				break;

			default:
				fprintf(stderr, "internal error: unknown pattern\n");
				exit(1);
		}

		/*
		 * Build a value guaranteed to be either below threshold or at/above threshold.
		 *
		 * Edge-case handling:
		 * - threshold == 0	=> everything is >= threshold
		 * - threshold == UINT32_MAX
		 *   only UINT32_MAX itself is >= threshold
		 */
		if (take) {
			if (threshold == UINT32_MAX) {
				data[i] = UINT32_MAX;
			} else {
				uint32_t span = UINT32_MAX - threshold;
				uint32_t r = xorshift32(&rng);
				data[i] = threshold + (span ? (r % (span + 1u)) : 0u);
			}
		} else {
			if (threshold == 0u) {
				data[i] = 0u;
			} else {
				uint32_t r = xorshift32(&rng);
				data[i] = r % threshold;
			}
		}

		/*
		 * Weight is an arbitrary non-zero payload. This gives the loop some real work
		 * and prevents the benchmark from degenerating into a pure count.
		 */
		weights[i] = (xorshift32(&rng) & 1023u) + 1u;
	}
}

/*
 * Count the number of elements that satisfy the predicate.
 * This is useful to confirm the actual hit rate of the generated dataset.
 */
static size_t count_hits(const uint32_t *data, size_t n, uint32_t threshold) {
	size_t hits = 0;
	size_t i;
	for (i = 0; i < n; ++i) {
		hits += (size_t)(data[i] >= threshold);
	}
	return hits;
}

/*
 * Traditional branchy version.
 *
 * If branch prediction is accurate, the CPU may run this very efficiently.
 * If prediction accuracy is poor, misprediction penalties can dominate.
 */
static uint64_t run_branchy(const uint32_t *data, const uint32_t *weights, size_t n, uint32_t threshold, int repeats) {
	uint64_t sum = 0;
	int r;
	size_t i;

	for (r = 0; r < repeats; ++r) {
		for (i = 0; i < n; ++i) {
			if (data[i] >= threshold) {
				sum += weights[i];
			}
		}
	}

	return sum;
}

/*
 * Branchless version.
 *
 * The comparison still exists, but we convert its result to 0 or 1 and use arithmetic
 * instead of control-flow redirection. This often avoids branch misprediction cost,
 * but it also means the loop always performs the add/multiply path.
 */
static uint64_t run_branchless(const uint32_t *data, const uint32_t *weights, size_t n, uint32_t threshold, int repeats) {
	uint64_t sum = 0;
	int r;
	size_t i;

	for (r = 0; r < repeats; ++r) {
		for (i = 0; i < n; ++i) {
			uint64_t take = (uint64_t)(data[i] >= threshold);
			sum += take * (uint64_t)weights[i];
		}
	}

	return sum;
}

int main(int argc, char **argv) {
	options_t opt;
	uint32_t *data = NULL;
	uint32_t *weights = NULL;
	uint64_t start_ns, end_ns, elapsed_ns;
	uint64_t checksum = 0;
	double ns_per_element = 0.0;
	double hit_rate = 0.0;
	size_t hits = 0;
	int w;

	parse_args(argc, argv, &opt);

	if (opt.csv_header) {
		printf("mode,pattern,elements,threshold,repeats,warmup,pin_cpu,hits,hit_rate,elapsed_ns,ns_per_element,checksum\n");
        return 0;
    }

	maybe_pin_cpu(opt.pin_cpu);

	data = (uint32_t *)aligned_alloc(64, opt.elements * sizeof(uint32_t));
	weights = (uint32_t *)aligned_alloc(64, opt.elements * sizeof(uint32_t));
	if (!data || !weights) {
		fprintf(stderr, "allocation failed\n");
		free(data);
		free(weights);
		return 1;
	}

	fill_pattern(data, weights, opt.elements, opt.threshold, opt.pattern);
	hits = count_hits(data, opt.elements, opt.threshold);
	hit_rate = (opt.elements == 0) ? 0.0 : (double)hits / (double)opt.elements;

	/*
	 * Warmup runs reduce cold-start effects such as:
	 * - page faults
	 * - instruction cache coldness
	 * - branch predictor cold state
	 */
	for (w = 0; w < opt.warmup; ++w) {
		if (opt.mode == MODE_BRANCHY) {
			checksum ^= run_branchy(data, weights, opt.elements, opt.threshold, 1);
		} else {
			checksum ^= run_branchless(data, weights, opt.elements, opt.threshold, 1);
		}
	}

	start_ns = now_ns();

	if (opt.mode == MODE_BRANCHY) {
		checksum ^= run_branchy(data, weights, opt.elements, opt.threshold, opt.repeats);
	} else {
		checksum ^= run_branchless(data, weights, opt.elements, opt.threshold, opt.repeats);
	}

	end_ns = now_ns();
	elapsed_ns = end_ns - start_ns;

	g_sink ^= checksum;

	if (opt.csv) {
        printf("%s,%s,%zu,%" PRIu32 ",%d,%d,%d,%zu,%.6f,%" PRIu64 ",%.6f,%" PRIu64 "\n",
               mode_name(opt.mode),
               pattern_name(opt.pattern),
               opt.elements,
               opt.threshold,
               opt.repeats,
               opt.warmup,
               opt.pin_cpu,
               hits,
               hit_rate,
               elapsed_ns,
               ns_per_element,
               checksum);
    } else {
        printf("mode=%s pattern=%s elements=%zu threshold=%" PRIu32
               " repeats=%d warmup=%d pin_cpu=%d hits=%zu hit_rate=%.6f "
               "elapsed_ns=%" PRIu64 " ns_per_element=%.6f checksum=%" PRIu64 "\n",
               mode_name(opt.mode),
               pattern_name(opt.pattern),
               opt.elements,
               opt.threshold,
               opt.repeats,
               opt.warmup,
               opt.pin_cpu,
               hits,
               hit_rate,
               elapsed_ns,
               ns_per_element,
               checksum);
    }

    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    free(data);
    free(weights);
    return 0;
}
