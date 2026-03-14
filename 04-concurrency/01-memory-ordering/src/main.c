#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * This lab demonstrates memory ordering with a classic
 * Store Buffering (SB) litmus test.
 *
 * Two threads run the following operations concurrently:
 *
 *   Thread 0: x = 1; r0 = y;
 *   Thread 1: y = 1; r1 = x;
 *
 * Under weak enough ordering, the outcome
 *
 *    r == 0 && r1 == 0
 *
 * is possible, even though each thread performed a store
 * before its load in program order.
 *
 * Why:
 * - Modern CPUs use store buffers and out-of-order execution.
 * - A store may not become visible to another core immediately.
 * - A later load can observe stale data from the other variable.
 *
 * This program runs the SB test many times and counts outcomes
 * under three C11 atomic modes:
 *
 *   relaxed : store/load use memory_order_relaxed
 *   acqrel  : store/ uses memory_order_release,
 *   	       load uses memory_order_acquire
 *   seqcst  : store/load use memory_order_seq_cst
 *
 * Important note:
 * - "acquire/release" does NOT magically fix everything.
 * - It only creates synchronization when paired through the
 *   same atomic communication pattern.
 * - In this SB test, acquire/release on separate atomics x/y
 *   usually does NOT forbid the both-zero outcome.
 *
 * Expected behavior:
 * - relaxed : both-zero may appear
 * - acqrel  : both-zero may still appear
 * - seqcst  : both-zero should be forbidden by the C11 SC model
 *
 * On x86:
 * - relaxed and acqrel often compile to similar machine code
 *   because x86 TSO is already relatively strong.
 * - However, store->load reordering through the store buffer
 *   is still observable, so both-zero can appear.
 *
 * On ARM:
 * - weak behaviors are often easier to observe.
 */

typedef enum {
	MODE_RELAXED = 0,
	MODE_ACQREL,
	MODE_SEQCST
} my_mode_t;

typedef struct {
	atomic_int x;
	atomic_int y;

	atomic_int r0;
	atomic_int r1;

	pthread_barrier_t start_barrier;
	pthread_barrier_t end_barrier;

	my_mode_t mode;
	long iterations;
	long warmup;
	int cpu0;
	int cpu1;
} shared_t;

typedef struct {
	shared_t *shared;
	int tid;
	int cpu;
} worker_arg_t;

static void die(const char *msg) {
	perror(msg);
	exit(EXIT_FAILURE);
}

static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		die("clock_gettime");
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_thread_to_cpu(int cpu) {
	if (cpu < 0) {
		return;
	}

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
	if (rc != 0) {
		errno = rc;
		die("pthread_setaffinity_np");
	}
}

static const char *mode_name(my_mode_t mode) {
	switch (mode) {
		case MODE_RELAXED: return "relaxed";
		case MODE_ACQREL:  return "acqrel";
		case MODE_SEQCST:  return "seqcst";
		default: 	   return "unknown";
	}
}

static my_mode_t parse_mode(const char *s) {
	if (strcmp(s, "relaxed") == 0) return MODE_RELAXED;
	if (strcmp(s, "acqrel")  == 0) return MODE_ACQREL;
	if (strcmp(s, "seqcst")  == 0) return MODE_SEQCST;

	fprintf(stderr, "Unknown mode: %s\n", s);
	fprintf(stderr, "Valid modes: relaxed, acqrel, seqcst\n");
	exit(EXIT_FAILURE);
}

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage, %s [--mode relaxed|acqrel|seqcst] [--iters N] [--warmup N]"
		"[--cpu0 N] [--cpu1 N]\n",
		prog
	       );
}

static void thread0_op(shared_t *s) {
	int v;

	switch (s->mode) {
		case MODE_RELAXED:
			atomic_store_explicit(&s->x, 1, memory_order_relaxed);
			v = atomic_load_explicit(&s->y, memory_order_relaxed);
			break;

		case MODE_ACQREL:
			atomic_store_explicit(&s->x, 1, memory_order_release);
			v = atomic_load_explicit(&s->y, memory_order_acquire);
			break;

		case MODE_SEQCST:
			atomic_store_explicit(&s->x, 1, memory_order_seq_cst);
			v = atomic_load_explicit(&s->y, memory_order_seq_cst);
			break;

		default:
			fprintf(stderr, "Invalid mode\n");
			exit(EXIT_FAILURE);
	}

	atomic_store_explicit(&s->r0, v, memory_order_relaxed);
}

static void thread1_op(shared_t *s) {
	int v;

	switch (s->mode) {
		case MODE_RELAXED:
			atomic_store_explicit(&s->y, 1, memory_order_relaxed);
			v = atomic_load_explicit(&s->x, memory_order_relaxed);
			break;

		case MODE_ACQREL:
			atomic_store_explicit(&s->y, 1, memory_order_release);
			v = atomic_load_explicit(&s->x, memory_order_acquire);
			break;

		case MODE_SEQCST:
			atomic_store_explicit(&s->y, 1, memory_order_seq_cst);
			v = atomic_load_explicit(&s->x, memory_order_seq_cst);
			break;

		default:
			fprintf(stderr, "Invalid mode\n");
			exit(EXIT_FAILURE);
	}

	atomic_store_explicit(&s->r1, v, memory_order_relaxed);
}

static void *worker_main(void *arg_) {
	worker_arg_t *arg = (worker_arg_t *)arg_;
	shared_t *s = arg->shared;

	pin_thread_to_cpu(arg->cpu);

	long total = s->warmup +s->iterations;
	for (long i = 0; i < total; i++) {
		/*
		 * Wait until the main thread resets x/y and releases both workers
		 * into the current trial.
		 */
		pthread_barrier_wait(&s->start_barrier);

		if (arg->tid == 0) {
			thread0_op(s);
		} else {
			thread1_op(s);
		}

		/*
		 * Signal that this thread finished the current trial.
		 * The main thread will read r0/r1 only after all parties
		 * pass this barrier.
		 */
		pthread_barrier_wait(&s->end_barrier);
	}

	return NULL;
}

int main(int argc, char **argv) {
	shared_t s;
	memset(&s, 0, sizeof(s));

	s.mode = MODE_RELAXED;
	s.iterations = 200000;
	s.warmup = 5000;
	s.cpu0 = 0;
	s.cpu1 = 1;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0) {
			if (++i >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			s.mode = parse_mode(argv[i]);
		} else if (strcmp(argv[i], "--iters") == 0) {
			if (++i >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			s.iterations = atol(argv[i]);
		} else if (strcmp(argv[i], "--warmup") == 0) {
			if (++i >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			s.warmup = atol(argv[i]);
		} else if (strcmp(argv[i], "--cpu0") == 0) {
			if (++i >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			s.cpu0 = atoi(argv[i]);
		} else if (strcmp(argv[i], "--cpu1") == 0) {
			if (++i >= argc) {
				usage(argv[0]);
				return EXIT_FAILURE;
			}
			s.cpu1 = atoi(argv[i]);
		} else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* Initialize atomics. */
	atomic_init(&s.x, 0);
	atomic_init(&s.y, 0);
	atomic_init(&s.r0, -1);
	atomic_init(&s.r1, -1);

	/*
	 * We use a 3-party barrier:
	 *   - worker thread 0
	 *   - worker thread 1
	 *   - main thread
	 *
	 * start_barrier synchronizes the start of each trial.
	 * end_barrier synchronizes the end of each trial.
	 */
	if (pthread_barrier_init(&s.start_barrier, NULL, 3) != 0) {
		die("pthread_barrier_init(start_barrier)");
	}
	if (pthread_barrier_init(&s.end_barrier, NULL, 3) != 0) {
		die("pthread_barrier_init(end_barrier)");
	}

	pthread_t th0, th1;
	worker_arg_t a0 = { .shared = &s, .tid = 0, .cpu = s.cpu0 };
	worker_arg_t a1 = { .shared = &s, .tid = 1, .cpu = s.cpu1 };

	if (pthread_create(&th0, NULL, worker_main, &a0) != 0) {
		die("pthread_create(th0)");
	}
	if (pthread_create(&th1, NULL, worker_main, &a1) != 0) {
		die("pthread_create(th1)");
	}

	uint64_t start_ns = 0;
	uint64_t end_ns = 0;

	long count_00 = 0;
	long count_01 = 0;
	long count_10 = 0;
	long count_11 = 0;

	long total = s.warmup + s.iterations;
	for (long i = 0; i < total; i++) {
		/*
		 * Reset the shared locations before each trial.
		 * Relaxed is enough here because the barriers structure the trial.
		 */
		atomic_store_explicit(&s.x, 0, memory_order_relaxed);
		atomic_store_explicit(&s.y, 0, memory_order_relaxed);
		atomic_store_explicit(&s.r0, -1, memory_order_relaxed);
		atomic_store_explicit(&s.r1, -1, memory_order_relaxed);

		if (i == s.warmup) {
			start_ns = now_ns();
		}

		/* Release both worker threads into the current trial. */
		pthread_barrier_wait(&s.start_barrier);

		/* Wait until both workers complete the current trial. */
		pthread_barrier_wait(&s.end_barrier);

		if (i >= s.warmup) {
			int r0 = atomic_load_explicit(&s.r0, memory_order_relaxed);
			int r1 = atomic_load_explicit(&s.r1, memory_order_relaxed);

			if (r0 == 0 && r1 == 0) count_00++;
			else if (r0 == 0 && r1 == 1) count_01++;
			else if (r0 == 1 && r1 == 0) count_10++;
			else if (r0 == 1 && r1 == 1) count_11++;
			else {
				/* Any other value indicates a bug in the harness. */
				fprintf(stderr, "Unexpected outcome: r0=%d r1=%d\n", r0, r1);
				return EXIT_FAILURE;
			}
		}
	}

	end_ns = now_ns();

	if (pthread_join(th0, NULL) != 0) {
		die("pthread_join(th0)");
	}
	if (pthread_join(th1, NULL) != 0) {
		die("pthread_join(th1)");
	}

	pthread_barrier_destroy(&s.start_barrier);
	pthread_barrier_destroy(&s.end_barrier);

	uint64_t elapsed_ns = end_ns - start_ns;
	double ns_per_trial = (double)elapsed_ns / (double)s.iterations;

	/* CSV-friendly output. */
	printf("%s,%ld,%ld,%d,%d,%llu,%.2f,%ld,%ld,%ld,%ld\n",
		mode_name(s.mode),
		s.iterations,
		s.warmup,
		s.cpu0,
		s.cpu1,
		(unsigned long long)elapsed_ns,
		ns_per_trial,
		count_00,
		count_01,
		count_10,
		count_11);

	return 0;
}
	
