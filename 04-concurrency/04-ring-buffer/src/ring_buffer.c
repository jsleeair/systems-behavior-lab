#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/*
 * ring_buffer.c
 *
 * A benchmark for a single-producer / single-consumer (SPSC) ring buffer.
 *
 * This lab is meant to show:
 *   1. Why a ring buffer is a practical low-latency queue structure.
 *   2. How acquire/release memory ordering is enough for SPSC correctness.
 *   3. How cache-line placement of producer/consumer indices affects throughput.
 *
 * We benchmark two layouts:
 *   - packed: head and tail indices live close together
 *   - padded: head and tail indices are separated to reduced false sharing
 *
 *   Output is written as CSV so that later we can plot:
 *     - ns/message
 *     - million messages / second
 *     - packed vs padded across capacities
 *
 * Build:
 *   gcc -O2 -march=native -Wall -Wextra -std=c11 -pthread
 *   	 src/ring_buffer.c -o artifacts/bin/ring_buffer
 *
 * Example:
 *   ./artifacts/bin/ring_buffer --mode packed --capacity 1024
 *   	  --messages 20000000 --repeats 5 --warmup 1 --producer -cpu 0 --consumer-cpu 1
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

typedef enum {
	MODE_PACKED = 0,
	MODE_PADDED =1
} bench_mode_t;

typedef struct {
	bench_mode_t mode;
	uint64_t capacity;
	uint64_t mask;
	uint64_t messages;
	int producer_cpu;
	int consumer_cpu;
} config_t;

/*
 * A compact layout:
 * head and tail are placed adjacent to each other.
 *
 * This is intentionally "worse" from a chace-coherence perspective because
 * producer writes tail, consumer writes head, and both may invalidate the 
 * same cache line repeatedly.
 */
typedef struct {
	_Atomic uint64_t head;
	_Atomic uint64_t tail;
	_Atomic uint64_t full_hits;
	uint64_t *buffer;
	uint64_t mask;
	uint64_t capacity;
} ring_packed_t;

/*
 * A padded layout:
 * head and tail are separated so producer and consumer do not keep fighting
 * over the same cache line as much.
 *
 * This is a classic trick in concurrent queue design.
 */
typedef struct {
	_Alignas(CACHELINE_SIZE) _Atomic uint64_t head;
	char pad1[CACHELINE_SIZE - sizeof(_Atomic uint64_t)];

	_Alignas(CACHELINE_SIZE) _Atomic uint64_t tail;
	char pad2[CACHELINE_SIZE - sizeof(_Atomic uint64_t)];

	_Alignas(CACHELINE_SIZE) _Atomic uint64_t full_hits;
    	char pad3[CACHELINE_SIZE];

	uint64_t *buffer;
	uint64_t mask;
	uint64_t capacity;
} ring_padded_t;

typedef struct {
	const config_t *cfg;
	void *ring;
} thread_ctx_t;

typedef struct {
	uint64_t checksum;
} consumer_result_t;

static volatile uint64_t global_sink = 0;

/* ---------- Utility helpers ----------- */

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

static bool is_power_of_two_u64(uint64_t x) {
	return x != 0 && (x & (x - 1)) == 0;
}

/*
 * A tiny CPU-relax hint.
 *
 * On x86, PAUSE reduces the penalty of spin-wait loops.
 * On other platforms we simply keep the loop empty.
 */
static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__("pause" ::: "memory");
#else
	__asm__ __volatile__("" ::: "memory");
#endif
}

/*
 * Best-effort thread pinning for Linux.
 * If pin_cpu < 0, we skip pinning.
 *
 * Pinning is useful here because it reduces run-to-run noise and makes
 * producer/consumer placement more explicit.
 */
static void maybe_pin_thread_to_cpu(int pin_cpu) {
#ifdef __linux__
	if (pin_cpu < 0) {
		return;
	}

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(pin_cpu, &set);

	int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
	if (rc != 0) {
		errno = rc;
		die("pthread_setaffinity_np");
	}
#else
	(void)pin_cpu;
#endif
}

/* ---------- Ring buffer allocation ---------- */

static ring_packed_t *ring_packed_create(uint64_t capacity) {
	ring_packed_t *rb = (ring_packed_t *)calloc(1, sizeof(*rb));
	if (!rb) {
		die("calloc(ring_packed)");
	}

	rb->buffer = (uint64_t *)aligned_alloc(CACHELINE_SIZE, capacity * sizeof(uint64_t));
	if (!rb->buffer) {
		die("aligned_alloc(buffer)");
	}

	memset(rb->buffer, 0, capacity * sizeof(uint64_t));
	atomic_init(&rb->head, 0);
	atomic_init(&rb->tail, 0);
	atomic_init(&rb->full_hits, 0);
	rb->capacity = capacity;
	rb->mask = capacity - 1;
	return rb;
}

static ring_padded_t *ring_padded_create(uint64_t capacity) {
	ring_padded_t *rb = (ring_padded_t *)calloc(1, sizeof(*rb));
	if (!rb) {
		die("calloc(ring_padded)");
	}

	rb->buffer = (uint64_t *)aligned_alloc(CACHELINE_SIZE, capacity * sizeof(uint64_t));
	if (!rb->buffer) {
		die("aligned_alloc(buffer)");
	}

	memset(rb->buffer, 0, capacity * sizeof(uint64_t));
	atomic_init(&rb->head, 0);
	atomic_init(&rb->tail, 0);
	atomic_init(&rb->full_hits, 0);
	rb->capacity = capacity;
	rb->mask = capacity -1;
	return rb;
}

static void ring_packed_destroy(ring_packed_t *rb) {
	if (!rb) return;
	free(rb->buffer);
	free(rb);
}

static void ring_padded_destroy(ring_padded_t *rb) {
	if (!rb) return;
	free(rb->buffer);
	free(rb);
}

/* ---------- Producer / consuer operations ---------- */

/*
 * SPSC enqueue for the packed layout.
 *
 * Memory ordering explanation:
 *    - Producer reads head with acquire semantics so it observes consumer progress.
 *    - Producer writes payload into the buffer first.
 *    - Producer then publishes new tail with release semantics.
 *
 * Consumer will read tail with acquire semantics before reading the element,
 * so the payload write becomes visible before the consumer uses it.
 */

static inline void ring_packed_push(ring_packed_t *rb, uint64_t value) {
	uint64_t tail;
	uint64_t head;

	for(;;) {
		tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
		head = atomic_load_explicit(&rb->head, memory_order_acquire);

		if((tail - head) < rb->capacity) {
			break; // queue is not full
		}

		atomic_fetch_add_explicit(&rb->full_hits, 1, memory_order_relaxed);
		cpu_relax();
	}

	rb->buffer[tail & rb->mask] = value;
	atomic_store_explicit(&rb->tail, tail + 1, memory_order_release);
}

static inline uint64_t ring_packed_pop(ring_packed_t *rb) {
	uint64_t head;
	uint64_t tail;

	for (;;) {
		head = atomic_load_explicit(&rb->head, memory_order_relaxed);
		tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

		if (head != tail) {
			break; // queue is not empty
		}

		cpu_relax();
	}

	uint64_t value = rb->buffer[head & rb->mask];
	atomic_store_explicit(&rb->head, head + 1, memory_order_release);
	return value;
}

static inline void ring_padded_push(ring_padded_t *rb, uint64_t value) {
	uint64_t tail;
	uint64_t head;

	for (;;) {
		tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
		head = atomic_load_explicit(&rb->head, memory_order_acquire);

		if ((tail - head) < rb->capacity) {
			break;
		}

		atomic_fetch_add_explicit(&rb->full_hits, 1, memory_order_relaxed);
		cpu_relax();
	}

	rb->buffer[tail & rb->mask] = value;
	atomic_store_explicit(&rb->tail, tail + 1, memory_order_release);
}

static inline uint64_t ring_padded_pop(ring_padded_t *rb) {
	uint64_t head;
	uint64_t tail;

	for (;;) {
		head = atomic_load_explicit(&rb->head, memory_order_relaxed);
		tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

		if (head != tail) {
			break;
		}

		cpu_relax();
	}

	uint64_t value = rb->buffer[head & rb->mask];
	atomic_store_explicit(&rb->head, head + 1, memory_order_release);
	return value;
}

/* ---------- Benchmark threads ---------- */

static void *producer_main(void *arg) {
	thread_ctx_t *ctx = (thread_ctx_t *)arg;
	const config_t *cfg = ctx->cfg;

	maybe_pin_thread_to_cpu(cfg->producer_cpu);

	if (cfg->mode == MODE_PACKED) {
		ring_packed_t *rb = (ring_packed_t *)ctx->ring;
		for (uint64_t i = 0; i < cfg->messages; ++i) {
			ring_packed_push(rb, i + 1);
		//	for (int j = 0; j < 20; j++) cpu_relax();
		}
	} else {
		ring_padded_t *rb = (ring_padded_t *)ctx->ring;
		for (uint64_t i = 0; i < cfg->messages; ++i) {
			ring_padded_push(rb, i + 1);
		//	for (int j = 0; j < 20; j++) cpu_relax();
		}
	}

	return NULL;
}

static void *consumer_main(void *arg) {
	thread_ctx_t *ctx = (thread_ctx_t *)arg;
	const config_t *cfg = ctx->cfg;

	maybe_pin_thread_to_cpu(cfg->consumer_cpu);

	consumer_result_t *result = (consumer_result_t *)calloc(1, sizeof(*result));
	if (!result) {
		die("calloc(consumer_result)");
	}

	uint64_t checksum = 0;

	if (cfg->mode == MODE_PACKED) {
		ring_packed_t *rb = (ring_packed_t *)ctx->ring;
		for (uint64_t i = 0; i < cfg->messages; ++i) {
			checksum += ring_packed_pop(rb);

			if ((i % 256) == 0) {
    				for (int j = 0; j < 200; ++j) cpu_relax();
			}
		}
	} else {
		ring_padded_t *rb = (ring_padded_t *)ctx->ring;
		for (uint64_t i = 0; i < cfg->messages; ++i) {
			checksum += ring_padded_pop(rb);
			if ((i % 256) == 0) {
    				for (int j = 0; j < 200; ++j) cpu_relax();
}			
		}
	}

	result->checksum = checksum;
	return result;
}

/* ----------- CLI parsing ---------- */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --mode packed|padded       Ring layout mode (default: padded)\n"
        "  --capacity N               Queue capacity, must be power of two (default: 1024)\n"
        "  --messages N               Number of messages to pass (default: 10000000)\n"
        "  --repeats N                Number of measured runs (default: 5)\n"
        "  --warmup N                 Number of warmup runs (default: 1)\n"
        "  --producer-cpu N           Pin producer thread to CPU N, -1 disables pinning (default: -1)\n"
        "  --consumer-cpu N           Pin consumer thread to CPU N, -1 disables pinning (default: -1)\n"
        "  --csv PATH                 Output CSV path (default: artifacts/data/ring_buffer.csv)\n"
        "  --help                     Show this help\n",
        prog);
}

static bench_mode_t parse_mode(const char *s) {
	if (strcmp(s, "packed") == 0) return MODE_PACKED;
	if (strcmp(s, "padded") == 0) return MODE_PADDED;

	fprintf(stderr, "Unknown mode: %s\n", s);
	exit(EXIT_FAILURE);
}

static uint64_t parse_u64(const char *s, const char *what) {
	char *end = NULL;
	errno = 0;
	unsigned long long v = strtoull(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid %s: %s\n", what, s);
		exit(EXIT_FAILURE);
	}
	return (uint64_t)v;
}

static int parse_int(const char *s, const char *what) {
	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "Invalid %s: %s\n", what, s);
		exit(EXIT_FAILURE);
	}
	return (int)v;
}

/* ---------- CSV helpers ---------- */

static FILE *open_csv_append_with_header(const char *path) {
	bool need_header = false;

	FILE *check = fopen(path, "r");
	if (!check) {
		need_header = true;
	} else {
		fclose(check);
	}

	FILE *fp = fopen(path, "a");
	if (!fp) {
		die("fopen(csv)");
	}

	if (need_header) {
		fprintf(fp,
			"mode,capacity,messages,producer_cpu,consumer_cpu,"
			"elapsed_ns,ns_per_msg,mmsgs_per_sec,checksum,full_hits\n");
		fflush(fp);
	}

	return fp;
}

/* ---------- Benchmark driver ---------- */

typedef struct {
	uint64_t elapsed_ns;
	uint64_t checksum;
	uint64_t full_hits;
} one_run_result_t;

static one_run_result_t run_once(const config_t *cfg) {
    pthread_t producer;
    pthread_t consumer;
    thread_ctx_t producer_ctx;
    thread_ctx_t consumer_ctx;
    void *consumer_ret = NULL;

    uint64_t start_ns;
    uint64_t end_ns;

    if (cfg->mode == MODE_PACKED) {
        ring_packed_t *rb = ring_packed_create(cfg->capacity);

        producer_ctx.cfg = cfg;
        producer_ctx.ring = rb;
        consumer_ctx.cfg = cfg;
        consumer_ctx.ring = rb;

        start_ns = now_ns();

        if (pthread_create(&producer, NULL, producer_main, &producer_ctx) != 0) {
            die("pthread_create(producer)");
        }
        if (pthread_create(&consumer, NULL, consumer_main, &consumer_ctx) != 0) {
            die("pthread_create(consumer)");
        }

        if (pthread_join(producer, NULL) != 0) {
            die("pthread_join(producer)");
        }
        if (pthread_join(consumer, &consumer_ret) != 0) {
            die("pthread_join(consumer)");
        }

        end_ns = now_ns();

        consumer_result_t *cres = (consumer_result_t *)consumer_ret;
        one_run_result_t out;
        out.elapsed_ns = end_ns - start_ns;
        out.checksum = cres ? cres->checksum : 0;
        out.full_hits = atomic_load_explicit(&rb->full_hits, memory_order_relaxed);

        free(cres);
        ring_packed_destroy(rb);
        return out;
    } else {
        ring_padded_t *rb = ring_padded_create(cfg->capacity);

        producer_ctx.cfg = cfg;
        producer_ctx.ring = rb;
        consumer_ctx.cfg = cfg;
        consumer_ctx.ring = rb;

        start_ns = now_ns();

        if (pthread_create(&producer, NULL, producer_main, &producer_ctx) != 0) {
            die("pthread_create(producer)");
        }
        if (pthread_create(&consumer, NULL, consumer_main, &consumer_ctx) != 0) {
            die("pthread_create(consumer)");
        }

        if (pthread_join(producer, NULL) != 0) {
            die("pthread_join(producer)");
        }
        if (pthread_join(consumer, &consumer_ret) != 0) {
            die("pthread_join(consumer)");
        }

        end_ns = now_ns();

        consumer_result_t *cres = (consumer_result_t *)consumer_ret;
        one_run_result_t out;
        out.elapsed_ns = end_ns - start_ns;
        out.checksum = cres ? cres->checksum : 0;
        out.full_hits = atomic_load_explicit(&rb->full_hits, memory_order_relaxed);

        free(cres);
        ring_padded_destroy(rb);
        return out;
    }
}
int main(int argc, char **argv) {
	uint64_t best_full_hits = 0;
	config_t cfg = {
		.mode = MODE_PADDED,
		.capacity = 1024,
		.mask = 1023,
		.messages = 10000000,
		.producer_cpu = -1,
		.consumer_cpu = -1
	};

	int repeats = 5;
	int warmup = 1;
	const char *csv_path = "artifacts/data/ring_buffer.csv";

for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--mode requires a value\n");
                return EXIT_FAILURE;
            }
            cfg.mode = parse_mode(argv[i]);
        } else if (strcmp(argv[i], "--capacity") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--capacity requires a value\n");
                return EXIT_FAILURE;
            }
            cfg.capacity = parse_u64(argv[i], "capacity");
        } else if (strcmp(argv[i], "--messages") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--messages requires a value\n");
                return EXIT_FAILURE;
            }
            cfg.messages = parse_u64(argv[i], "messages");
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--repeats requires a value\n");
                return EXIT_FAILURE;
            }
            repeats = parse_int(argv[i], "repeats");
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--warmup requires a value\n");
                return EXIT_FAILURE;
            }
            warmup = parse_int(argv[i], "warmup");
        } else if (strcmp(argv[i], "--producer-cpu") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--producer-cpu requires a value\n");
                return EXIT_FAILURE;
            }
            cfg.producer_cpu = parse_int(argv[i], "producer-cpu");
        } else if (strcmp(argv[i], "--consumer-cpu") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--consumer-cpu requires a value\n");
                return EXIT_FAILURE;
            }
            cfg.consumer_cpu = parse_int(argv[i], "consumer-cpu");
        } else if (strcmp(argv[i], "--csv") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "--csv requires a value\n");
                return EXIT_FAILURE;
            }
            csv_path = argv[i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!is_power_of_two_u64(cfg.capacity)) {
        fprintf(stderr, "capacity must be a power of two, got %" PRIu64 "\n", cfg.capacity);
        return EXIT_FAILURE;
    }
    if (cfg.messages == 0) {
        fprintf(stderr, "messages must be > 0\n");
        return EXIT_FAILURE;
    }
    if (repeats <= 0) {
        fprintf(stderr, "repeats must be > 0\n");
        return EXIT_FAILURE;
    }
    if (warmup < 0) {
        fprintf(stderr, "warmup must be >= 0\n");
        return EXIT_FAILURE;
    }

    cfg.mask = cfg.capacity - 1;

    printf("[configuration]\n");
    printf("  mode         = %s\n", cfg.mode == MODE_PACKED ? "packed" : "padded");
    printf("  capacity     = %" PRIu64 "\n", cfg.capacity);
    printf("  messages     = %" PRIu64 "\n", cfg.messages);
    printf("  repeats      = %d\n", repeats);
    printf("  warmup       = %d\n", warmup);
    printf("  producer_cpu = %d\n", cfg.producer_cpu);
    printf("  consumer_cpu = %d\n", cfg.consumer_cpu);
    printf("  csv          = %s\n", csv_path);

    for (int i = 0; i < warmup; ++i) {
        one_run_result_t r = run_once(&cfg);
        global_sink ^= r.checksum;
        printf("[warmup %d/%d] elapsed_ns=%" PRIu64 " checksum=%" PRIu64 "\n",
               i + 1, warmup, r.elapsed_ns, r.checksum);
    }

    FILE *csv = open_csv_append_with_header(csv_path);

    uint64_t best_elapsed_ns = UINT64_MAX;
    double best_ns_per_msg = 0.0;
    double best_mmsgs_per_sec = 0.0;
    uint64_t final_checksum = 0;

    for (int i = 0; i < repeats; ++i) {
        one_run_result_t r = run_once(&cfg);
        global_sink ^= r.checksum;

        double ns_per_msg = (double)r.elapsed_ns / (double)cfg.messages;
        double mmsgs_per_sec = ((double)cfg.messages / (double)r.elapsed_ns) * 1000.0;

        if (r.elapsed_ns < best_elapsed_ns) {
            best_elapsed_ns = r.elapsed_ns;
            best_ns_per_msg = ns_per_msg;
            best_mmsgs_per_sec = mmsgs_per_sec;
            final_checksum = r.checksum;
	    best_full_hits = r.full_hits;
        }

        fprintf(csv, "%s,%" PRIu64 ",%" PRIu64 ",%d,%d,%" PRIu64 ",%.6f,%.6f,%" PRIu64 ",%" PRIu64 "\n",
                cfg.mode == MODE_PACKED ? "packed" : "padded",
                cfg.capacity,
                cfg.messages,
                cfg.producer_cpu,
                cfg.consumer_cpu,
                r.elapsed_ns,
                ns_per_msg,
                mmsgs_per_sec,
                r.checksum,
		r.full_hits);

        fflush(csv);

        printf("[run %d/%d] elapsed_ns=%" PRIu64
               " ns_per_msg=%.6f mmsgs_per_sec=%.6f checksum=%" PRIu64
	      " full_hits=%" PRIu64 "\n",
               i + 1, repeats, r.elapsed_ns, ns_per_msg, mmsgs_per_sec, r.checksum, r.full_hits);
    }

    fclose(csv);
	
    printf("[summary] best_elapsed_ns=%" PRIu64
           " best_ns_per_msg=%.6f best_mmsgs_per_sec=%.6f checksum=%" PRIu64
	  " full_hits=%" PRIu64 "\n",
           best_elapsed_ns, best_ns_per_msg, best_mmsgs_per_sec, final_checksum, best_full_hits);
    printf("[sink] %" PRIu64 "\n", global_sink);

    return 0;
}
