#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include "util.h"

/*
TLB Effect Microbenchmark (Linux, x86_64)

Goal:
  - Make memory access patterns that stress the TLB (address translation cache),
    not just the data cache.
  - Vary the number of pages touched to observe a "knee" where performance
    drops due to increased TLB misses.

Key idea:
  - If we touch 1 word per page across N pages, then our working set for
    translation is ~N pages.
  - When N exceeds the effective TLB reach (entries * page size), we should
    see extra latency due to more frequent page table walks.

Important notes (microbenchmark hygiene):
  - Pin to one CPU core to reduce scheduling noise.
  - Use a pointer-chasing loop across pages to reduce hardware prefetch benefits.
  - Warm up (touch pages first) to avoid measuring first-touch page faults.
  - Measure cycles (TSC) per access.
  - Keep results in CSV-friendly format.

Options:
  - You can request Transparent Huge Pages (THP) via madvise(MADV_HUGEPAGE).
    This may or may not take effect depending on kernel settings.
*/

typedef struct {
    int cpu;                 // CPU core to pin to
    size_t min_pages;        // minimum pages in experiment
    size_t max_pages;        // maximum pages in experiment
    size_t step_pages;       // increment of pages
    size_t iters;            // number of pointer-chase steps per measurement
    int use_thp_hint;        // madvise(MADV_HUGEPAGE)
    int randomize;           // randomize page order
    size_t warmup;              // warmup steps
    int csv;                 // print CSV only
} opts_t;

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --cpu N            Pin to CPU core N (default: 1)\n"
        "  --min-pages N      Minimum number of pages (default: 16)\n"
        "  --max-pages N      Maximum number of pages (default: 65536)\n"
        "  --step-pages N     Step in pages (default: 16)\n"
        "  --iters N          Pointer-chase steps per measurement (default: 20000000)\n"
        "  --warmup N         Warmup steps (default: 2000000)\n"
        "  --thp              Hint kernel to use Transparent Huge Pages (MADV_HUGEPAGE)\n"
        "  --no-rand          Do not randomize page order\n"
        "  --csv              Output CSV only\n"
        "\n"
        "Example:\n"
        "  %s --cpu 1 --min-pages 16 --max-pages 32768 --step-pages 64 --iters 30000000 --csv\n",
        prog, prog
    );
}

static int parse_args(int argc, char** argv, opts_t* o) {
    // Defaults tuned for a quick but visible run.
    o->cpu = 1;
    o->min_pages = 16;
    o->max_pages = 65536;
    o->step_pages = 16;
    o->iters = 20000000;
    o->use_thp_hint = 0;
    o->randomize = 1;
    o->warmup = 2000000;
    o->csv = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cpu") && i + 1 < argc) {
            o->cpu = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--min-pages") && i + 1 < argc) {
            o->min_pages = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--max-pages") && i + 1 < argc) {
            o->max_pages = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--step-pages") && i + 1 < argc) {
            o->step_pages = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--iters") && i + 1 < argc) {
            o->iters = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) {
            o->warmup = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--thp")) {
            o->use_thp_hint = 1;
        } else if (!strcmp(argv[i], "--no-rand")) {
            o->randomize = 0;
        } else if (!strcmp(argv[i], "--csv")) {
            o->csv = 1;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }

    if (o->min_pages == 0 || o->max_pages < o->min_pages || o->step_pages == 0) {
        fprintf(stderr, "Invalid pages range.\n");
        return -1;
    }
    if (o->iters < 1000) {
        fprintf(stderr, "iters too small; increase for stable measurement.\n");
        return -1;
    }
    return 0;
}

static void shuffle_u32(uint32_t* a, size_t n, uint64_t seed) {
    // Fisher-Yates shuffle
    // We use a very simple LCG for deterministic "good enough" shuffling.
    uint64_t x = seed ? seed : 88172645463393265ull;
    for (size_t i = n - 1; i > 0; i--) {
        x = x * 2862933555777941757ull + 3037000493ull;
        size_t j = (size_t)(x % (i + 1));
        uint32_t tmp = a[i];
        a[i] = a[j];
        a[j] = tmp;
    }
}

static inline uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char** argv) {
    opts_t opt;
    int pr = parse_args(argc, argv, &opt);
    if (pr != 0) return (pr > 0) ? 0 : 1;

    try_raise_priority();
    if (pin_to_cpu(opt.cpu) != 0) {
        // Not fatal, but results may be noisier.
    }

    const size_t page_size = get_page_size();

    if (!opt.csv) {
        printf("# TLB Effect Lab\n");
        printf("# page_size=%zu bytes\n", page_size);
        printf("# pin_cpu=%d\n", opt.cpu);
        printf("# iters=%zu warmup=%zu randomize=%d thp_hint=%d\n",
               opt.iters, opt.warmup, opt.randomize, opt.use_thp_hint);
        printf("#\n");
        printf("# Columns: pages, bytes, cycles_per_access, ns_per_access\n");
    } else {
        printf("pages,bytes,cycles_per_access,ns_per_access\n");
    }

    // We allocate the maximum required space once, then reuse prefixes.
    size_t max_bytes = opt.max_pages * page_size;

    // Anonymous private mapping (demand-paged).
    uint8_t* buf = mmap(NULL, max_bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "mmap(%zu) failed: %s\n", max_bytes, strerror(errno));
        return 1;
    }

    if (opt.use_thp_hint) {
        // This is only a hint. The kernel may ignore it.
        if (madvise(buf, max_bytes, MADV_HUGEPAGE) != 0) {
            // Not fatal.
        }
    }

    // Build an array of page indices [0..max_pages-1].
    uint32_t* pages = (uint32_t*)malloc(opt.max_pages * sizeof(uint32_t));
    if (!pages) {
        fprintf(stderr, "malloc pages failed\n");
        return 1;
    }
    for (size_t i = 0; i < opt.max_pages; i++) pages[i] = (uint32_t)i;

    // Touch every page once up to max to make sure it's mapped (avoid page faults during timing).
    // We write one byte per page.
    for (size_t i = 0; i < opt.max_pages; i++) {
        buf[i * page_size] = (uint8_t)(i & 0xFF);
    }

    // We'll store "next pointers" inside each page to create a pointer-chasing ring.
    // Each node lives at the beginning of a page.
    // Access pattern: p = *(uint8_t**)p; repeated.
    for (size_t n_pages = opt.min_pages; n_pages <= opt.max_pages; n_pages += opt.step_pages) {
        // Optionally shuffle the first n_pages indices to defeat prefetch and make the pattern more TLB-driven.
        if (opt.randomize && n_pages > 1) {
            // Reinitialize [0..n_pages-1], then shuffle.
            for (size_t i = 0; i < n_pages; i++) pages[i] = (uint32_t)i;

            // Seed with time + n_pages so different sizes produce different rings.
            uint64_t seed = ns_now() ^ (uint64_t)n_pages * 0x9e3779b97f4a7c15ull;
            shuffle_u32(pages, n_pages, seed);
        } else {
            for (size_t i = 0; i < n_pages; i++) pages[i] = (uint32_t)i;
        }

        // Build a cyclic linked list across pages.
        // Each page start stores a pointer to the next page start.
        for (size_t i = 0; i < n_pages; i++) {
            uint32_t cur = pages[i];
            uint32_t nxt = pages[(i + 1) % n_pages];
            uint8_t** cur_ptr = (uint8_t**)(buf + (size_t)cur * page_size);
            *cur_ptr = (uint8_t*)(buf + (size_t)nxt * page_size);
        }

        // Warm up: chase pointers to populate TLB/cache to a steady state.
        volatile uint8_t* p = buf + (size_t)pages[0] * page_size;
        for (size_t i = 0; i < opt.warmup; i++) {
            p = *(uint8_t* const*)p;
        }

        // Timed measurement (cycles + ns).
        uint64_t t0c = rdtsc_ordered();
        uint64_t t0n = ns_now();

        for (size_t i = 0; i < opt.iters; i++) {
            p = *(uint8_t* const*)p;
        }

        uint64_t t1n = ns_now();
        uint64_t t1c = rdtsc_ordered();

        // Prevent compiler from optimizing away the loop by "using" p.
        // This write should be in-cache and not affect the measurement much.
        *(volatile uint8_t*)p ^= 1;

        double cycles = (double)(t1c - t0c);
        double nsec   = (double)(t1n - t0n);
        double cycles_per = cycles / (double)opt.iters;
        double ns_per     = nsec / (double)opt.iters;

        size_t bytes = n_pages * page_size;

        if (!opt.csv) {
            printf("%zu,%zu,%.3f,%.3f\n", n_pages, bytes, cycles_per, ns_per);
        } else {
            printf("%zu,%zu,%.3f,%.3f\n", n_pages, bytes, cycles_per, ns_per);
        }
        fflush(stdout);
    }

    munmap(buf, max_bytes);
    free(pages);
    return 0;
}
