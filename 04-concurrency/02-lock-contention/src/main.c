#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	int thread_id;
	int cpu_count;
	int pin_cpu;

	uint64_t iters;
	uint64_t cs_work;
	uint64_t outside_work;

	pthread_mutex_t *lock;
	volatile uint64_t *shared_counter;

	/*
	 * Each thread maintains a local pseudo-random state so that the
	 * synthetic "work" loop cannot be trivially optimized away.
	 */
	uint64_t local_state;
} thread_arg_t;

/*
 * A global sink used to make sure the compiler cannot eliminate the
 * synthetic work loop as dead code.
 */
static volatile uint64_t g_sink = 0;

/*
 * Return monotonic time in nanoseconds.
 *
 * CLOCK_MONOTONIC is appropriate for elapsed-time measurement because it
 * is not affacted by wall-clock adjustments.
 */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Parse an unsigned 64-bit integer from a command-line string. */
static uint64_t parse_u64(const char *s, const char *name) {
	char *end = NULL;
	errno = 0;
	unsigned long long v = strtoull(s, &end, 10);

	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid value for %s: %s\n", name, s);
		exit(EXIT_FAILURE);
	}

	return (uint64_t)v;
}

/* Parse a regular integer from a command-line string. */
static int parse_int(const char *s, const char *name) {
	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);

	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid value for %s: %s\n", name, s);
		exit(EXIT_FAILURE);
	}

	return (int)v;
}

/*
 * Best-effort pinning of a thread to one CPU.
 *
 * We map thread_id to a CPU in round-robin fashion. The goal here is not
 * perfect NUMA/topology control, but to reduce scheduler noise and make the 
 * experiment more repeatable.
 */
static void maybe_pin_thread(int pin_cpu, int thread_id, int cpu_count) {
	if (!pin_cpu) {
		return;
	}

	if (cpu_count <= 0) {
		return;
	}

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);

	int cpu = thread_id % cpu_count;
	CPU_SET(cpu, &cpuset);

	int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if (rc != 0) {
		fprintf(stderr,
			"Warning: pthread_setaffinity_np failed for thread %d: %s\n",
			thread_id, strerror(rc));
	}
}

/*
 * Perform a configurable amount of synthetic CPU work.
 *
 * Why do this instead of just spinning on an empty loop?
 * - An empty loop may be optimized away.
 * - A simple arithmetic recurrence creates a small but real dependency chain.
 * - The function lets us independently control work inside and outside the lock.
 *
 * The return value is fed back into thread-local state to preserve the data
 * dependency and discourage the compiler from simplifying the computation.
 */
static uint64_t do_work(uint64_t iterations, uint64_t state) {
	for (uint64_t i = 0; i < iterations; i++) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		state += i + 0x9e3779b97f4a7c15ull;
	}

	/*
	 * Touch the global sink so the computation has an exteernally visible effect.
	 * This is intentionally very lightweight compared to the lock operations.
	 */
	g_sink ^= state;
	return state;
}

static void *worker_main(void *arg_) {
	thread_arg_t *arg = (thread_arg_t *)arg_;

	maybe_pin_thread(arg->pin_cpu, arg->thread_id, arg->cpu_count);

	uint64_t state = arg->local_state;

	for (uint64_t i = 0; i < arg->iters; i++) {
		/*
		 * Acquire the shared mutex. This is the focal point of the experiment:
		 * as more threads arrive here at similar times, contention grows.
		 */
		pthread_mutex_lock(arg->lock);

		/*
		 * Simulate work that must happen while holding the lock.
		 * Increasing cs_work lengthens lock hold time, which should amplify
		 * contention and reduce throughput.
		 */
		state = do_work(arg->cs_work, state);

		/* Update a shared data structure. A single counter is enough to create
		 * a centralized synchronization point fro the experiment.
		 */
		(*arg->shared_counter)++;

		pthread_mutex_unlock(arg->lock);

		/*
		 * Simulate work that happens outside the critical section.
		 * Increasing outside_work spreads out lock acquisition attempts and
		 * often reduces contention pressure.
		 */
		state = do_work(arg->outside_work, state);
	}

	arg->local_state = state;
	return NULL;
}

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s <threads> <iters_per_thread> <cs_work> <outside_work> <pin_cpu>\n"
		"\n"
		"  threads		Number of worker threads\n"
		"  iters_per_thread	Number of lock/unlock iterations per thread\n"
		"  cs_work 		Synthetic work iterations inside the critical section\n"
		"  outside_work 	Synthetic work iterations outside the critical section\n"
		"  pin_cput		0 = no pinning, 1 = pin threads round-robin to CPUs\n",
		prog);
}

int main(int argc, char **argv) {
	if (argc != 6) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	int threads = parse_int(argv[1], "threads");
	uint64_t iters_per_thread = parse_u64(argv[2], "iters_per_thread");
	uint64_t cs_work = parse_u64(argv[3], "cs_work");
	uint64_t outside_work = parse_u64(argv[4], "outside_work");
	int pin_cpu = parse_int(argv[5], "pin_cpu");

	if (threads <= 0) {
		fprintf(stderr, "threads must be > 0\n");
		return EXIT_FAILURE;
	}

	if (!(pin_cpu == 0 || pin_cpu == 1)) {
		fprintf(stderr, "pin_cpu must be 0 or 1\n");
		return EXIT_FAILURE;
	}

	int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (cpu_count <= 0) {
		cpu_count = 1;
	}

	pthread_t *tids = (pthread_t *)calloc((size_t)threads, sizeof(pthread_t));
	thread_arg_t *args = (thread_arg_t *)calloc((size_t)threads, sizeof(thread_arg_t));
	if (!tids || !args) {
		perror("calloc");
		free(tids);
		free(args);
		return EXIT_FAILURE;
	}

	pthread_mutex_t lock;
	pthread_mutex_init(&lock, NULL);

	volatile uint64_t shared_counter = 0;

	/*
	 * Create threads first, then start timing immediately before the create loop
	 * to include thread startup overhead in a consistent way across runs.
	 *
	 * Another valid design would be to use a barrier and measure only the steady
	 * state. For this lab, including startup overhead is acceptable because the
	 * iteration counts are large enough that contention behavior dominates.
	 */
	uint64_t start_ns = now_ns();

	for (int i = 0; i < threads; i++) {
		args[i].thread_id = i;
		args[i].cpu_count = cpu_count;
		args[i].pin_cpu = pin_cpu;
		args[i].iters = iters_per_thread;
		args[i].cs_work = cs_work;
		args[i].outside_work = outside_work;
		args[i].lock = &lock;
		args[i].shared_counter = &shared_counter;
		args[i].local_state = 0x12345678abcdefull ^ (uint64_t)(i + 1) * 0x9e3779b97f4a7c15ull;

		int rc = pthread_create(&tids[i], NULL, worker_main, &args[i]);
		if (rc != 0) {
			fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
			return EXIT_FAILURE;
		}
	}

	for (int i = 0; i < threads; i++) {
		int rc = pthread_join(tids[i], NULL);
		if (rc != 0) {
			fprintf(stderr, "pthread_join failed: %s\n", strerror(rc));
			return EXIT_FAILURE;
		}
	}

	uint64_t end_ns = now_ns();
	uint64_t elapsed_ns = end_ns - start_ns;

	uint64_t total_ops = (uint64_t)threads * iters_per_thread;
	double ns_per_op = (double)elapsed_ns / (double)total_ops;
	double mops_per_sec = ((double)total_ops / (double)elapsed_ns) * 1e3;

	/* CSV-friendly one-line output. */
	printf("threads=%d,iters_per_thread=%" PRIu64
		",cs_work=%" PRIu64
		",outside_work=%" PRIu64
		",pin_cpu=%d"
		",elapsed_ns=%" PRIu64
		",total_ops=%" PRIu64
		",ns_per_op=%.2f"
		",mops_per_sec=%.3f"
		",final_counter=%" PRIu64
		",sink=%" PRIu64 "\n",
		threads,
		iters_per_thread,
		cs_work,
		outside_work,
		pin_cpu,
		elapsed_ns,
		total_ops,
		ns_per_op,
		mops_per_sec,
		(uint64_t)shared_counter,
		(uint64_t)g_sink);

	pthread_mutex_destroy(&lock);
	free(tids);
	free(args);
	return EXIT_SUCCESS;
}
