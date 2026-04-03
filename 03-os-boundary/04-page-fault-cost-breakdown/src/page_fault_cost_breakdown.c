#define _POSIX_C_SOURCE 200809L
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
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

typedef enum {
    MODE_ANON_READ_FIRST = 0,
    MODE_ANON_WRITE_FIRST,
    MODE_ANON_READ_SECOND,
    MODE_FILE_READ_FIRST,
    MODE_FILE_READ_SECOND,
    MODE_FILE_WRITE_PRIVATE_FIRST,
    MODE_FILE_READ_POPULATE,
    MODE_COUNT
} mode_t_experiment;

typedef struct {
    const char *name;
    uint64_t elapsed_ns;
    long minflt_delta;
    long majflt_delta;
    uint64_t checksum;
} result_t;

typedef struct {
    size_t pages;
    size_t page_size;
    int repeats;
    int warmup;
    int pin_cpu;
    const char *csv_path;
    const char *backing_file_path;
} config_t;

static volatile uint64_t g_sink = 0;

static void die_perror(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  --pages N              Number of pages to touch (default: 4096)\n"
            "  --page-size N          Page size in bytes (default: system page size)\n"
            "  --repeats N            Number of measured repeats (default: 3)\n"
            "  --warmup N             Number of warmup runs per mode (default: 1)\n"
            "  --pin-cpu N            Pin process to CPU N, -1 disables pinning (default: -1)\n"
            "  --csv PATH             CSV output path\n"
            "  --backing-file PATH    Backing file path for file-backed modes\n"
            "  --help                 Show this message\n",
            prog);
    exit(EXIT_FAILURE);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        die_perror("clock_gettime");
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu_if_requested(int cpu) {
    if (cpu < 0) {
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET((unsigned)cpu, &set);

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        die_perror("sched_setaffinity");
    }
}

static long rusage_minflt(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        die_perror("getrusage");
    }
    return ru.ru_minflt;
}

static long rusage_majflt(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        die_perror("getrusage");
    }
    return ru.ru_majflt;
}

static size_t parse_size(const char *s) {
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid numeric value: %s\n", s);
        exit(EXIT_FAILURE);
    }
    return (size_t)v;
}

static int parse_int(const char *s) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid integer value: %s\n", s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static void ensure_parent_dirs_for_file(const char *path) {
    char *tmp = strdup(path);
    if (!tmp) {
        die_perror("strdup");
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                die_perror("mkdir");
            }
            *p = '/';
        }
    }

    free(tmp);
}

static void fill_pattern(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)((i * 131u + 17u) & 0xffu);
    }
}

static void create_backing_file(const char *path, size_t total_bytes, size_t page_size) {
    ensure_parent_dirs_for_file(path);

    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        die_perror("open backing file");
    }

    if (ftruncate(fd, (off_t)total_bytes) != 0) {
        close(fd);
        die_perror("ftruncate");
    }

    uint8_t *buf = malloc(page_size);
    if (!buf) {
        close(fd);
        die_perror("malloc");
    }

    fill_pattern(buf, page_size);

    size_t written = 0;
    while (written < total_bytes) {
        size_t chunk = page_size;
        if (chunk > total_bytes - written) {
            chunk = total_bytes - written;
        }

        ssize_t rc = pwrite(fd, buf, chunk, (off_t)written);
        if (rc < 0 || (size_t)rc != chunk) {
            free(buf);
            close(fd);
            die_perror("pwrite");
        }
        written += chunk;
    }

    if (fsync(fd) != 0) {
        free(buf);
        close(fd);
        die_perror("fsync");
    }

    free(buf);
    close(fd);
}

static uint64_t touch_read_pages(uint8_t *p, size_t pages, size_t page_size) {
    uint64_t sum = 0;
    for (size_t i = 0; i < pages; i++) {
        sum += p[i * page_size];
    }
    return sum;
}

static uint64_t touch_write_pages(uint8_t *p, size_t pages, size_t page_size) {
    uint64_t sum = 0;
    for (size_t i = 0; i < pages; i++) {
        size_t off = i * page_size;
        p[off] = (uint8_t)(p[off] + 1u);
        sum += p[off];
    }
    return sum;
}

static void best_effort_drop_file_cache(int fd) {
    if (fdatasync(fd) != 0) {
        /* best effort only */
    }
}

static result_t run_mode_anon_read_first(size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "anon_read_first";

    size_t total_bytes = pages * page_size;
    uint8_t *p = mmap(NULL, total_bytes, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        die_perror("mmap anon read");
    }

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_read_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        die_perror("munmap anon read");
    }

    return r;
}

static result_t run_mode_anon_write_first(size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "anon_write_first";

    size_t total_bytes = pages * page_size;
    uint8_t *p = mmap(NULL, total_bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        die_perror("mmap anon write");
    }

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_write_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        die_perror("munmap anon write");
    }

    return r;
}

static result_t run_mode_anon_read_second(size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "anon_read_second";

    size_t total_bytes = pages * page_size;
    uint8_t *p = mmap(NULL, total_bytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        die_perror("mmap anon read second");
    }

    (void)touch_write_pages(p, pages, page_size);

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_read_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        die_perror("munmap anon read second");
    }

    return r;
}

static result_t run_mode_file_read_first(const char *path, size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "file_read_first";

    size_t total_bytes = pages * page_size;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        die_perror("open file_read_first");
    }

    best_effort_drop_file_cache(fd);

    uint8_t *p = mmap(NULL, total_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        die_perror("mmap file_read_first");
    }

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_read_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        close(fd);
        die_perror("munmap file_read_first");
    }
    close(fd);

    return r;
}

static result_t run_mode_file_read_second(const char *path, size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "file_read_second";

    size_t total_bytes = pages * page_size;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        die_perror("open file_read_second");
    }

    uint8_t *p = mmap(NULL, total_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        die_perror("mmap file_read_second");
    }

    (void)touch_read_pages(p, pages, page_size);

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_read_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        close(fd);
        die_perror("munmap file_read_second");
    }
    close(fd);

    return r;
}

static result_t run_mode_file_write_private_first(const char *path, size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "file_write_private_first";

    size_t total_bytes = pages * page_size;
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        die_perror("open file_write_private_first");
    }

    best_effort_drop_file_cache(fd);

    uint8_t *p = mmap(NULL, total_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        die_perror("mmap file_write_private_first");
    }

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_write_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        close(fd);
        die_perror("munmap file_write_private_first");
    }
    close(fd);

    return r;
}

static result_t run_mode_file_read_populate(const char *path, size_t pages, size_t page_size) {
    result_t r = {0};
    r.name = "file_read_populate";

    size_t total_bytes = pages * page_size;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        die_perror("open file_read_populate");
    }

    best_effort_drop_file_cache(fd);

    uint8_t *p = mmap(NULL, total_bytes, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        die_perror("mmap file_read_populate");
    }

    long min_before = rusage_minflt();
    long maj_before = rusage_majflt();
    uint64_t t0 = now_ns();

    r.checksum = touch_read_pages(p, pages, page_size);

    uint64_t t1 = now_ns();
    long min_after = rusage_minflt();
    long maj_after = rusage_majflt();

    r.elapsed_ns = t1 - t0;
    r.minflt_delta = min_after - min_before;
    r.majflt_delta = maj_after - maj_before;

    if (munmap(p, total_bytes) != 0) {
        close(fd);
        die_perror("munmap file_read_populate");
    }
    close(fd);

    return r;
}

static result_t run_one_mode(mode_t_experiment mode, const config_t *cfg) {
    switch (mode) {
        case MODE_ANON_READ_FIRST:
            return run_mode_anon_read_first(cfg->pages, cfg->page_size);
        case MODE_ANON_WRITE_FIRST:
            return run_mode_anon_write_first(cfg->pages, cfg->page_size);
        case MODE_ANON_READ_SECOND:
            return run_mode_anon_read_second(cfg->pages, cfg->page_size);
        case MODE_FILE_READ_FIRST:
            return run_mode_file_read_first(cfg->backing_file_path, cfg->pages, cfg->page_size);
        case MODE_FILE_READ_SECOND:
            return run_mode_file_read_second(cfg->backing_file_path, cfg->pages, cfg->page_size);
        case MODE_FILE_WRITE_PRIVATE_FIRST:
            return run_mode_file_write_private_first(cfg->backing_file_path, cfg->pages, cfg->page_size);
        case MODE_FILE_READ_POPULATE:
            return run_mode_file_read_populate(cfg->backing_file_path, cfg->pages, cfg->page_size);
        default:
            fprintf(stderr, "Unknown mode\n");
            exit(EXIT_FAILURE);
    }
}

static void write_csv_header(FILE *fp) {
    fprintf(fp,
            "mode,pages,page_size,total_bytes,repeats,warmup,pin_cpu,"
            "run_index,elapsed_ns,minflt_delta,majflt_delta,ns_per_page,checksum\n");
}

int main(int argc, char **argv) {
    config_t cfg;
    cfg.pages = 4096;
    cfg.page_size = (size_t)sysconf(_SC_PAGESIZE);
    cfg.repeats = 3;
    cfg.warmup = 1;
    cfg.pin_cpu = -1;
    cfg.csv_path = "artifacts/data/page_fault_cost_breakdown.csv";
    cfg.backing_file_path = "artifacts/data/page_fault_backing.bin";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pages") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.pages = parse_size(argv[i]);
        } else if (strcmp(argv[i], "--page-size") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.page_size = parse_size(argv[i]);
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.repeats = parse_int(argv[i]);
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.warmup = parse_int(argv[i]);
        } else if (strcmp(argv[i], "--pin-cpu") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.pin_cpu = parse_int(argv[i]);
        } else if (strcmp(argv[i], "--csv") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.csv_path = argv[i];
        } else if (strcmp(argv[i], "--backing-file") == 0) {
            if (++i >= argc) die_usage(argv[0]);
            cfg.backing_file_path = argv[i];
        } else if (strcmp(argv[i], "--help") == 0) {
            die_usage(argv[0]);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            die_usage(argv[0]);
        }
    }

    if (cfg.pages == 0 || cfg.page_size == 0 || cfg.repeats <= 0 || cfg.warmup < 0) {
        fprintf(stderr, "Invalid configuration values\n");
        return EXIT_FAILURE;
    }

    pin_to_cpu_if_requested(cfg.pin_cpu);

    size_t total_bytes = cfg.pages * cfg.page_size;

    ensure_parent_dirs_for_file(cfg.csv_path);
    ensure_parent_dirs_for_file(cfg.backing_file_path);
    create_backing_file(cfg.backing_file_path, total_bytes, cfg.page_size);

    FILE *fp = fopen(cfg.csv_path, "w");
    if (!fp) {
        die_perror("fopen csv");
    }

    write_csv_header(fp);

    printf("[summary] pages=%zu page_size=%zu total_bytes=%zu repeats=%d warmup=%d pin_cpu=%d\n",
           cfg.pages, cfg.page_size, total_bytes, cfg.repeats, cfg.warmup, cfg.pin_cpu);
    printf("[summary] csv=%s\n", cfg.csv_path);
    printf("[summary] backing_file=%s\n", cfg.backing_file_path);

    for (int mode = 0; mode < MODE_COUNT; mode++) {
        for (int w = 0; w < cfg.warmup; w++) {
            result_t warm = run_one_mode((mode_t_experiment)mode, &cfg);
            g_sink ^= warm.checksum;
        }

        for (int r = 0; r < cfg.repeats; r++) {
            result_t res = run_one_mode((mode_t_experiment)mode, &cfg);
            g_sink ^= res.checksum;

            double ns_per_page = (double)res.elapsed_ns / (double)cfg.pages;

            fprintf(fp,
                    "%s,%zu,%zu,%zu,%d,%d,%d,%d,%" PRIu64 ",%ld,%ld,%.2f,%" PRIu64 "\n",
                    res.name,
                    cfg.pages,
                    cfg.page_size,
                    total_bytes,
                    cfg.repeats,
                    cfg.warmup,
                    cfg.pin_cpu,
                    r,
                    res.elapsed_ns,
                    res.minflt_delta,
                    res.majflt_delta,
                    ns_per_page,
                    res.checksum);

            printf("[run] mode=%s run=%d elapsed_ns=%" PRIu64
                   " minflt=%ld majflt=%ld ns_per_page=%.2f checksum=%" PRIu64 "\n",
                   res.name,
                   r,
                   res.elapsed_ns,
                   res.minflt_delta,
                   res.majflt_delta,
                   ns_per_page,
                   res.checksum);
        }
    }

    fclose(fp);

    printf("[sink] %" PRIu64 "\n", g_sink);
    printf("[done] output: %s\n", cfg.csv_path);
    return 0;
}
