#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * This lab compares two lock types:
 *   1. pthread_mutex_t
 *   2. pthread_spinlock_t
 *
 * The benchmark structure is intentionally simple:
 *   - N worker threads start together
 *   - each thread repeats:
 *   	outside_work()
 *   	lock()
 *   	critical_section_work()
 *   	shared_counter++
 *   	unlock()
 *
 * By tuning:
 *   - thread count
 *   - critical-section work
 *   - outside-lock work
 *
 * we can move between:
 *   - low contention
 *   - high contention
 *   - short critical section
 *   - long critical section
 *
 * The purpose is not to produce a single universal winner.
 * The purpose is to see how waiting policy interacts with
 * contention and critical-section length.
 */

typedef enum {
	LOCK_MUTEX = 0,
	LOCK_SPIN = 1,
} lock_mode_t;

typedef struct {
	pthread_mutex_t mutex;
	pthread_spinlock_t spin;
	lock_mode_t mode;
} bench_lock_t;

typedef struct {
	int thread_id;
	uint64_t iters;
	uint64_t cs_work;
	uint64_t outside_work;
	int pin_cpu_base;
	bench_lock_t *lock;
	pthread_barrier_t *start_barrier;
	volatile uint64_t *shared_counter;
} thread_arg_t;

/*
 * Prevent the compiler from optimizing away artificial work too aggressively.
 * 'sink' is volatile so the loop still has observable side effects.
 */
static volatile uint64_t sink = 0;

/*
 * Return monotonic time in nanoseconds.
 * CLOCK_MONOTONIC_RAW is preferred for low-level measurement because it avoids
 * some adjustments that may affect CLOCK_MONOTONIC on some systems.
 */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		perror("clock_gettime");
		exit(1);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Parse a positive integer from a string.
 * We use uint64_t for iteration counts and work counts so benchmark can scale.
 */
static uint64_t parse_u64(const char *s, const char *name) {
	errno = 0;
	char *end = NULL;
	unsigned long long v = strtoull(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid %s: %s\n", name, s);
		exit(1);
	}
	return (uint64_t)v;
}

/*
 * Parse a signed integer for options such as CPU pinning.
 * pin_cpu_base = -1 means "do not pin".
 */
static int parse_int(const char *s, const char *name) {
	errno = 0;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid %s: %s\n", name, s);
		exit(1);
	}
	return (int)v;
}

static const char *mode_name(lock_mode_t mode) {
	switch (mode) {
		case LOCK_MUTEX: return "mutex";
		case LOCK_SPIN:  return "spin";
		default :	 return "unknown";
	}
}

/*
 * A small configurable busy-work loop.
 *
 * This is not meant to model real work precisely.
 * It only creates a controllable amount of CPU work so that:
 *   - cs_work		controls critical-section length
 *   - outside_work	controls spacing between lock acquisitions
 *
 * That lets us shape contention intentionally.
 */
static inline void do_work(uint64_t work) {
	uint64_t local = sink;
	for (uint64_t i = 0; i < work; i++) {
		local = local * 1315423911u + (i ^ (local >> 3));
	}
	sink = local;
}

static void bench_lock_init(bench_lock_t *lock, lock_mode_t mode) {
	lock->mode = mode;

	if (pthread_mutex_init(&lock->mutex, NULL) != 0) {
		perror("pthread_mutex_init");
		exit(1);
	}

	if (pthread_spin_init(&lock->spin, PTHREAD_PROCESS_PRIVATE) != 0) {
		perror("pthread_spin_init");
		exit(1);
	}
}

static void bench_lock_destroy(bench_lock_t *lock) {
	pthread_mutex_destroy(&lock->mutex);
	pthread_spin_destroy(&lock->spin);
}

static inline void bench_lock_acquire(bench_lock_t *lock) {
	if (lock->mode == LOCK_MUTEX) {
		pthread_mutex_lock(&lock->mutex);
	} else {
		pthread_spin_lock(&lock->spin);
	}
}

static inline void bench_lock_release(bench_lock_t *lock) {
	if (lock->mode == LOCK_MUTEX) {
		pthread_mutex_unlock(&lock->mutex);
	} else {
		pthread_spin_unlock(&lock->spin);
	}
}

/*
 * Pin the current thread to one CPU, if requested.
 *
 * Why pinning matters:
 *   - It reduces run-to-run noise caused by scheduler movement.
 *   - It can make contention behavior easier to reason about.
 *
 * pin_cpu_base = -1 means "no pinning".
 * Otherwise thread i is pinned to CPU (pin_cpu_base + i) % num_cpus.
 */
static void maybe_pin_thread(int pin_cpu_base, int thread_id) {
	if (pin_cpu_base < 0) {
		return;
	}

	long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (num_cpus <= 0) {
		fprintf(stderr, "sysconf(_SC_NPROCESSORS_ONLN) failed\n");
		exit(1);
	}

	int cpu = (pin_cpu_base + thread_id) % (int)num_cpus;

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	if (rc != 0) {
		errno = rc;
		perror("pthread_setaffinity_np");
		exit(1);
	}
}

static void *worker_main(void *arg_) {
	thread_arg_t *arg = (thread_arg_t *)arg_;

	maybe_pin_thread(arg->pin_cpu_base, arg->thread_id);

	/*
	 * Barrier ensures that all threads begin their real work at roughly
	 * the same moment. This reduces start skew and makes elapsed time
	 * easier to interpret.
	 */
	pthread_barrier_wait(arg->start_barrier);

	for (uint64_t i = 0; i < arg->iters; i++) {
		do_work(arg->outside_work);

		bench_lock_acquire(arg->lock);

		/*
		 * Everything here is serialized by the lock.
		 * If cs_work is large, contention becomes more painful.
		 */
		do_work(arg->cs_work);
		(*arg->shared_counter)++;

		bench_lock_release(arg->lock);
	}

	return NULL;
}

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s <mode> <threads> <iters> <cs_work> <outside_work> [pin_cpu_base]\n"
		"\n"
		"  mode		: mutex | spin\n"
		"  threads	: number of worker threads\n"
		"  iters	: increments per threads\n"
		"  cs_work	: busy-work iterations inside the critical section\n"
		"  outside_work	: busy-work iterations outside the critical section\n"
		"  pin_cpu_base	: optional CPU pin base, -1 disables pinning (default: -1)\n"
		"\n"
		"Example:\n"
		"  %s mutex 4 1000000 0 0 0\n"
		"  %s spin  4 1000000 100 0 0\n",
		prog, prog, prog);
}

int main(int argc, char **argv) {
	if (argc < 6 || argc > 7) {
		usage(argv[0]);
		return 1;
	}

	lock_mode_t mode;
	if (strcmp(argv[1], "mutex") == 0) {
		mode = LOCK_MUTEX;
	} else if (strcmp(argv[1], "spin") == 0) {
		mode = LOCK_SPIN;
	} else {
		fprintf(stderr, "Invalid mode: %s\n", argv[1]);
		usage(argv[0]);
		return 1;
	}

	int threads = parse_int(argv[2], "threads");
	if (threads <= 0) {
		fprintf(stderr, "threds must be > 0\n");
		return 1;
	}

	uint64_t iters = parse_u64(argv[3], "iters");
	uint64_t cs_work = parse_u64(argv[4], "cs_work");
	uint64_t outside_work = parse_u64(argv[5], "outside_work");
	int pin_cpu_base = -1;

	if (argc == 7) {
		pin_cpu_base = parse_int(argv[6], "pin_cpu_base");
	}

	pthread_t *tids = calloc((size_t)threads, sizeof(*tids));
	thread_arg_t *args = calloc((size_t)threads, sizeof(*args));
	if (!tids || !args) {
		perror("calloc");
		return 1;
	}

	/*
	 * Shared counter is volatile to prevent the compiler from making
	 * unrealistic assumptions across threads in this simple benchmark.
	 *
	 * The lock is the real synchronization mechanism.
	 */
	volatile uint64_t shared_counter = 0;

	bench_lock_t lock;
	bench_lock_init(&lock, mode);

	pthread_barrier_t start_barrier;
	if (pthread_barrier_init(&start_barrier, NULL, (unsigned)threads +1) != 0) {
		perror("pthread_barrier_init");
		return 1;
	}

	for (int i = 0; i < threads; i++) {
		args[i].thread_id = i;
		args[i].iters = iters;
		args[i].cs_work = cs_work;
		args[i].outside_work = outside_work;
		args[i].pin_cpu_base = pin_cpu_base;
		args[i].lock = &lock;
		args[i].start_barrier = &start_barrier;
		args[i].shared_counter = &shared_counter;

		int rc = pthread_create(&tids[i], NULL, worker_main, &args[i]);
		if (rc != 0) {
			errno = rc;
			perror("pthread_create");
			return 1;
		}
	}

	/*
	 * Main thread joins the barrier too.
	 * This makes the timer start right before releasing all workers together.
	 */
	uint64_t t0 = now_ns();
	pthread_barrier_wait(&start_barrier);

	for (int i = 0; i < threads; i++) {
		int rc = pthread_join(tids[i], NULL);
		if (rc != 0) {
			errno = rc;
			perror("pthread_join");
			return 1;
		}
	}
	uint64_t t1 = now_ns();

	uint64_t elapsed_ns = t1 - t0;
	uint64_t expected = (uint64_t)threads * iters;
	double ns_per_op = (double)elapsed_ns / (double)expected;

	/*
	 * Output is CSV-friendly and machine-readalbe.
	 * That makes later plotting and analysis much easier.
	 */
	printf("%s,%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%d,%" PRIu64 ",%.2f,%" PRIu64 ",%" PRIu64 "\n",
		mode_name(mode),
		threads,
		iters,
		cs_work,
		outside_work,
		pin_cpu_base,
		elapsed_ns,
		ns_per_op,
		(uint64_t)shared_counter,
		expected);

	pthread_barrier_destroy(&start_barrier);
	bench_lock_destroy(&lock);
	free(tids);
	free(args);

	return 0;
}
