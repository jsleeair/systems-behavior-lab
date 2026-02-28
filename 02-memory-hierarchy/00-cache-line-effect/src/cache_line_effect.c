/*
 * cache_line_effect.c
 *
 * Goal:
 *   Empirically observe cache-line effects by varying stride and access density.
 *   You should see throughput collapse when stride grows beyond cache line size, and additional effects when working set exceeds LLC.
 *
 *   Notes:
 *     - We intentionally do "simple" byte touches to isolate memory behavior
 *     - We compute a checksum to prevent the compiler from optimizing work away.
 *     - We provide two modes:
 *         (A) sparse: touch 1 byte per stride step (wastes most of cache line)
 *         (B) dense:  touch N bytes within the same cache line per step (uses the line)
 *
 * Build:
 *   gcc -O2 -march=native -Wall -Wextra -std=c11 cache_line_effect.c -o cache_line_effect
 *
 * Run (example):
 *   ./cache_line_effect --size-mb 256 --reps 10 --stride 64 --mode sparse
 *   ./cache_line_effect --size-mb 256 --reps 10 --stride 64 --mode dense --dense-bytes 64
 *
 * Output:
 *   prints one CSV line to stdout (so scripts can append to results.csv).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void die(const char *msg) {
    fprintf(stderr, "fatal: %s (errno=%d: %s)\n", msg, errno, strerror(errno));
    exit(1);
}

static size_t parse_size_mb(const char *s) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0' || v <= 0) die("invalid --size-mb");
    return (size_t)v * 1024ull * 1024ull;
}

static size_t parse_size_pos(const char *s, const char *flagname) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0' || v <= 0) {
        fprintf(stderr, "fatal: invalid %s\n", flagname);
        exit(1);
    }
    return (size_t)v;
}

static int parse_bool01(const char *s, const char *flagname) {
    if (strcmp(s, "0") == 0) return 0;
    if (strcmp(s, "1") == 0) return 1;
    fprintf(stderr, "fatal: %s must be 0 or 1 (got '%s')\n", flagname, s);
    exit(1);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --size-mb N            Working set size in MB (default: 256)\n"
        "  --reps N               Repetitions (default: 10)\n"
        "  --stride N             Stride in bytes (default: 64)\n"
        "  --mode sparse|dense    (default: sparse)\n"
        "  --dense-bytes N        In dense mode, touch N bytes per step (default: 64)\n"
        "  --warmup-mb N          Warmup/flush buffer size in MB (default: 16)\n"
        "  --write 0|1            If 1, do writes instead of reads (default: 0)\n"
        "  --flush none|once|each Cache flush policy using warmup buffer (default: each)\n"
        "\n"
        "CSV output columns:\n"
	"  size_bytes,stride,mode,dense_bytes,reps,write,elapsed_ns,steps,bytes_touched,checksum,useful_GBps,traffic_GBps\n",
        argv0
    );
}

static inline uint8_t rotl8(uint8_t x, unsigned r) {
    return (uint8_t)((x << (r & 7)) | (x >> ((8 - r) & 7)));
}

static inline uint64_t ceil_div_u64(uint64_t a, uint64_t b) {
    return (a + b - 1) / b;
}

/*
 * Estimate how many distinct cache lines are touched per repetition.
 *
 * Assumptions (simple but practical for this lab):
 *  - Cache line size is 64 bytes (common on x86_64).
 *  - If stride >= 64: each step likely lands on a different line (no overlap),
 *    so "lines ~ steps * lines_per_step".
 *  - If stride < 64: access pattern walks through the region densely enough
 *    that we eventually touch (almost) all lines in the working set,
 *    so "lines ~ ceil(size_bytes / 64)".
 *
 * This is an approximation. It intentionally trades precision for interpretability.
 */
static uint64_t estimate_lines_per_rep(size_t size_bytes,
                                      size_t stride,
                                      int is_dense,
                                      size_t dense_bytes,
                                      size_t steps) {
    const uint64_t line_size = 64;

    uint64_t total_lines_in_region = ceil_div_u64((uint64_t)size_bytes, line_size);

    if ((uint64_t)stride < line_size) {
        // For stride < 64, we assume the pattern touches (nearly) all lines.
        return total_lines_in_region;
    }

    // stride >= 64: each step tends to hit a new line (no overlap)
    uint64_t lines_per_step = 1;
    if (is_dense) {
        // dense_bytes may span multiple lines if >64
        lines_per_step = ceil_div_u64((uint64_t)dense_bytes, line_size);
    }

    return (uint64_t)steps * lines_per_step;
}

static void do_warmup(volatile uint64_t *checksum, uint8_t *warm, size_t warmup_bytes) {
    // Step by 64 to touch one byte per cache line.
    for (size_t i = 0; i < warmup_bytes; i += 64) {
        *checksum += warm[i];
    }
}

typedef enum { FLUSH_NONE=0, FLUSH_ONCE=1, FLUSH_EACH=2 } flush_mode_t;

int main(int argc, char **argv) {
    size_t size_bytes = 256ull * 1024ull * 1024ull;
    size_t reps = 10;
    size_t stride = 64;
    int write_mode = 0;

    enum { MODE_SPARSE, MODE_DENSE } mode = MODE_SPARSE;
    size_t dense_bytes = 64;
    size_t warmup_bytes = 16ull * 1024ull * 1024ull;
    flush_mode_t flush_mode = FLUSH_EACH;

    // ---------- arg parse ----------
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size-mb") && i + 1 < argc) {
            size_bytes = parse_size_mb(argv[++i]);
        } else if (!strcmp(argv[i], "--reps") && i + 1 < argc) {
            reps = parse_size_pos(argv[++i], "--reps");
        } else if (!strcmp(argv[i], "--stride") && i + 1 < argc) {
            stride = parse_size_pos(argv[++i], "--stride");
        } else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *m = argv[++i];
            if (!strcmp(m, "sparse")) mode = MODE_SPARSE;
            else if (!strcmp(m, "dense")) mode = MODE_DENSE;
            else die("invalid --mode (use sparse|dense)");
        } else if (!strcmp(argv[i], "--dense-bytes") && i + 1 < argc) {
            dense_bytes = parse_size_pos(argv[++i], "--dense-bytes");
        } else if (!strcmp(argv[i], "--warmup-mb") && i + 1 < argc) {
            warmup_bytes = parse_size_mb(argv[++i]);
        } else if (!strcmp(argv[i], "--write") && i + 1 < argc) {
            write_mode = parse_bool01(argv[++i], "--write");
        } else if (!strcmp(argv[i], "--flush") && i + 1 < argc) {
            const char *fm = argv[++i];
            if (!strcmp(fm, "none")) flush_mode = FLUSH_NONE;
            else if (!strcmp(fm, "once")) flush_mode = FLUSH_ONCE;
            else if (!strcmp(fm, "each")) flush_mode = FLUSH_EACH;
            else die("invalid --flush (use none|once|each)");
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (stride == 0) die("stride must be > 0");
    if (mode == MODE_DENSE && dense_bytes == 0) die("dense_bytes must be > 0");
    if (mode == MODE_DENSE && dense_bytes > size_bytes) die("dense_bytes > size_bytes");

    // ---------- alloc ----------
    uint8_t *buf = NULL;
    if (posix_memalign((void **)&buf, 64, size_bytes) != 0) die("posix_memalign(buf) failed");

    for (size_t i = 0; i < size_bytes; i++) {
        buf[i] = (uint8_t)(i * 13u + 7u);
    }

    uint8_t *warm = NULL;
    if (warmup_bytes > 0) {
        if (posix_memalign((void **)&warm, 64, warmup_bytes) != 0) die("posix_memalign(warm) failed");
        for (size_t i = 0; i < warmup_bytes; i++) warm[i] = (uint8_t)(i * 17u + 3u);
    }

    volatile uint64_t checksum = 0;

    // dense safe upper bound
    size_t max_index = size_bytes;
    if (mode == MODE_DENSE) {
        max_index = size_bytes - dense_bytes + 1;
    }
    size_t steps = (max_index + stride - 1) / stride;

    // ---------- benchmark ----------
    if (flush_mode == FLUSH_ONCE && warmup_bytes > 0) {
        do_warmup(&checksum, warm, warmup_bytes);
    }

    uint64_t t0 = now_ns();

    for (size_t r = 0; r < reps; r++) {
        if (flush_mode == FLUSH_EACH && warmup_bytes > 0) {
            do_warmup(&checksum, warm, warmup_bytes);
        }

        if (mode == MODE_SPARSE) {
            for (size_t i = 0; i < size_bytes; i += stride) {
                if (write_mode) {
                    buf[i] = rotl8((uint8_t)(buf[i] + (uint8_t)r), (unsigned)(i & 7));
                } else {
                    checksum += buf[i];
                }
            }
        } else { // MODE_DENSE
            for (size_t i = 0; i < max_index; i += stride) {
                if (write_mode) {
                    for (size_t k = 0; k < dense_bytes; k++) {
                        buf[i + k] = (uint8_t)(buf[i + k] + (uint8_t)(k + r));
                    }
                } else {
                    for (size_t k = 0; k < dense_bytes; k++) {
                        checksum += buf[i + k];
                    }
                }
            }
        }
    }

    uint64_t t1 = now_ns();
    uint64_t elapsed_ns = t1 - t0;

    uint64_t bytes_touched_per_rep = (mode == MODE_SPARSE)
        ? (uint64_t)steps * 1ull
        : (uint64_t)steps * (uint64_t)dense_bytes;

    uint64_t bytes_touched_total = bytes_touched_per_rep * (uint64_t)reps;
    double elapsed_s = (double)elapsed_ns / 1e9;

    // "Useful" throughput: bytes your loop actually touched (1B for sparse, dense_bytes for dense).
    double useful_gbps = (elapsed_s > 0.0) ? ((double)bytes_touched_total / elapsed_s / 1e9) : 0.0;

    // "Traffic" throughput: estimated bytes moved in cache-line units.
    // traffic_bytes_total = lines_per_rep * 64 * reps
    uint64_t lines_per_rep = estimate_lines_per_rep(
    size_bytes,
    stride,
    (mode == MODE_DENSE),
    dense_bytes,
    steps
    );
    uint64_t traffic_bytes_total = lines_per_rep * 64ull * (uint64_t)reps;

    double traffic_gbps = (elapsed_s > 0.0) ? ((double)traffic_bytes_total / elapsed_s / 1e9) : 0.0;
    printf("%zu,%zu,%s,%zu,%zu,%d,%" PRIu64 ",%zu,%" PRIu64 ",%" PRIu64 ",%.6f,%.6f\n",

           size_bytes,
           stride,
           (mode == MODE_SPARSE ? "sparse" : "dense"),
           (mode == MODE_SPARSE ? (size_t)1 : dense_bytes),
           reps,
           write_mode,
           elapsed_ns,
           steps,
           bytes_touched_total,
           (uint64_t)checksum,
	   useful_gbps,
	   traffic_gbps
	);

    free(warm);
    free(buf);
    return 0;
}
