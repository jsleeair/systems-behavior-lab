#define _GNU_SOURCE

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/*
 * This lab measures the cost of crossing the user/kernel boundary.
 *
 * We compare:
 *  1. An empty loop			-> loop/control overhead
 *  2. A normal function call		-> user-space call overhead
 *  3. getpid() wrapper call		-> libc wrapper around process ID retrieval
 *  4. syscall(SYS_getpid) directly	-> explicit syscall path
 *
 * Key benchmarking concerns:
 *   - Pervert the compiler from optimizing away the benchmarked work
 *   - Use a steady clock with nanosecond resolution
 *   - Run enough iterations so that timing noise becomes small
 *   - Optionally pin to one CPU to reduce scheduling noise
 */

/*
 * A global volatile sink is used to make benchmark results observable.
 * Writing to or reading from a volatile object prevents the compiler from
 * removing code paths as "dead" or "unused".
 */
static volatile long g_sink = 0;

/*
 * Return monotonic time in nanoseconds.
 *
 * CLOCK_MONOTONIC is preferred for elapsed-time measurement because it is
 * not affected by wall-clock adjustments.
 */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) !=0) {
		perror("clock_gettime");
		exit(1);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Best effort CPU pinning.
 *
 * Pinning the process to a single CPU can reduce run-to-run variance caused
 * by migration across cores. This is especially useful for microbenchmarks.
 *
 * If pinning falls, we do not abort; we simply continue.
 */
static void pin_to_cpu0_if_requested(int pin) {
	if (!pin) {
		return;
	}

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(0, &set);

	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		fprintf(stderr, "[warn] sched_setaffinity failed: %s\n", strerror(errno));
	}
}

/*
 * A noinline function prevents the compiler from inlining the call site.
 *
 * This allows us to measure actual function call overhead rather than an optiized-away direct expression.
 */
__attribute__((noinline))
static long trivial_function(long x) {
	return x + 1;
}

/*
 * Compiler barrier:
 * Prevents the compiler from proving the loop has not effect and deleting it.
 * This does NOT issue a CPU fench; it only constrains compiler optimization.
 */
static inline void compiler_barrier(void) {
	__asm__ volatile("" ::: "memory");
}

/*
 * Benchmark 1: empty loop.
 *
 * This measures loop-control overhead plus the cost of touching a volatile
 * variable. We intentionally keep a tiny side effect so the compliler cannot eliminate the loop entirely.
 */
static uint64_t bench_empty_loop(size_t iters) {
	uint64_t start = now_ns();

	for (size_t i = 0; i < iters; i++) {
		compiler_barrier();
	}

	uint64_t end = now_ns();
	return end - start;
}

/*
 * Benchmark 2: normal user-space function call.
 *
 * This remains in user mode. It does not cross into the kernel.
 * Comparing this against the syscall cases shows how expensive kernel entry
 * and return are relative to an ordinary call.
 */
static uint64_t bench_function_call(size_t iters) {
	long acc = 0;
	uint64_t start = now_ns();

	for (size_t i = 0; i < iters; i++) {
		acc += trivial_function((long)i);
	}

	uint64_t end = now_ns();
	g_sink += acc;
	return end - start;
}

/*
 * Benchmark 3: libc getpid();
 *
 * On many Linux systems, getpid() will eventually perform a real syscall.
 * This measures the practical overhead of asking the OS for the process ID
 * through the standard libc interface.
 *
 * Note: Some library calls on Linux can be serviced through vDSO instead of a 
 * real syscall, but getpid() is commonly treated as a genuine syscall path in 
 * user-space experiments. Still, actual behavior can depend on platfrom/libc.
 */
static uint64_t bench_getpid_wrapper(size_t iters) {
	long acc = 0;
	uint64_t start = now_ns();

	for (size_t i = 0; i < iters; i++) {
		acc += (long)getpid();
	}

	uint64_t end = now_ns();
	g_sink += acc;
	return end - start;
}

/*
 * Benchmark 4: direct syscall.
 *
 * This calls the generic syscall entry point directly with SYS_getpid.
 * It helps separate "libc wrapper overhead" from "kernel boundary crossing"
 * and gives a more explicit syscall path.
 */
static uint64_t bench_direct_syscall_getpid(size_t iters) {
	long acc = 0;
	uint64_t start = now_ns();

	for (size_t i = 0; i < iters; i++) {
		acc += (long)syscall(SYS_getpid);
	}

	uint64_t end = now_ns();
	g_sink += acc;
	return end - start;
}

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s [iters] [pin]\n"
		"  iters : number of loop iterations (default: 10000000)\n"
		"  pin	 : 1 to pin on CPU0, 0 otherwise (default: 1)\n",
		prog);
}

/*
 * Print a structured one-line result.
 *
 * This format is intentionally easy to parse later with awk, Python, or shell.
 */
static void print_result(const char *mode, size_t iters, int pin, uint64_t elapsed_ns) {
	double ns_per_iter = (double)elapsed_ns / (double)iters;
	printf("mode=%s iters=%zu pin=%d elapsed_ns=%llu ns_per_iter=%.4f sink=%ld\n",
		mode,
		iters,
		pin,
		(unsigned long long)elapsed_ns,
		ns_per_iter,
		(long)g_sink);
}

int main(int argc, char **argv) {
	size_t iters = 10000000ull;
	int pin = 1;

	if (argc >= 2) {
		char *end = NULL;
		unsigned long long v = strtoull(argv[1], &end, 10);
		if (!end || *end != '\0') {
			usage(argv[0]);
			return 1;
		}
		iters = (size_t)v;
	}

	if (argc >= 3) {
		pin = atoi(argv[2]);
		if (!(pin == 0 || pin ==1)) {
			usage(argv[0]);
			return 1;
		}
	}

	pin_to_cpu0_if_requested(pin);

	uint64_t t_empty	= bench_empty_loop(iters);
	uint64_t t_func		= bench_function_call(iters);
	uint64_t t_getpid	= bench_getpid_wrapper(iters);
	uint64_t t_syscall	= bench_direct_syscall_getpid(iters);

	print_result("empty_loop", iters, pin, t_empty);
	print_result("function_call", iters, pin, t_func);
	print_result("getpid_wrapper", iters, pin, t_getpid);
	print_result("direct_syscall_getpid", iters, pin, t_syscall);

	return 0;
}
