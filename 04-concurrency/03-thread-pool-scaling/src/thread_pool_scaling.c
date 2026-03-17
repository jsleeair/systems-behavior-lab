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

/*
 * 04-concurrency/03-thread-pool-scaling
 *
 * This lab measures how a simple thread pool scales as:
 *   - the number of worker threads changes
 *   - the amount of work per task changes
 *   - CPU pinning is enabled/disabled
 *
 * Why this experiment matters:
 *   A thread pool is not "free parallelism".
 *   Real throughput depends on task granularity, queue contention,
 *   synchronization overhead, wakeups, and the scheduler.
 *
 * We intentionally implement a simple educational thread pool:
 *   - fixed number of worker threads
 *   - bounded ring-buffer task queue
 *   - mutex + condition variables
 *   - main thread submits tasks
 *   - workers consume tasks and execute CPU-bound work
 *
 * This design makes the overhead visible and measurable.
 */

/* ----------------------------- Utilities ----------------------------- */

static void die_perror(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        die_perror("clock_gettime");
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int get_online_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) {
        return 1;
    }
    return (int)n;
}

/*
 * Pin the calling thread to a specific CPU.
 * We use pthread_setaffinity_np because this benchmark is Linux-focused.
 */
static void pin_current_thread_to_cpu(int cpu_id) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);

    int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        errno = rc;
        die_perror("pthread_setaffinity_np");
    }
}

/* -------------------------- Benchmark Payload ------------------------- */

/*
 * Each task performs CPU-bound work with a deterministic pseudo-random mix
 * of arithmetic and bit operations.
 *
 * Goals:
 *   1) Avoid being optimized away
 *   2) Keep tasks mostly independent
 *   3) Let task granularity be controlled by task_iters
 *
 * The returned value is written to a per-task output slot, which avoids
 * introducing an artificial global synchronization bottleneck in the payload.
 */
static uint64_t cpu_burn(uint64_t seed, int task_iters) {
    uint64_t x = seed ^ 0x9e3779b97f4a7c15ull;

    for (int i = 0; i < task_iters; i++) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= 0x2545F4914F6CDD1Dull;

        /* Mix in the loop index to avoid trivial recurrence patterns. */
        x += (uint64_t)i * 0x9e3779b185ebca87ull;
        x = (x << 7) | (x >> (64 - 7));
    }

    return x;
}

typedef struct {
    int task_id;
    int task_iters;
    uint64_t seed;
    uint64_t *result_slot;
} bench_task_arg_t;

static void run_bench_task(void *arg_ptr) {
    bench_task_arg_t *arg = (bench_task_arg_t *)arg_ptr;
    uint64_t v = cpu_burn(arg->seed + (uint64_t)arg->task_id, arg->task_iters);
    *(arg->result_slot) = v;
}

/* ---------------------------- Thread Pool ---------------------------- */

typedef void (*task_fn_t)(void *);

/*
 * A single queue entry.
 * We store a function pointer and an opaque argument.
 */
typedef struct {
    task_fn_t fn;
    void *arg;
} task_t;

/*
 * Thread pool state.
 *
 * queue:
 *   A bounded circular buffer protected by one mutex.
 *
 * Synchronization:
 *   - cv_not_empty: workers wait here when the queue is empty
 *   - cv_not_full:  producer waits here when the queue is full
 *   - cv_all_done:  main thread waits until outstanding work becomes zero
 *
 * outstanding:
 *   Number of tasks submitted but not yet completed.
 *   This includes tasks waiting in the queue plus tasks currently running.
 */
typedef struct {
    pthread_t *threads;
    int num_threads;

    task_t *queue;
    int queue_capacity;
    int queue_head;
    int queue_tail;
    int queue_size;

    pthread_mutex_t mu;
    pthread_cond_t cv_not_empty;
    pthread_cond_t cv_not_full;
    pthread_cond_t cv_all_done;

    int stop;

    uint64_t outstanding;

    int pin_workers;
    int online_cpus;
} thread_pool_t;

typedef struct {
    thread_pool_t *pool;
    int worker_index;
} worker_arg_t;

static void *worker_main(void *opaque) {
    worker_arg_t *warg = (worker_arg_t *)opaque;
    thread_pool_t *pool = warg->pool;

    if (pool->pin_workers) {
        int cpu_id = warg->worker_index % pool->online_cpus;
        pin_current_thread_to_cpu(cpu_id);
    }

    for (;;) {
        task_t task;

        if (pthread_mutex_lock(&pool->mu) != 0) {
            die_perror("pthread_mutex_lock");
        }

        /*
         * Standard condition-variable pattern:
         * wait in a loop, because wakeups can be spurious and because the
         * condition must be re-checked after re-acquiring the mutex.
         */
        while (pool->queue_size == 0 && !pool->stop) {
            if (pthread_cond_wait(&pool->cv_not_empty, &pool->mu) != 0) {
                die_perror("pthread_cond_wait");
            }
        }

        /*
         * If stop is set and the queue is empty, worker exits cleanly.
         */
        if (pool->stop && pool->queue_size == 0) {
            if (pthread_mutex_unlock(&pool->mu) != 0) {
                die_perror("pthread_mutex_unlock");
            }
            break;
        }

        /* Pop one task from the ring buffer. */
        task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_capacity;
        pool->queue_size--;

        /*
         * Wake producer if it was blocked because the queue was full.
         */
        if (pthread_cond_signal(&pool->cv_not_full) != 0) {
            die_perror("pthread_cond_signal");
        }

        if (pthread_mutex_unlock(&pool->mu) != 0) {
            die_perror("pthread_mutex_unlock");
        }

        /* Execute task outside the mutex. */
        task.fn(task.arg);

        if (pthread_mutex_lock(&pool->mu) != 0) {
            die_perror("pthread_mutex_lock");
        }

        pool->outstanding--;

        /*
         * When outstanding reaches zero, all submitted work has completed.
         */
        if (pool->outstanding == 0) {
            if (pthread_cond_broadcast(&pool->cv_all_done) != 0) {
                die_perror("pthread_cond_broadcast");
            }
        }

        if (pthread_mutex_unlock(&pool->mu) != 0) {
            die_perror("pthread_mutex_unlock");
        }
    }

    return NULL;
}

static void thread_pool_init(thread_pool_t *pool,
                             int num_threads,
                             int queue_capacity,
                             int pin_workers) {
    if (num_threads <= 0) {
        die_msg("num_threads must be > 0");
    }
    if (queue_capacity <= 0) {
        die_msg("queue_capacity must be > 0");
    }

    memset(pool, 0, sizeof(*pool));

    pool->num_threads = num_threads;
    pool->queue_capacity = queue_capacity;
    pool->pin_workers = pin_workers;
    pool->online_cpus = get_online_cpu_count();

    pool->threads = (pthread_t *)calloc((size_t)num_threads, sizeof(pthread_t));
    pool->queue = (task_t *)calloc((size_t)queue_capacity, sizeof(task_t));
    if (!pool->threads || !pool->queue) {
        die_perror("calloc");
    }

    if (pthread_mutex_init(&pool->mu, NULL) != 0) {
        die_perror("pthread_mutex_init");
    }
    if (pthread_cond_init(&pool->cv_not_empty, NULL) != 0) {
        die_perror("pthread_cond_init cv_not_empty");
    }
    if (pthread_cond_init(&pool->cv_not_full, NULL) != 0) {
        die_perror("pthread_cond_init cv_not_full");
    }
    if (pthread_cond_init(&pool->cv_all_done, NULL) != 0) {
        die_perror("pthread_cond_init cv_all_done");
    }

    worker_arg_t *wargs =
        (worker_arg_t *)calloc((size_t)num_threads, sizeof(worker_arg_t));
    if (!wargs) {
        die_perror("calloc worker args");
    }

    for (int i = 0; i < num_threads; i++) {
        wargs[i].pool = pool;
        wargs[i].worker_index = i;

        int rc = pthread_create(&pool->threads[i], NULL, worker_main, &wargs[i]);
        if (rc != 0) {
            errno = rc;
            die_perror("pthread_create");
        }
    }

    /*
     * Important note:
     * wargs must stay alive until all workers have read their startup args.
     * The simplest safe approach here is to join workers before freeing wargs.
     * To keep the interface compact, we store the pointer inside pool by
     * reusing an otherwise-unused slot through a cast.
     */
    pool->queue_tail = 0; /* explicit initialization for readability */

    /*
     * Store worker args pointer in a portable, visible way would require
     * extending the struct. Let's do that instead of using tricks.
     */
    {
        /* no-op; handled by explicit struct extension below */
    }

    /* This function will be redefined after struct extension block. */
}

/*
 * To keep the implementation readable and safe, extend the pool with worker
 * argument storage explicitly instead of hiding it somewhere unsafe.
 */
typedef struct {
    thread_pool_t base;
    worker_arg_t *worker_args;
} owned_thread_pool_t;

static void owned_thread_pool_init(owned_thread_pool_t *opool,
                                   int num_threads,
                                   int queue_capacity,
                                   int pin_workers) {
    thread_pool_t *pool = &opool->base;

    if (num_threads <= 0) {
        die_msg("num_threads must be > 0");
    }
    if (queue_capacity <= 0) {
        die_msg("queue_capacity must be > 0");
    }

    memset(opool, 0, sizeof(*opool));

    pool->num_threads = num_threads;
    pool->queue_capacity = queue_capacity;
    pool->pin_workers = pin_workers;
    pool->online_cpus = get_online_cpu_count();

    pool->threads = (pthread_t *)calloc((size_t)num_threads, sizeof(pthread_t));
    pool->queue = (task_t *)calloc((size_t)queue_capacity, sizeof(task_t));
    opool->worker_args =
        (worker_arg_t *)calloc((size_t)num_threads, sizeof(worker_arg_t));

    if (!pool->threads || !pool->queue || !opool->worker_args) {
        die_perror("calloc");
    }

    if (pthread_mutex_init(&pool->mu, NULL) != 0) {
        die_perror("pthread_mutex_init");
    }
    if (pthread_cond_init(&pool->cv_not_empty, NULL) != 0) {
        die_perror("pthread_cond_init cv_not_empty");
    }
    if (pthread_cond_init(&pool->cv_not_full, NULL) != 0) {
        die_perror("pthread_cond_init cv_not_full");
    }
    if (pthread_cond_init(&pool->cv_all_done, NULL) != 0) {
        die_perror("pthread_cond_init cv_all_done");
    }

    for (int i = 0; i < num_threads; i++) {
        opool->worker_args[i].pool = pool;
        opool->worker_args[i].worker_index = i;

        int rc = pthread_create(
            &pool->threads[i], NULL, worker_main, &opool->worker_args[i]);
        if (rc != 0) {
            errno = rc;
            die_perror("pthread_create");
        }
    }
}

static void thread_pool_submit(thread_pool_t *pool, task_fn_t fn, void *arg) {
    if (pthread_mutex_lock(&pool->mu) != 0) {
        die_perror("pthread_mutex_lock");
    }

    /*
     * If the queue is full, the producer waits.
     * This backpressure is useful for observing queue capacity effects.
     */
    while (pool->queue_size == pool->queue_capacity && !pool->stop) {
        if (pthread_cond_wait(&pool->cv_not_full, &pool->mu) != 0) {
            die_perror("pthread_cond_wait");
        }
    }

    if (pool->stop) {
        if (pthread_mutex_unlock(&pool->mu) != 0) {
            die_perror("pthread_mutex_unlock");
        }
        die_msg("submit attempted after pool stop");
    }

    pool->queue[pool->queue_tail].fn = fn;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_capacity;
    pool->queue_size++;
    pool->outstanding++;

    if (pthread_cond_signal(&pool->cv_not_empty) != 0) {
        die_perror("pthread_cond_signal");
    }

    if (pthread_mutex_unlock(&pool->mu) != 0) {
        die_perror("pthread_mutex_unlock");
    }
}

static void thread_pool_wait_all(thread_pool_t *pool) {
    if (pthread_mutex_lock(&pool->mu) != 0) {
        die_perror("pthread_mutex_lock");
    }

    while (pool->outstanding != 0) {
        if (pthread_cond_wait(&pool->cv_all_done, &pool->mu) != 0) {
            die_perror("pthread_cond_wait");
        }
    }

    if (pthread_mutex_unlock(&pool->mu) != 0) {
        die_perror("pthread_mutex_unlock");
    }
}

static void owned_thread_pool_destroy(owned_thread_pool_t *opool) {
    thread_pool_t *pool = &opool->base;

    if (pthread_mutex_lock(&pool->mu) != 0) {
        die_perror("pthread_mutex_lock");
    }

    pool->stop = 1;

    if (pthread_cond_broadcast(&pool->cv_not_empty) != 0) {
        die_perror("pthread_cond_broadcast");
    }
    if (pthread_cond_broadcast(&pool->cv_not_full) != 0) {
        die_perror("pthread_cond_broadcast");
    }

    if (pthread_mutex_unlock(&pool->mu) != 0) {
        die_perror("pthread_mutex_unlock");
    }

    for (int i = 0; i < pool->num_threads; i++) {
        int rc = pthread_join(pool->threads[i], NULL);
        if (rc != 0) {
            errno = rc;
            die_perror("pthread_join");
        }
    }

    if (pthread_mutex_destroy(&pool->mu) != 0) {
        die_perror("pthread_mutex_destroy");
    }
    if (pthread_cond_destroy(&pool->cv_not_empty) != 0) {
        die_perror("pthread_cond_destroy cv_not_empty");
    }
    if (pthread_cond_destroy(&pool->cv_not_full) != 0) {
        die_perror("pthread_cond_destroy cv_not_full");
    }
    if (pthread_cond_destroy(&pool->cv_all_done) != 0) {
        die_perror("pthread_cond_destroy cv_all_done");
    }

    free(opool->worker_args);
    free(pool->threads);
    free(pool->queue);
}

/* ---------------------------- Benchmark Run --------------------------- */

typedef struct {
    int threads;
    int tasks;
    int task_iters;
    int warmup_rounds;
    int pin_workers;
    int queue_capacity;
} config_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s "
            "--threads N "
            "--tasks N "
            "--task-iters N "
            "[--warmup N] "
            "[--pin-workers 0|1] "
            "[--queue-capacity N]\n",
            prog);
}

static config_t parse_args(int argc, char **argv) {
    config_t cfg;
    cfg.threads = 0;
    cfg.tasks = 0;
    cfg.task_iters = 0;
    cfg.warmup_rounds = 1;
    cfg.pin_workers = 0;
    cfg.queue_capacity = 1024;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            cfg.threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--tasks") == 0 && i + 1 < argc) {
            cfg.tasks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--task-iters") == 0 && i + 1 < argc) {
            cfg.task_iters = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            cfg.warmup_rounds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pin-workers") == 0 && i + 1 < argc) {
            cfg.pin_workers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--queue-capacity") == 0 && i + 1 < argc) {
            cfg.queue_capacity = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (cfg.threads <= 0 || cfg.tasks <= 0 || cfg.task_iters <= 0 ||
        cfg.warmup_rounds < 0 || cfg.queue_capacity <= 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    return cfg;
}

static void submit_round(thread_pool_t *pool,
                         bench_task_arg_t *task_args,
                         int tasks) {
    for (int i = 0; i < tasks; i++) {
        thread_pool_submit(pool, run_bench_task, &task_args[i]);
    }
    thread_pool_wait_all(pool);
}

int main(int argc, char **argv) {
    config_t cfg = parse_args(argc, argv);

    owned_thread_pool_t opool;
    owned_thread_pool_init(
        &opool, cfg.threads, cfg.queue_capacity, cfg.pin_workers);
    thread_pool_t *pool = &opool.base;

    uint64_t *results = (uint64_t *)calloc((size_t)cfg.tasks, sizeof(uint64_t));
    bench_task_arg_t *task_args =
        (bench_task_arg_t *)calloc((size_t)cfg.tasks, sizeof(bench_task_arg_t));
    if (!results || !task_args) {
        die_perror("calloc");
    }

    for (int i = 0; i < cfg.tasks; i++) {
        task_args[i].task_id = i;
        task_args[i].task_iters = cfg.task_iters;
        task_args[i].seed = 0x12345678abcdef00ull + (uint64_t)i * 1315423911ull;
        task_args[i].result_slot = &results[i];
    }

    /*
     * Warmup rounds:
     *   Useful to reduce one-time effects such as initial page faults,
     *   cold instruction/data cache state, and thread startup transients.
     *
     * We keep the pool alive across rounds, which is closer to how real
     * thread pools are used in servers and runtimes.
     */
    for (int w = 0; w < cfg.warmup_rounds; w++) {
        memset(results, 0, (size_t)cfg.tasks * sizeof(uint64_t));
        submit_round(pool, task_args, cfg.tasks);
    }

    memset(results, 0, (size_t)cfg.tasks * sizeof(uint64_t));

    uint64_t t0 = now_ns();
    submit_round(pool, task_args, cfg.tasks);
    uint64_t t1 = now_ns();

    uint64_t checksum = 0;
    for (int i = 0; i < cfg.tasks; i++) {
        checksum ^= results[i];
    }

    uint64_t elapsed_ns = t1 - t0;
    double ns_per_task = (double)elapsed_ns / (double)cfg.tasks;
    double tasks_per_sec = (double)cfg.tasks * 1e9 / (double)elapsed_ns;

    /*
     * Machine-readable output:
     * one line, easy to append into CSV from shell scripts.
     */
    printf("threads=%d,"
           "tasks=%d,"
           "task_iters=%d,"
           "warmup_rounds=%d,"
           "pin_workers=%d,"
           "queue_capacity=%d,"
           "elapsed_ns=%" PRIu64 ","
           "ns_per_task=%.2f,"
           "tasks_per_sec=%.2f,"
           "checksum=%" PRIu64 "\n",
           cfg.threads,
           cfg.tasks,
           cfg.task_iters,
           cfg.warmup_rounds,
           cfg.pin_workers,
           cfg.queue_capacity,
           elapsed_ns,
           ns_per_task,
           tasks_per_sec,
           checksum);

    free(task_args);
    free(results);
    owned_thread_pool_destroy(&opool);

    return 0;
}
