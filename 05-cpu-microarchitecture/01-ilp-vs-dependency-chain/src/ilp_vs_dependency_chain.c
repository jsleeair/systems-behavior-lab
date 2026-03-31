/*
 * SPDX-Licensce-Identifier: MIT
 * ilp_vs_dependency_chain.c
 *
 * This benchamrk demonstrates the difference between:
 *   1. a tight dependency chain, where each operation depends on the
 *      result of the previous one, and
 *   2. multiple independent arithmetic streams, where the CPU can overlap
 *      execution and exploit instruction-level parallelism (ILP).
 *
 * The goal is to isolate core execution behavior as much as possible.
 * We intentionally keep the working set in registers and avoid memory-heavy
 * logic so that the dominant factor becomes:
 *   - dependency latency for chain-like code
 *   - execution throughput / available ILP for independent streams
 *
 * Build example:
 *   make
 *
 * Run example:
 *   ./artifacts/bin/ilp_vs_dependency_chain --iters 200000000 --repeats 5 --warmup 1 --pin-cpu 0
 *
 * Notes:
 * - This benchmark is designed for x86-64 Linux, but most of the code is portable.
 * - CPU pinning use sched_setaffinity and may fail on restricted environments.
 * - Timing uses CLOCK_MONOTONIC_RAW when available.
 * - We use volatile sinks and noinline functions to reduce the chance that the 
 *   compiler eliminates the benchmarked loops.
 *
 * Suggested compiler flags:
 *   -O3 -march=native -fno-omit-frame-pointer
 *
 * Why add/xor?
 * - Simple integer operations are easy for the core to execute.
 * - Multiple independent accumulators expose ILP.
 * - A single accumulator creates a true read-after-write dependency chain.
 */

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

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

static volatile uint64_t g_sink = 0;

typedef struct {
	const char *mode;
	uint64_t iters;
	int warmup;
	int pin_cpu;
	int repeats;
	uint64_t elapsed_ns;
	uint64_t ops;
	double ns_per_op;
	uint64_t checksum;
} result_t;

static void die(const char *msg) {
	fprintf(stderr, "error: %s\n", msg);
	exit(10);
}

static uint64_t parse_u64(const char *s, const char *name) {
	char *end = NULL;
	errno = 0;
	unsigned long long v = strtoull(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "invalid %s: %s\n", name, s);
		exit(1);
	}
	return (uint64_t)v;
}

static int parse_i32(const char *s, const char *name) {
	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "invalid %s: %s\n", name, s);
		exit(1);
	}
	return (int)v;
}

static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		die("clock_gettime failed");
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu_if_requested(int cpu) {
	if (cpu < 0) {
		return;
	}

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET((size_t)cpu, &set);

	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
		die("failed to pin process to requested CPU");
	}
}

static void print_usage(const char *prog) {
	fprintf(stderr,
		"usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  --mode <all|chain1|indep2|indep4|indep8>	benchmark mode (default: all)\n"
		"  --iters <N>					loop iterations per repeat (default: 200000000)\n"
		"  --repeats <N>				measured repeats per mode (default: 5)\n"
		"  --warmup <0|1>				run one warmup before measurement (default: 1)\n"
		"  --pin-cpu <cpu_id|-1>			pin process to a CPU (default: -1)\n"
		"  --csv					print CSV rows in addition to text summary\n"
		"  --help					show this message\n",
		prog
	       );
}

static inline uint64_t mix_u64(uint64_t x) {
	/*
	 * A small integer mixing function to prevent the optimizer from treating
	 * the arithmetic as trivial or folding everything into constants too eaily.
	 */
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return x;
}

__attribute__((noinline))
static uint64_t bench_chain1(uint64_t iters) {
	/*
	 * One accumulator.
	 * Every new value of 'a' depends on the previous one.
	 * This creates a long dependency chain:
	 *
	 *   a_(i+1) = f(a_i)
	 *
	 * The CPU cannot freely overlap these operations because each iteration
	 * needs the previous result first.
	 */
	uint64_t a = 0x123456789abcdef0ULL;

	for (uint64_t i = 0; i < iters; ++i) {
		a += 0x9e3779b97f4a7c15ULL;
		a ^= (a >> 13);
		a += 0xbf58476d1ce4e5b9ULL;
		a ^= (a << 7);
	}

	return mix_u64(a);
}

__attribute__((noinline))
static uint64_t bench_indep2(uint64_t iters) {
	/*
	 * Two independent accumulators.
	 * Each stream still has internal dependency, but the two streams are
	 * independent of each other. This gives the out-of-order core more freedom
	 * to overlap execution.
	 */
	uint64_t a = 0x123456789abcdef0ULL;
    uint64_t b = 0x0fedcba987654321ULL;

    for (uint64_t i = 0; i < iters; ++i) {
        a += 0x9e3779b97f4a7c15ULL;
        a ^= (a >> 13);
        a += 0xbf58476d1ce4e5b9ULL;
        a ^= (a << 7);

        b += 0x94d049bb133111ebULL;
        b ^= (b >> 11);
        b += 0xda942042e4dd58b5ULL;
        b ^= (b << 9);
    }

    return mix_u64(a ^ b);
}

__attribute__((noinline))
static uint64_t bench_indep4(uint64_t iters) {
	/*
	 * Four independent streams.
	 * If the processor has enough rename registers, issue bandwidth, and
	 * suitable execution resources, this often performs much better than
	 * chain1 because the core cna hide the latency of one chain behind
	 * work from the others.
	 */
	uint64_t a = 0x123456789abcdef0ULL;
    uint64_t b = 0x0fedcba987654321ULL;
    uint64_t c = 0x55aa55aa55aa55aaULL;
    uint64_t d = 0xaa55aa55aa55aa55ULL;

    for (uint64_t i = 0; i < iters; ++i) {
        a += 0x9e3779b97f4a7c15ULL;
        a ^= (a >> 13);
        a += 0xbf58476d1ce4e5b9ULL;
        a ^= (a << 7);

        b += 0x94d049bb133111ebULL;
        b ^= (b >> 11);
        b += 0xda942042e4dd58b5ULL;
        b ^= (b << 9);

        c += 0x369dea0f31a53f85ULL;
        c ^= (c >> 17);
        c += 0xdb4f0b9175ae2165ULL;
        c ^= (c << 5);

        d += 0x2545f4914f6cdd1dULL;
        d ^= (d >> 19);
        d += 0x9e6c63d0676a9a99ULL;
        d ^= (d << 3);
    }

    return mix_u64(a ^ b ^ c ^ d);
}

__attribute__((noinline))
static uint64_t bench_indep8(uint64_t iters) {
	/*
	 * Eight independent streams.
	 * This may continue improving on wide out-of-order cores, but eventually
	 * the benchmark hits other limits: register pressure, issue/retire width,
	 * front-end bandwidth, or execution-port saturation.
	 */
	uint64_t a0 = 0x123456789abcdef0ULL;
    uint64_t a1 = 0x0fedcba987654321ULL;
    uint64_t a2 = 0x55aa55aa55aa55aaULL;
    uint64_t a3 = 0xaa55aa55aa55aa55ULL;
    uint64_t a4 = 0x1111222233334444ULL;
    uint64_t a5 = 0x9999aaaabbbbccccULL;
    uint64_t a6 = 0x3141592653589793ULL;
    uint64_t a7 = 0x2718281828459045ULL;

    for (uint64_t i = 0; i < iters; ++i) {
        a0 += 0x9e3779b97f4a7c15ULL; a0 ^= (a0 >> 13); a0 += 0xbf58476d1ce4e5b9ULL; a0 ^= (a0 << 7);
        a1 += 0x94d049bb133111ebULL; a1 ^= (a1 >> 11); a1 += 0xda942042e4dd58b5ULL; a1 ^= (a1 << 9);
        a2 += 0x369dea0f31a53f85ULL; a2 ^= (a2 >> 17); a2 += 0xdb4f0b9175ae2165ULL; a2 ^= (a2 << 5);
        a3 += 0x2545f4914f6cdd1dULL; a3 ^= (a3 >> 19); a3 += 0x9e6c63d0676a9a99ULL; a3 ^= (a3 << 3);
        a4 += 0x632be59bd9b4e019ULL; a4 ^= (a4 >> 7);  a4 += 0x8cb92ba72f3d8dd7ULL; a4 ^= (a4 << 11);
        a5 += 0xa24baed4963ee407ULL; a5 ^= (a5 >> 5);  a5 += 0x9fb21c651e98df25ULL; a5 ^= (a5 << 13);
        a6 += 0xb7e151628aed2a6bULL; a6 ^= (a6 >> 23); a6 += 0xc6bc279692b5cc83ULL; a6 ^= (a6 << 2);
        a7 += 0x165667b19e3779f9ULL; a7 ^= (a7 >> 29); a7 += 0xe7037ed1a0b428dbULL; a7 ^= (a7 << 6);
    }

    return mix_u64(a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7);
}

typedef uint64_t (*bench_fn_t)(uint64_t);

static result_t run_one(const char *mode, bench_fn_t fn, uint64_t iters, int warmup, int pin_cpu) {
	if (warmup) {
		g_sink ^= fn(iters / 10 ? iters / 10 : 1);
	}

	uint64_t start = now_ns();
	uint64_t checksum = fn(iters);
	uint64_t end = now_ns();

	result_t r;
	memset(&r, 0, sizeof(r));
	r.mode = mode;
	r.iters = iters;
	r.warmup = warmup;
	r.pin_cpu = pin_cpu;
	r.elapsed_ns = end - start;
	r.checksum = checksum;

	// Each iteration executes 4 arithmetic/bitwise update statements per stream.
	if (strcmp(mode, "chain1") == 0) {
		r.ops = iters * 4;
	} else if (strcmp(mode, "indep2") == 0) {
		r.ops = iters * 8;
	} else if (strcmp(mode, "indep4") == 0) {
		r.ops = iters * 16;
	} else if (strcmp(mode, "indep8") == 0) {
		r.ops = iters * 32;
	} else {
		r.ops = 0;
	}

	r.ns_per_op = (double)r.elapsed_ns / (double)r.ops;
	g_sink ^= checksum;
	return r;
}

static void print_text_result(const result_t *r) {
	fprintf(stderr,
		"mode=%s,iters=%" PRIu64 ",warmup=%d,pin_cpu=%d,elapsed_ns=%" PRIu64
		",ops=%" PRIu64 ",ns_per_op=%.6f,checksum=%" PRIu64 "\n",
		r->mode, r->iters, r->warmup, r->pin_cpu,
		r->elapsed_ns, r->ops, r->ns_per_op, r->checksum);
}

static void print_csv_header(void) {
	printf("mode,iters,warmup,pin_cpu,elapsed_ns,ops,ns_per_op,checksum\n");
}

static void print_csv_row(const result_t *r) {
	printf("%s,%" PRIu64 ",%d,%d,%" PRIu64 ",%" PRIu64 ",%.6f,%" PRIu64 "\n",
	r->mode, r->iters, r->warmup, r->pin_cpu,
	r->elapsed_ns, r->ops, r->ns_per_op, r->checksum);
}

static void run_mode(const char *mode, bench_fn_t fn, uint64_t iters, int repeats, int warmup, int pin_cpu, int emit_csv) {
	double best = 0.0;
	double sum = 0.0;
	result_t best_r;
	memset(&best_r, 0, sizeof(best_r));
	int first = 1;

	for (int i = 0; i < repeats; ++i) {
		result_t r = run_one(mode, fn, iters, warmup, pin_cpu);
		r.repeats = repeats;

		print_text_result(&r);
		if (emit_csv) {
			print_csv_row(&r);
		}

		sum += r.ns_per_op;
		if (first || r.ns_per_op < best) {
			best = r.ns_per_op;
			best_r = r;
			first = 0;
		}
	}
	fprintf(stderr,
		"[summary] mode=%s repeats=%d best_ns_per_op=%.6f avg_ns_per_op=%.6f best_elapsed_ns=%" PRIu64 "\n",
		mode, repeats, best, sum / (double)repeats, best_r.elapsed_ns);
}

int main(int argc, char **argv) {
	const char *mode = "all";
	uint64_t iters = 200000000ULL;
	int repeats = 5;
	int warmup = 1;
	int pin_cpu = -1;
	int emit_csv = 0;

	for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) die("missing value for --mode");
            mode = argv[i];
        } else if (strcmp(argv[i], "--iters") == 0) {
            if (++i >= argc) die("missing value for --iters");
            iters = parse_u64(argv[i], "iters");
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (++i >= argc) die("missing value for --repeats");
            repeats = parse_i32(argv[i], "repeats");
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (++i >= argc) die("missing value for --warmup");
            warmup = parse_i32(argv[i], "warmup");
        } else if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (++i >= argc) die("missing value for --pin-cpu");
            pin_cpu = parse_i32(argv[i], "pin_cpu");
        } else if (strcmp(argv[i], "--csv") == 0) {
            emit_csv = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
          }
	}

	if (repeats <= 0) {
    		die("repeats must be >= 1");
	}

	if (iters == 0) {
    		die("iters must be >= 1");
	}

	pin_to_cpu_if_requested(pin_cpu);

	if (emit_csv) {
		print_csv_header();
	}

	if (strcmp(mode, "all") == 0 || strcmp(mode, "chain1") == 0) {
		run_mode("chain1", bench_chain1, iters, repeats, warmup, pin_cpu, emit_csv);
	}

	if (strcmp(mode, "all") == 0 || strcmp(mode, "indep2") == 0) {
		run_mode("indep2", bench_indep2, iters, repeats, warmup, pin_cpu, emit_csv);
	}

	if (strcmp(mode, "all") == 0 || strcmp(mode, "indep4") == 0) {
        run_mode("indep4", bench_indep4, iters, repeats, warmup, pin_cpu, emit_csv);
    }

    if (strcmp(mode, "all") == 0 || strcmp(mode, "indep8") == 0) {
        run_mode("indep8", bench_indep8, iters, repeats, warmup, pin_cpu, emit_csv);
    }

    // Print the sink so the compiler cannot assume all benchmark results are dead.
    fprintf(stderr, "[sink] %" PRIu64 "\n", g_sink);

    return 0;
}


