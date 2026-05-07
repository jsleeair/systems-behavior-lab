/*
 * src/flamegraph_lab.c
 *
 * Flame Graph lab workload.
 *
 * Goal:
 *   This program intentionally creates a multi-stage call hierarchy where
 *   several different parent functions call the same hot leaf function.
 *   A flat perf report will often show the leaf function as the main hotspot,
 *   but a Flame Graph makes it easier to see which call path produced the cost.
 *
 * Build recommendation:
 *   Use -fno-omit-frame-pointer so perf can unwind stacks with --call-graph fp.
 *   Use -fno-optimize-sibling-calls to reduce tail-call transformations that
 *   may hide frames in educational profiling experiments.
 *
 * This is not meant to model one real application exactly.
 * It is a controlled workload for learning stack-based CPU profiling.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

static volatile uint64_t g_sink = 0;

typedef struct Config {
	size_t elements;
	int rounds;
	int frontend_weight;
	int backend_weight;
	int cleanup_weight;
	unsigned seed;
} Config;

static void die(const char *msg) {
	perror(msg);
	exit(1);
}

static uint64_t now_ns(void) {
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        die("clock_gettime");
    }

    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t parse_u64_arg(const char *s, const char *name) {
    char *end = NULL;
    errno = 0;

    unsigned long long v = strtoull(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(1);
    }

    return (uint64_t)v;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  --elements N          Number of uint64_t elements. Default: 1048576\n"
            "  --rounds N            Number of pipeline rounds. Default: 60\n"
            "  --frontend-weight N   Repeats for frontend stage. Default: 2\n"
            "  --backend-weight N    Repeats for backend stage. Default: 3\n"
            "  --cleanup-weight N    Repeats for cleanup stage. Default: 1\n"
            "  --seed N              Data initialization seed. Default: 1\n"
            "  --help                Show this message.\n",
            prog);
}

/*
 * A small deterministic pseudo-random generator.
 * This avoids external dependencies and keeps the workload reproducible.
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
 * Shared hot leaf function.
 *
 * Several different higher-level stages call this same function.
 * This is intentional: in a flat report, mix_u64 may dominate,
 * but the Flame Graph should separate the caller paths.
 */
NOINLINE static uint64_t mix_u64(uint64_t x) {
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdull;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ull;
	x ^= x >> 33;

	return x;
}

/*
 * Another CPU-heavy leaf function with a different shape.
 * It is used by the compression-like backedn stage.
 */
NOINLINE static uint64_t rotate_scramble(uint64_t x) {
	for (int i = 0; i < 8; ++i) {
		x = (x << 7) | (x >> (64 - 7));
		x ^= 0x9e3779b97f4a7c15ull;
		x += (x >> 3);
	}

	return x;
}

/*
 * Branch-heavy scoring function.
 *
 * This gives the profile a branchy-looking path that differs from the
 * pure arithmetic mix path. It is still deterministic.
 */
NOINLINE static uint64_t branchy_score(uint64_t x) {
	uint64_t score = 0;

	for (int i = 0; i < 16; ++i) {
		uint64_t bit = (x >> i) & 1ull;

		if (bit) {
			score += x ^ (0x123456789abcdef0ull + (uint64_t)i);
		} else {
			score += (x >> 1) ^ (0x0fedcba987654321ull - (uint64_t)i);
		}

		x = mix_u64(x + score);
	}

	return score;
}

/*
 * Fronted-like parser.
 *
 * This represents work such as parsing, tokenization, decoding, or light
 * transformation. It calls the shared hot leaf mix_u64.
 */
NOINLINE static uint64_t parse_records(uint64_t *data, size_t n) {
	uint64_t acc = 0;

	for (size_t i = 0; i < n; ++i) {
		uint64_t x = data[i];

		x ^= (uint64_t)i * 0x9e3779b97f4a7c15ull;
		acc += mix_u64(x);
	}

	return acc;
}

/*
 * Frontend-like validation.
 *
 * This gives the frontend stage a second visible branch in the call tree.
 */
NOINLINE static uint64_t validate_records(uint64_t *data, size_t n) {
	uint64_t acc = 0;

	/* Step by 4 to make this lighter than parse_records. */
	for (size_t i = 0; i < n; i += 16) {
		acc ^= branchy_score(data[i] + i);
	}

	return acc;
}

NOINLINE static uint64_t frontend_stage(uint64_t *data, size_t n, int weight) {
	uint64_t acc = 0;

	for (int r = 0; r < weight; ++r) {
		acc += parse_records(data, n);
		acc ^= validate_records(data, n);
	}

	return acc;
}

/*
 * Backend-like hash index build.
 *
 * This deliberately calls the same shared leaf mix_u64 as parse_records.
 * Flame Graph should show that mix_u64 cost comes from multiple parents.
 */
NOINLINE static uint64_t hash_index_build(uint64_t *data, size_t n) {
	uint64_t acc = 0xcbf29ce484222325ull;

	for (size_t i = 0; i < n; ++i) {
		uint64_t h = mix_u64(data[i] ^ acc);
		acc ^= h;
		acc *= 0x100000001b3ull;
	}

	return acc;
}

/*
 * Backed-like compression pass.
 *
 * This creates a separate hot branch that should be visible in the Flame Graph.
 */
NOINLINE static uint64_t compress_blocks(uint64_t *data, size_t n) {
	uint64_t acc = 0;

	for (size_t i = 0; i < n; i +=2) {
		uint64_t x = data[i];

		if (i + 1 < n) {
			x ^= data[i + 1];
		}

		acc += rotate_scramble(x);
	}

	return acc;
}

NOINLINE static uint64_t backend_stage(uint64_t *data, size_t n, int weight) {
	uint64_t acc = 0;

	for (int r = 0; r < weight; ++r) {
		acc += hash_index_build(data, n);
		acc ^= compress_blocks(data, n);
	}

	return acc;
}

/*
 * Cleanup-like memory sweep.
 *
 * This gives the profile a simpler memory traversal path.
 * It is intentionally lighter by default.
 */
NOINLINE static uint64_t memory_sweep(uint64_t *data, size_t n) {
	uint64_t acc = 0;

	for (size_t i = 0; i < n; i += 8) {
		acc += data[i];
		data[i] ^= acc + i;
	}

	return acc;
}

NOINLINE static uint64_t cleanup_stage(uint64_t *data, size_t n, int weight) {
	uint64_t acc = 0;

	for (int r = 0; r < weight; ++r) {
		acc ^= memory_sweep(data, n);
	}

	return acc;
}

NOINLINE static uint64_t run_pipeline(uint64_t *data, const Config *cfg) {
	uint64_t acc = 0;

	for (int round = 0; round < cfg->rounds; ++round) {
		acc += frontend_stage(data, cfg->elements, cfg->frontend_weight);
		acc ^= backend_stage(data, cfg->elements, cfg->backend_weight);
		acc += cleanup_stage(data, cfg->elements, cfg->cleanup_weight);

		/*
		 * Mutate a small part of the input so the compiler cannot safely
		 * treat all rounds as redundant.
		 */
		data[(size_t)round % cfg->elements] ^= acc + (uint64_t)round;
	}

	return acc;
}

static void init_data(uint64_t *data, size_t n, unsigned seed) {
	uint64_t state = (uint64_t)seed + 0x12345678ull;

	for (size_t i = 0; i < n; ++i) {
		data[i] = xorshift64(&state);
	}
}

int main(int argc, char **argv) {
	Config cfg;

	cfg.elements = 1024 * 1024;
	cfg.rounds = 60;
	cfg.frontend_weight = 2;
	cfg.backend_weight = 3;
	cfg.cleanup_weight = 1;
	cfg.seed = 1;

	for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--elements") == 0 && i + 1 < argc) {
            cfg.elements = (size_t)parse_u64_arg(argv[++i], "elements");
        } else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            cfg.rounds = (int)parse_u64_arg(argv[++i], "rounds");
        } else if (strcmp(argv[i], "--frontend-weight") == 0 && i + 1 < argc) {
            cfg.frontend_weight = (int)parse_u64_arg(argv[++i], "frontend-weight");
        } else if (strcmp(argv[i], "--backend-weight") == 0 && i + 1 < argc) {
            cfg.backend_weight = (int)parse_u64_arg(argv[++i], "backend-weight");
        } else if (strcmp(argv[i], "--cleanup-weight") == 0 && i + 1 < argc) {
            cfg.cleanup_weight = (int)parse_u64_arg(argv[++i], "cleanup-weight");
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = (unsigned)parse_u64_arg(argv[++i], "seed");
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (cfg.elements == 0 || cfg.rounds <= 0) {
        fprintf(stderr, "elements and rounds must be positive\n");
        return 1;
    }

    uint64_t *data = NULL;
    size_t bytes = cfg.elements * sizeof(uint64_t);

    if (posix_memalign((void **)&data, 64, bytes) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        return 1;
    }

    init_data(data, cfg.elements, cfg.seed);

    uint64_t start = now_ns();
    uint64_t result = run_pipeline(data, &cfg);
    uint64_t end = now_ns();

    g_sink ^= result;

    printf("elements=%zu,rounds=%d,frontend_weight=%d,backend_weight=%d,cleanup_weight=%d,elapsed_ns=%" PRIu64 ",checksum=%" PRIu64 "\n",
           cfg.elements,
           cfg.rounds,
           cfg.frontend_weight,
           cfg.backend_weight,
           cfg.cleanup_weight,
           end - start,
           result);

    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    free(data);
    return 0;
}
