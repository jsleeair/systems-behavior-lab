// 03-os-boundary/02-mmap-vs-read/src/main.c
//
// This lab compares two ways of reading a file from user space:
//
//   1) read()
//   2) mmap()
//
// The goal is not to prove that one is always faster than the other.
// Instead, the goal is to observe how the OS boundary changes:
//
// - read(): repeated syscalls + explicit kernel-to-user copy
// - mmap(): fewer explicit reads, but page faults on first touch
//
// We intentionally keep the "work" per page very small so that the result
// is dominated more by the data access path and less by user-space compute.
//
// The benchmark:
// - creates a test file if it does not exist
// - scans the file sequentially
// - samples one byte per page (or per configurable stride)
// - computes a checksum so the compiler cannot optimize the loop away
//
// Expected observations:
// - Small read buffer sizes increase syscall overhead for read()
// - mmap() can look attractive when data is already in page cache
// - First-touch page faults matter for mmap()
// - Results depend heavily on page cache and the storage stack
//
// Build output:
//   artifacts/bin/mmap_vs_read
//
// Data output:
//   artifacts/data/testfile.bin
//   artifacts/data/results.csv

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef DEFAULT_FILE_SIZE_MB
#define DEFAULT_FILE_SIZE_MB 256
#endif

#ifndef DEFAULT_READ_CHUNK_KB
#define DEFAULT_READ_CHUNK_KB 256
#endif

#ifndef DEFAULT_STRIDE_BYTES
#define DEFAULT_STRIDE_BYTES 4096
#endif

#ifndef DEFAULT_REPEATS
#define DEFAULT_REPEATS 5
#endif

typedef struct {
    const char *file_path;
    size_t file_size_bytes;
    size_t read_chunk_bytes;
    size_t stride_bytes;
    int repeats;
    int warmup;
    int pin_cpu;
} config_t;

typedef struct {
    const char *mode;
    uint64_t elapsed_ns;
    double mib_per_sec;
    double ns_per_sample;
    uint64_t samples;
    uint64_t checksum;
} result_t;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        die("clock_gettime");
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static size_t parse_size_t_env(const char *name, size_t default_value) {
    const char *s = getenv(name);
    if (!s || *s == '\0') {
        return default_value;
    }

    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid numeric value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (size_t)v;
}

static int parse_int_env(const char *name, int default_value) {
    const char *s = getenv(name);
    if (!s || *s == '\0') {
        return default_value;
    }

    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid integer value for %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static void maybe_pin_cpu(int cpu) {
    if (cpu < 0) {
        return;
    }

#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        die("sched_setaffinity");
    }
#else
    (void)cpu;
#endif
}

static off_t file_size_of_fd(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
        die("fstat");
    }
    return st.st_size;
}

static bool file_exists_with_size(const char *path, size_t wanted_size) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) && (size_t)st.st_size == wanted_size;
}

static void ensure_parent_dirs(void) {
    // We assume the lab's run.sh or Makefile creates artifacts/bin and artifacts/data.
    // This function is intentionally minimal so the C benchmark stays focused on the experiment.
}

static void create_test_file(const char *path, size_t size_bytes) {
    if (file_exists_with_size(path, size_bytes)) {
        return;
    }

    fprintf(stderr,
            "[prepare] creating test file: path=%s size_bytes=%zu\n",
            path, size_bytes);

    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        die("open(create_test_file)");
    }

    // Use a moderately sized temporary buffer to generate a deterministic pattern.
    // The pattern avoids an all-zero file and gives the benchmark something stable
    // to checksum.
    const size_t buf_size = 1 << 20; // 1 MiB
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        close(fd);
        die("malloc(test file buffer)");
    }

    for (size_t i = 0; i < buf_size; i++) {
        buf[i] = (uint8_t)((i * 131u + 17u) & 0xffu);
    }

    size_t remaining = size_bytes;
    while (remaining > 0) {
        size_t chunk = remaining < buf_size ? remaining : buf_size;
        ssize_t written = write(fd, buf, chunk);
        if (written < 0) {
            free(buf);
            close(fd);
            die("write(test file)");
        }
        if ((size_t)written != chunk) {
            free(buf);
            close(fd);
            fprintf(stderr, "Short write while creating test file\n");
            exit(EXIT_FAILURE);
        }
        remaining -= chunk;
    }

    // fsync helps make file creation more explicit and prevents timing surprises
    // if the first benchmark run overlaps with heavy background writeback.
    if (fsync(fd) != 0) {
        free(buf);
        close(fd);
        die("fsync(test file)");
    }

    free(buf);
    close(fd);
}

static result_t bench_read_mode(const config_t *cfg) {
    result_t r;
    memset(&r, 0, sizeof(r));
    r.mode = "read";

    int fd = open(cfg->file_path, O_RDONLY);
    if (fd < 0) {
        die("open(read)");
    }

#ifdef POSIX_FADV_SEQUENTIAL
    // Hint to the kernel that this benchmark is doing sequential access.
    // The kernel may already infer this, but giving the hint makes the intent explicit.
    (void)posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    uint8_t *buf = NULL;
    if (posix_memalign((void **)&buf, 4096, cfg->read_chunk_bytes) != 0) {
        close(fd);
        fprintf(stderr, "posix_memalign failed\n");
        exit(EXIT_FAILURE);
    }

    uint64_t checksum = 0;
    uint64_t samples = 0;

    uint64_t start = now_ns();

    while (1) {
        ssize_t n = read(fd, buf, cfg->read_chunk_bytes);
        if (n < 0) {
            free(buf);
            close(fd);
            die("read");
        }
        if (n == 0) {
            break;
        }

        // Sequentially sample one byte every stride_bytes.
        // This drastically reduces user-space arithmetic so that the benchmark is
        // more about the access path than about computation.
        for (size_t off = 0; off < (size_t)n; off += cfg->stride_bytes) {
            checksum += buf[off];
            samples++;
        }

    }

    uint64_t end = now_ns();

    r.elapsed_ns = end - start;
    r.samples = samples;
    r.checksum = checksum;

    double mib = (double)cfg->file_size_bytes / (1024.0 * 1024.0);
    double sec = (double)r.elapsed_ns / 1e9;
    r.mib_per_sec = mib / sec;
    r.ns_per_sample = (double)r.elapsed_ns / (double)r.samples;

    free(buf);
    close(fd);
    return r;
}

static result_t bench_mmap_mode(const config_t *cfg) {
    result_t r;
    memset(&r, 0, sizeof(r));
    r.mode = "mmap";

    int fd = open(cfg->file_path, O_RDONLY);
    if (fd < 0) {
        die("open(mmap)");
    }

    off_t file_sz = file_size_of_fd(fd);
    if ((size_t)file_sz != cfg->file_size_bytes) {
        fprintf(stderr,
                "File size mismatch: expected=%zu actual=%jd\n",
                cfg->file_size_bytes, (intmax_t)file_sz);
        close(fd);
        exit(EXIT_FAILURE);
    }

    uint8_t *p = mmap(NULL, cfg->file_size_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        die("mmap");
    }

#ifdef MADV_SEQUENTIAL
    // Hint that the mapped region will be scanned sequentially.
    (void)madvise(p, cfg->file_size_bytes, MADV_SEQUENTIAL);
#endif

    uint64_t checksum = 0;
    uint64_t samples = 0;

    uint64_t start = now_ns();

    for (size_t off = 0; off < cfg->file_size_bytes; off += cfg->stride_bytes) {
        checksum += p[off];
        samples++;
    }

    uint64_t end = now_ns();

    r.elapsed_ns = end - start;
    r.samples = samples;
    r.checksum = checksum;

    double mib = (double)cfg->file_size_bytes / (1024.0 * 1024.0);
    double sec = (double)r.elapsed_ns / 1e9;
    r.mib_per_sec = mib / sec;
    r.ns_per_sample = (double)r.elapsed_ns / (double)r.samples;

    if (munmap(p, cfg->file_size_bytes) != 0) {
        close(fd);
        die("munmap");
    }

    close(fd);
    return r;
}

static void print_result_line(const config_t *cfg, int iter, const result_t *r) {
    printf("mode=%s,iter=%d,file_mb=%zu,chunk_kb=%zu,stride=%zu,"
           "elapsed_ns=%" PRIu64 ",samples=%" PRIu64 ",ns_per_sample=%.2f,"
           "mib_per_sec=%.2f,checksum=%" PRIu64 "\n",
           r->mode,
           iter,
           cfg->file_size_bytes / (1024 * 1024),
           cfg->read_chunk_bytes / 1024,
           cfg->stride_bytes,
           r->elapsed_ns,
           r->samples,
           r->ns_per_sample,
           r->mib_per_sec,
           r->checksum);
}

static void print_csv_header(void) {
    printf("mode,iter,file_mb,chunk_kb,stride,elapsed_ns,samples,ns_per_sample,mib_per_sec,checksum\n");
}

static void print_csv_row(const config_t *cfg, int iter, const result_t *r) {
    printf("%s,%d,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%.2f,%.2f,%" PRIu64 "\n",
           r->mode,
           iter,
           cfg->file_size_bytes / (1024 * 1024),
           cfg->read_chunk_bytes / 1024,
           cfg->stride_bytes,
           r->elapsed_ns,
           r->samples,
           r->ns_per_sample,
           r->mib_per_sec,
           r->checksum);
}

int main(void) {
    config_t cfg;
    cfg.file_path = getenv("FILE_PATH") ? getenv("FILE_PATH") : "artifacts/data/testfile.bin";
    cfg.file_size_bytes = parse_size_t_env("FILE_MB", DEFAULT_FILE_SIZE_MB) * 1024ull * 1024ull;
    cfg.read_chunk_bytes = parse_size_t_env("CHUNK_KB", DEFAULT_READ_CHUNK_KB) * 1024ull;
    cfg.stride_bytes = parse_size_t_env("STRIDE_BYTES", DEFAULT_STRIDE_BYTES);
    cfg.repeats = parse_int_env("REPEATS", DEFAULT_REPEATS);
    cfg.warmup = parse_int_env("WARMUP", 1);
    cfg.pin_cpu = parse_int_env("CPU", 0);

    if (cfg.read_chunk_bytes == 0) {
        fprintf(stderr, "CHUNK_KB must be > 0\n");
        return EXIT_FAILURE;
    }
    if (cfg.stride_bytes == 0) {
        fprintf(stderr, "STRIDE_BYTES must be > 0\n");
        return EXIT_FAILURE;
    }
    if (cfg.repeats <= 0) {
        fprintf(stderr, "REPEATS must be > 0\n");
        return EXIT_FAILURE;
    }

    maybe_pin_cpu(cfg.pin_cpu);
    ensure_parent_dirs();
    create_test_file(cfg.file_path, cfg.file_size_bytes);

    fprintf(stderr,
            "[config] file=%s file_mb=%zu chunk_kb=%zu stride=%zu repeats=%d warmup=%d cpu=%d\n",
            cfg.file_path,
            cfg.file_size_bytes / (1024 * 1024),
            cfg.read_chunk_bytes / 1024,
            cfg.stride_bytes,
            cfg.repeats,
            cfg.warmup,
            cfg.pin_cpu);

    // Optional warmup phase:
    // This is intentionally explicit because many file benchmarks are dominated
    // by "was the data already in page cache?" rather than by the API itself.
    if (cfg.warmup) {
        result_t w1 = bench_read_mode(&cfg);
        result_t w2 = bench_mmap_mode(&cfg);

        fprintf(stderr,
                "[warmup] read_elapsed_ns=%" PRIu64 " mmap_elapsed_ns=%" PRIu64 "\n",
                w1.elapsed_ns, w2.elapsed_ns);
    }

    print_csv_header();

    for (int i = 0; i < cfg.repeats; i++) {
        result_t rr = bench_read_mode(&cfg);
        result_t rm = bench_mmap_mode(&cfg);

        print_csv_row(&cfg, i, &rr);
        print_csv_row(&cfg, i, &rm);

        // Also print a human-readable form to stderr if desired during debugging.
        // Kept disabled in normal runs to preserve clean CSV on stdout.
        // print_result_line(&cfg, i, &rr);
        // print_result_line(&cfg, i, &rm);
    }

    return 0;
}
