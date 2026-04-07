#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <unistd.h>
#endif

/*
 * Producer-Consumer Queue Lab
 *
 * This program benchmarks a bounded blocking queue implemented as:
 *   - ring buffer
 *   - pthread mutex
 *   - pthread condition variables (not_full / not_empty)
 *
 * Why this lab matters:
 *   - It demonstrates the classical bounded-buffer problem.
 *   - It shows how queue capacity affects throughput and backpressure.
 *   - It exposes producer-side full waits and consumer-side empty waits.
 *   - It bridges earlier labs:
 *   	mutex/spinlock -> lock contention -> ring buffer -> real queue behavior
 *
 * Example:
 *   ./artifacts/bin/producer_consumer_queue
 *   	--producers 2
 *   	--consumers 2
 *   	--items-per-producer 1000000
 *   	--capacity 256
 *   	--producer-work 0
 *   	--consumer-work 100
 *   	--warmup 1
 *   	--repeats 3
 *   	--pin-cpu 0
 */

typedef struct {
	uint64_t value;
} item_t;

typedef struct {
	item_t *buffer;
	size_t capacity;
	size_t head;
	size_t tail;
	size_t count;

	size_t max_observed_count;

	/*
	 * active_producers tracks how many producers have not finished yet.
	 * Consumers can terminate only when:
	 *   queue is empty AND active_prdocuers == 0
	 */
	int active_producers;

	pthread_mutex_t mutex;
	pthread_cond_t not_full;
	pthread_cond_t not_empty;
} queue_t;

typedef struct {
	uint64_t produced;
	uint64_t full_wait_count;
	uint64_t full_wait_ns;
	uint64_t checksum;
} producer_stats_t;

typedef struct {
	uint64_t consumed;
	uint64_t empty_wait_count;
	uint64_t empty_wait_ns;
	uint64_t checksum;
} consumer_stats_t;

typedef struct {
	int id;
	queue_t *queue;
	uint64_t items_to_produce;
	uint64_t producer_work;
	int pin_cpu;
	producer_stats_t stats;
} producer_arg_t;

typedef struct {
	int id;
	queue_t *queue;
	uint64_t consumer_work;
	int pin_cpu;
	consumer_stats_t stats;
} consumer_arg_t;

typedef struct {
	int producers;
	int consumers;
	uint64_t items_per_producer;
	size_t capacity;
	uint64_t producer_work;
	uint64_t consumer_work;
	int warmup;
	int repeats;
	int pin_cpu;
	int csv;
	int csv_header;
} options_t;

static uint64_t g_sink = 0;

/* ---------- Timing helpers ---------- */

static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ---------- CPU pinning helper ---------- */

static void pin_current_thread_if_requested(int requested_cpu, int logical_thread_index) {
#ifdef __linux__
	if (requested_cpu < 0) {
		return;
	}

	long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpu_count <= 0) {
		return;
	}

	int cpu = (requested_cpu + logical_thread_index) % (int)cpu_count;

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	if (rc != 0) {
		fprintf(stderr, "[warn] pthread_setaffinity_np failed for cpu=%d: %s\n",
			cpu, strerror(rc));
	}
#else
	(void)requested_cpu;
	(void)logical_thread_index;
#endif
}

/* ---------- Busy work helper ---------- */

/*
 * busy_work simulates compute performed outside the queue critical section.
 * This is important because producer/consumer imbalance is what makes queue
 * depth and backpressure visible.
 *
 * The volatile accumulator prevents the loop from being optimized away.
 */
static void busy_work(uint64_t iterations, uint64_t seed) {
	volatile uint64_t x = seed + 0x9e3779b97f4a7c15ULL;
	for (uint64_t i = 0; i < iterations; ++i) {
		x ^= (x << 7);
		x ^= (x >> 9);
		x += 0x9e3779b97f4a7c15ULL + i;
	}
	g_sink ^= x;
}

/* ---------- Queue implementation ---------- */

static void queue_init(queue_t *q, size_t capacity, int active_producers) {
	memset(q, 0, sizeof(*q));

	q->buffer = (item_t *)calloc(capacity, sizeof(item_t));
	if (!q->buffer) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	q->capacity = capacity;
	q->active_producers = active_producers;

	if (pthread_mutex_init(&q->mutex, NULL) != 0) {
		perror("pthread_mutex_init");
		exit(EXIT_FAILURE);
	}
	if (pthread_cond_init(&q->not_full, NULL) != 0) {
		perror("pthread_cond_init(not_full)");
		exit(EXIT_FAILURE);
	}
	if (pthread_cond_init(&q->not_empty, NULL) != 0) {
		perror("pthread_cond_init(not_empty)");
		exit(EXIT_FAILURE);
	}
}

static void queue_destroy(queue_t *q) {
	free(q->buffer);
	pthread_mutex_destroy(&q->mutex);
	pthread_cond_destroy(&q->not_full);
	pthread_cond_destroy(&q->not_empty);
}

/*
 * queue_push_blocking:
 *   - blocks while the queue is full
 *   - records how often and how long the producer had to wait
 */
static void queue_push_blocking(queue_t *q, item_t item, producer_stats_t *stats) {
	if (pthread_mutex_lock(&q->mutex) != 0) {
		perror("pthread_mutex_lock");
		exit(EXIT_FAILURE);
	}

	while (q->count == q->capacity) {
		uint64_t t0 = now_ns();
		stats->full_wait_count++;

		if (pthread_cond_wait(&q->not_full, &q->mutex) != 0) {
			perror("pthread_cond_wait(not_full)");
			exit(EXIT_FAILURE);
		}

		uint64_t t1 = now_ns();
		stats->full_wait_ns += (t1 - t0);
	}

	q->buffer[q->tail] = item;
	q->tail = (q->tail + 1) % q->capacity;
	q->count++;

	if (q->count > q->max_observed_count) {
		q->max_observed_count = q->count;
	}

	if (pthread_cond_signal(&q->not_empty) != 0) {
		perror("pthread_cond_signal(not_empty)");
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_unlock(&q->mutex) != 0) {
		perror("pthread_mutex_unlock");
		exit(EXIT_FAILURE);
	}
}

/*
 * queue_pop_blocking:
 *   - blocks while the queue is empty but producers are still active
 *   - stops when queue is empty and all producers have finished
 *   - returns false when there is no more work
 */
static bool queue_pop_blocking(queue_t *q, item_t *out, consumer_stats_t *stats) {
	if (pthread_mutex_lock(&q->mutex) != 0) {
		perror("pthread_mutex_lock");
		exit(EXIT_FAILURE);
	}

	while (q->count == 0 && q->active_producers > 0) {
		uint64_t t0 = now_ns();
		stats->empty_wait_count++;

		if (pthread_cond_wait(&q->not_empty, &q->mutex) != 0) {
			perror("pthread_cond_wait(not_empty)");
			exit(EXIT_FAILURE);
		}

		uint64_t t1 = now_ns();
		stats->empty_wait_ns += (t1 - t0);
	}

	if (q->count == 0 && q->active_producers == 0) {
		if (pthread_mutex_unlock(&q->mutex) != 0) {
			perror("pthread_mutex_unlock");
			exit(EXIT_FAILURE);
		}
		return false;
	}

	*out = q->buffer[q->head];
	q->head = (q->head + 1) % q->capacity;
	q->count--;

	if (pthread_cond_signal(&q->not_full) != 0) {
		perror("pthread_cond_signal(not_full)");
		exit(EXIT_FAILURE);
	}

	    if (pthread_mutex_unlock(&q->mutex) != 0) {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }

    return true;
}

/*
 * Called by a producer after it has enqueued all of its items.
 * We broadcast not_empty so consumers blocked on an empty queue can re-check
 * the "active_producers" termination condition.
 */
static void queue_producer_finished(queue_t *q) {
	if (pthread_mutex_lock(&q->mutex) != 0) {
		perror("pthread_mutex_lock");
		exit(EXIT_FAILURE);
	}

	q->active_producers--;

	if (pthread_cond_broadcast(&q->not_empty) != 0) {
		perror("pthread_cond_broadcast(not_empty)");
		exit(EXIT_FAILURE);
	}

	if (pthread_mutex_unlock(&q->mutex) != 0) {
		perror("pthread_mutex_unlock");
		exit(EXIT_FAILURE);
	}
}

/* ---------- Thread entry points ---------- */

static void *producer_main(void *arg_) {
	producer_arg_t *arg = (producer_arg_t *)arg_;

	pin_current_thread_if_requested(arg->pin_cpu, arg->id);

	/*
	 * Each producer generates a unique value stream so checksum is sensitive
	 * to lost/duplicated data.
	 */
	uint64_t base = ((uint64_t)arg->id << 56);

	for (uint64_t i = 0; i < arg->items_to_produce; ++i) {
		if (arg->producer_work > 0) {
			busy_work(arg->producer_work, base ^ i);
		}

		item_t item;
		item.value = base ^ (i + 1);

		queue_push_blocking(arg->queue, item, &arg->stats);

		arg->stats.produced++;
		arg->stats.checksum ^= item.value;
	}

	queue_producer_finished(arg->queue);
	return NULL;
}

static void *consumer_main(void *arg_) {
	consumer_arg_t *arg = (consumer_arg_t *)arg_;

	pin_current_thread_if_requested(arg->pin_cpu, 1000 + arg->id);

	item_t item;
	while (queue_pop_blocking(arg->queue, &item, &arg->stats)) {
		if (arg->consumer_work > 0) {
			busy_work(arg->consumer_work, item.value);
		}

		arg->stats.consumed++;
		arg->stats.checksum ^= item.value;
	}

	return NULL;
}

/* ------------------------------------------------------------
 * Option parsing
 * ------------------------------------------------------------ */

static void options_init(options_t *opt) {
    opt->producers = 1;
    opt->consumers = 1;
    opt->items_per_producer = 1000000ULL;
    opt->capacity = 256;
    opt->producer_work = 0;
    opt->consumer_work = 0;
    opt->warmup = 1;
    opt->repeats = 3;
    opt->pin_cpu = -1;
    opt->csv = 0;
    opt->csv_header = 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  --producers N            Number of producer threads (default: 1)\n"
            "  --consumers N            Number of consumer threads (default: 1)\n"
            "  --items-per-producer N   Items produced by each producer (default: 1000000)\n"
            "  --capacity N             Queue capacity (default: 256)\n"
            "  --producer-work N        Busy-work iterations per produced item (default: 0)\n"
            "  --consumer-work N        Busy-work iterations per consumed item (default: 0)\n"
            "  --warmup N               Warmup runs (default: 1)\n"
            "  --repeats N              Measured runs (default: 3)\n"
            "  --pin-cpu N              Pin threads starting from logical CPU N (default: -1 = disabled)\n"
            "  --csv                    Print CSV row only\n"
            "  --csv-header             Print CSV header only\n"
            "  --help                   Show this help\n",
            prog);
}

static uint64_t parse_u64(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (uint64_t)v;
}

static int parse_i32(const char *s, const char *name) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static void parse_options(options_t *opt, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--producers") == 0 && i + 1 < argc) {
            opt->producers = parse_i32(argv[++i], "--producers");
        } else if (strcmp(argv[i], "--consumers") == 0 && i + 1 < argc) {
            opt->consumers = parse_i32(argv[++i], "--consumers");
        } else if (strcmp(argv[i], "--items-per-producer") == 0 && i + 1 < argc) {
            opt->items_per_producer = parse_u64(argv[++i], "--items-per-producer");
        } else if (strcmp(argv[i], "--capacity") == 0 && i + 1 < argc) {
            opt->capacity = (size_t)parse_u64(argv[++i], "--capacity");
        } else if (strcmp(argv[i], "--producer-work") == 0 && i + 1 < argc) {
            opt->producer_work = parse_u64(argv[++i], "--producer-work");
        } else if (strcmp(argv[i], "--consumer-work") == 0 && i + 1 < argc) {
            opt->consumer_work = parse_u64(argv[++i], "--consumer-work");
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            opt->warmup = parse_i32(argv[++i], "--warmup");
        } else if (strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) {
            opt->repeats = parse_i32(argv[++i], "--repeats");
        } else if (strcmp(argv[i], "--pin-cpu") == 0 && i + 1 < argc) {
            opt->pin_cpu = parse_i32(argv[++i], "--pin-cpu");
        } else if (strcmp(argv[i], "--csv") == 0) {
            opt->csv = 1;
        } else if (strcmp(argv[i], "--csv-header") == 0) {
            opt->csv_header = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else {
            usage(argv[0]);
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }

    if (opt->producers <= 0 || opt->consumers <= 0) {
        fprintf(stderr, "producers and consumers must be > 0\n");
        exit(EXIT_FAILURE);
    }
    if (opt->capacity == 0) {
        fprintf(stderr, "capacity must be > 0\n");
        exit(EXIT_FAILURE);
    }
    if (opt->warmup < 0 || opt->repeats <= 0) {
        fprintf(stderr, "warmup must be >= 0 and repeats must be > 0\n");
        exit(EXIT_FAILURE);
    }
}

static void print_csv_header(void) {
    printf("producers,consumers,items_per_producer,total_items,capacity,"
           "producer_work,consumer_work,pin_cpu,"
           "elapsed_ns,throughput_ops_per_sec,ns_per_item,"
           "produced,consumed,"
           "producer_full_wait_count,producer_full_wait_ns,"
           "consumer_empty_wait_count,consumer_empty_wait_ns,"
           "max_observed_queue_depth,checksum\n");
}

/* ---------- Single run ---------- */

typedef struct {
	uint64_t elapsed_ns;
	double throughput_ops_per_sec;
	double ns_per_item;

	uint64_t total_produced;
	uint64_t total_consumed;
	
	uint64_t producer_full_wait_count;
	uint64_t producer_full_wait_ns;

	uint64_t consumer_empty_wait_count;
	uint64_t consumer_empty_wait_ns;

	size_t max_observed_queue_depth;
	uint64_t checksum;
} run_result_t;

static run_result_t run_once(const options_t *opt) {
	queue_t queue;
	queue_init(&queue, opt->capacity, opt->producers);

	pthread_t *producer_threads = (pthread_t *)calloc((size_t)opt->producers, sizeof(pthread_t));
	pthread_t *consumer_threads = (pthread_t *)calloc((size_t)opt->consumers, sizeof(pthread_t));
	producer_arg_t *producer_args = (producer_arg_t *)calloc((size_t)opt->producers, sizeof(producer_arg_t));
	consumer_arg_t *consumer_args = (consumer_arg_t *)calloc((size_t)opt->consumers, sizeof(consumer_arg_t));

	if (!producer_threads || !consumer_threads || !producer_args || !consumer_args) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	uint64_t t0 = now_ns();

	for (int i = 0; i < opt->consumers; ++i) {
		consumer_args[i].id = i;
		consumer_args[i].queue = &queue;
		consumer_args[i].consumer_work = opt->consumer_work;
		consumer_args[i].pin_cpu = opt->pin_cpu;

		if (pthread_create(&consumer_threads[i], NULL, consumer_main, &consumer_args[i]) != 0) {
				perror("pthread_create(consumer)");
				exit(EXIT_FAILURE);
		}
	}

	for (int i = 0; i < opt->producers; ++i) {
		producer_args[i].id = i;
		producer_args[i].queue = &queue;
		producer_args[i].items_to_produce  = opt->items_per_producer;
		producer_args[i].producer_work = opt->producer_work;
		producer_args[i].pin_cpu = opt->pin_cpu;
		
		if (pthread_create(&producer_threads[i], NULL, producer_main, &producer_args[i]) != 0) {
			perror("pthread_create(producer)");
			exit(EXIT_FAILURE);
		}
	}

	for (int i = 0; i < opt->producers; ++i) {
		if (pthread_join(producer_threads[i], NULL) != 0) {
			perror("pthread_join(producer)");
			exit(EXIT_FAILURE);
		}
	}

	for (int i = 0; i < opt->consumers; ++i) {
		if (pthread_join(consumer_threads[i], NULL) != 0) {
			perror("pthread_join(consumer)");
			exit(EXIT_FAILURE);
		}
	}

	uint64_t t1 = now_ns();

	run_result_t r;
	memset(&r, 0, sizeof(r));
	r.elapsed_ns = t1 - t0;
	r.max_observed_queue_depth = queue.max_observed_count;

	for (int i = 0; i < opt->producers; ++i) {
		r.total_produced += producer_args[i].stats.produced;
		r.producer_full_wait_count += producer_args[i].stats.full_wait_count;
		r.producer_full_wait_ns += producer_args[i].stats.full_wait_ns;
		r.checksum ^= producer_args[i].stats.checksum;
	}

	for (int i = 0; i < opt->consumers; ++i) {
		r.total_consumed += consumer_args[i].stats.consumed;
		r.consumer_empty_wait_count += consumer_args[i].stats.empty_wait_count;
		r.consumer_empty_wait_ns += consumer_args[i].stats.empty_wait_ns;
		r.checksum ^= consumer_args[i].stats.checksum;
	}

	uint64_t total_items = (uint64_t)opt->producers * opt->items_per_producer;
	r.ns_per_item = (double)r.elapsed_ns / (double)total_items;
	r.throughput_ops_per_sec = (double)total_items * 1e9 / (double)r.elapsed_ns;

	free(producer_threads);
	free(consumer_threads);
	free(producer_args);
	free(consumer_args);
	queue_destroy(&queue);

	return r;
}

static void print_human_summary(const options_t *opt, const run_result_t *best, double avg_ns_per_item) {
    uint64_t total_items = (uint64_t)opt->producers * opt->items_per_producer;

    printf("[configuration]\n");
    printf("  producers          = %d\n", opt->producers);
    printf("  consumers          = %d\n", opt->consumers);
    printf("  items_per_producer = %" PRIu64 "\n", opt->items_per_producer);
    printf("  total_items        = %" PRIu64 "\n", total_items);
    printf("  capacity           = %zu\n", opt->capacity);
    printf("  producer_work      = %" PRIu64 "\n", opt->producer_work);
    printf("  consumer_work      = %" PRIu64 "\n", opt->consumer_work);
    printf("  pin_cpu            = %d\n", opt->pin_cpu);
    printf("\n");

    printf("[best run]\n");
    printf("  elapsed_ns                 = %" PRIu64 "\n", best->elapsed_ns);
    printf("  throughput_ops_per_sec     = %.2f\n", best->throughput_ops_per_sec);
    printf("  ns_per_item                = %.2f\n", best->ns_per_item);
    printf("  avg_ns_per_item            = %.2f\n", avg_ns_per_item);
    printf("  produced                   = %" PRIu64 "\n", best->total_produced);
    printf("  consumed                   = %" PRIu64 "\n", best->total_consumed);
    printf("  producer_full_wait_count   = %" PRIu64 "\n", best->producer_full_wait_count);
    printf("  producer_full_wait_ns      = %" PRIu64 "\n", best->producer_full_wait_ns);
    printf("  consumer_empty_wait_count  = %" PRIu64 "\n", best->consumer_empty_wait_count);
    printf("  consumer_empty_wait_ns     = %" PRIu64 "\n", best->consumer_empty_wait_ns);
    printf("  max_observed_queue_depth   = %zu\n", best->max_observed_queue_depth);
    printf("  checksum                   = %" PRIu64 "\n", best->checksum);
    printf("  sink                       = %" PRIu64 "\n", g_sink);
}

static void print_csv_row(const options_t *opt, const run_result_t *r) {
    uint64_t total_items = (uint64_t)opt->producers * opt->items_per_producer;

    printf("%d,%d,%" PRIu64 ",%" PRIu64 ",%zu,"
           "%" PRIu64 ",%" PRIu64 ",%d,"
           "%" PRIu64 ",%.2f,%.4f,"
           "%" PRIu64 ",%" PRIu64 ","
           "%" PRIu64 ",%" PRIu64 ","
           "%" PRIu64 ",%" PRIu64 ","
           "%zu,%" PRIu64 "\n",
           opt->producers,
           opt->consumers,
           opt->items_per_producer,
           total_items,
           opt->capacity,
           opt->producer_work,
           opt->consumer_work,
           opt->pin_cpu,
           r->elapsed_ns,
           r->throughput_ops_per_sec,
           r->ns_per_item,
           r->total_produced,
           r->total_consumed,
           r->producer_full_wait_count,
           r->producer_full_wait_ns,
           r->consumer_empty_wait_count,
           r->consumer_empty_wait_ns,
           r->max_observed_queue_depth,
           r->checksum);
}

int main(int argc, char **argv) {
	options_t opt;
	options_init(&opt);
	parse_options(&opt, argc, argv);

	if (opt.csv_header) {
		print_csv_header();
		return 0;
	}

	for (int i = 0; i < opt.warmup; ++i) {
		(void)run_once(&opt);
	}

	run_result_t best;
	memset(&best, 0, sizeof(best));
	double sum_ns_per_item = 0.0;

	for (int i = 0; i < opt.repeats; ++i) {
		run_result_t r = run_once(&opt);
		sum_ns_per_item += r.ns_per_item;

		if (i == 0 || r.ns_per_item < best.ns_per_item) {
			best = r;
		}
	}

	double avg_ns_per_item = sum_ns_per_item / (double)opt.repeats;

	if (opt.csv) {
		print_csv_row(&opt, &best);
	} else {
		print_human_summary(&opt, &best, avg_ns_per_item);
	}

	return 0;
}
