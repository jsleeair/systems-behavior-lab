#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * context_switch.c
 *
 * This benchmark measures the end-to-end cost of a blocking wakeup based
 * "ping-pong" between a parent process and a child process.
 *
 * Design:
 *   - The parent and child communicate through two pipes.
 *   - Parent writes 1 byte to the child, then blocks waiting for a reply.
 *   - Child blocks waiting for the parent's byte, then writes 1 byte back.
 *   - One round-trip therefore includes roughly two task hand-offs:
 *         parent -> child
 *         child  -> parent
 *
 * What this benchmark captures:
 *   - system call overhead of pipe read/write
 *   - scheduler wakeup / sleep behavior
 *   - process context switch cost
 *   - cache / locality effects, depending on CPU pinning
 *
 * Important interpretation note:
 *   This is NOT a pure "scheduler internal only" cost. It is better understood as
 *   the cost of a blocking IPC round-trip, where context switching is a major part.
 *
 * Usage example:
 *   ./artifacts/bin/context_switch -n 200000 -w 10000 -m same -c 0
 *
 * Output:
 *   A single CSV-like line that is easy to capture into a file.
 */

typedef struct {
    long iterations;      /* Number of measured ping-pong round trips */
    long warmup;          /* Number of warmup round trips before timing */
    int base_cpu;         /* Base CPU index used for affinity */
    int split_core;       /* 0 = same core, 1 = parent/child on different cores */
} config_t;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static long parse_long(const char *s, const char *name) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

static int parse_int(const char *s, const char *name) {
    long v = parse_long(s, name);
    if (v < -2147483648L || v > 2147483647L) {
        fprintf(stderr, "Out-of-range %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-n iterations] [-w warmup] [-c base_cpu] [-m same|split]\n"
            "\n"
            "Options:\n"
            "  -n iterations   Number of measured round trips (default: 200000)\n"
            "  -w warmup       Number of warmup round trips   (default: 20000)\n"
            "  -c base_cpu     Base CPU index for affinity    (default: 0)\n"
            "  -m mode         CPU placement mode: same|split (default: same)\n",
            prog);
}

static void parse_args(int argc, char **argv, config_t *cfg) {
    cfg->iterations = 200000;
    cfg->warmup = 20000;
    cfg->base_cpu = 0;
    cfg->split_core = 0;

    int opt;
    while ((opt = getopt(argc, argv, "n:w:c:m:h")) != -1) {
        switch (opt) {
        case 'n':
            cfg->iterations = parse_long(optarg, "iterations");
            break;
        case 'w':
            cfg->warmup = parse_long(optarg, "warmup");
            break;
        case 'c':
            cfg->base_cpu = parse_int(optarg, "base_cpu");
            break;
        case 'm':
            if (strcmp(optarg, "same") == 0) {
                cfg->split_core = 0;
            } else if (strcmp(optarg, "split") == 0) {
                cfg->split_core = 1;
            } else {
                fprintf(stderr, "Invalid mode: %s (expected same or split)\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (cfg->iterations <= 0) {
        fprintf(stderr, "iterations must be > 0\n");
        exit(EXIT_FAILURE);
    }
    if (cfg->warmup < 0) {
        fprintf(stderr, "warmup must be >= 0\n");
        exit(EXIT_FAILURE);
    }
    if (cfg->base_cpu < 0) {
        fprintf(stderr, "base_cpu must be >= 0\n");
        exit(EXIT_FAILURE);
    }
}

static void pin_to_cpu_or_die(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        die("sched_setaffinity");
    }
}

static inline uint64_t timespec_to_ns(const struct timespec *ts) {
    return (uint64_t)ts->tv_sec * 1000000000ull + (uint64_t)ts->tv_nsec;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        die("clock_gettime");
    }
    return timespec_to_ns(&ts);
}

static void write_byte_or_die(int fd, char v) {
    ssize_t ret = write(fd, &v, 1);
    if (ret != 1) {
        die("write");
    }
}

static void read_byte_or_die(int fd, char *out) {
    ssize_t ret = read(fd, out, 1);
    if (ret != 1) {
        die("read");
    }
}

static void child_loop(int read_fd, int write_fd, long total_rounds, int child_cpu) {
    /*
     * The child blocks on read(), wakes up when the parent writes a byte,
     * then immediately writes back one byte to wake the parent.
     */
    pin_to_cpu_or_die(child_cpu);

    char token;
    for (long i = 0; i < total_rounds; ++i) {
        read_byte_or_die(read_fd, &token);
        write_byte_or_die(write_fd, token);
    }

    close(read_fd);
    close(write_fd);
    _exit(0);
}

int main(int argc, char **argv) {
    config_t cfg;
    parse_args(argc, argv, &cfg);

    /*
     * Two pipes:
     *   p2c: parent -> child
     *   c2p: child  -> parent
     */
    int p2c[2];
    int c2p[2];
    if (pipe(p2c) != 0) die("pipe p2c");
    if (pipe(c2p) != 0) die("pipe c2p");

    int parent_cpu = cfg.base_cpu;
    int child_cpu = cfg.split_core ? (cfg.base_cpu + 1) : cfg.base_cpu;

    /*
     * We use total_rounds = warmup + measured iterations.
     * The parent controls when timing starts, while the child simply participates
     * in all exchanges.
     */
    long total_rounds = cfg.warmup + cfg.iterations;

    pid_t pid = fork();
    if (pid < 0) {
        die("fork");
    }

    if (pid == 0) {
        /* Child process */
        close(p2c[1]); /* child does not write to p2c */
        close(c2p[0]); /* child does not read from c2p */
        child_loop(p2c[0], c2p[1], total_rounds, child_cpu);
    }

    /* Parent process */
    close(p2c[0]); /* parent does not read from p2c */
    close(c2p[1]); /* parent does not write to c2p */

    pin_to_cpu_or_die(parent_cpu);

    char token = 'x';

    /*
     * Warmup phase:
     *   We intentionally do a number of untimed round trips so that
     *   startup effects, first wakeups, and initial scheduler/cache noise
     *   do not dominate the result.
     */
    for (long i = 0; i < cfg.warmup; ++i) {
        write_byte_or_die(p2c[1], token);
        read_byte_or_die(c2p[0], &token);
    }

    uint64_t t0 = now_ns();

    /*
     * Measured phase:
     *   Each loop iteration is one full round-trip:
     *     parent write -> child wake/read/write -> parent wake/read
     *
     *   A rough approximation is:
     *     1 round-trip ~= 2 context switches
     */
    for (long i = 0; i < cfg.iterations; ++i) {
        write_byte_or_die(p2c[1], token);
        read_byte_or_die(c2p[0], &token);
    }

    uint64_t t1 = now_ns();

    close(p2c[1]);
    close(c2p[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        die("waitpid");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Child exited abnormally\n");
        return EXIT_FAILURE;
    }

    uint64_t elapsed_ns = t1 - t0;
    double ns_per_roundtrip = (double)elapsed_ns / (double)cfg.iterations;
    double ns_per_context_switch_est = ns_per_roundtrip / 2.0;

    /*
     * Emit a machine-friendly CSV row.
     * This makes shell collection very easy.
     */
    printf("mode=%s,iterations=%ld,warmup=%ld,parent_cpu=%d,child_cpu=%d,"
           "elapsed_ns=%llu,ns_per_roundtrip=%.2f,ns_per_context_switch_est=%.2f\n",
           cfg.split_core ? "split" : "same",
           cfg.iterations,
           cfg.warmup,
           parent_cpu,
           child_cpu,
           (unsigned long long)elapsed_ns,
           ns_per_roundtrip,
           ns_per_context_switch_est);

    return 0;
}
