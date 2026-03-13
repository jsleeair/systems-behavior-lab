#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Goal
 * Demonstrate Copy-on-Write (COW) behavior after fork().
 *
 * High-level idea
 * 1. Allocate a large anonymous private buffer.
 * 2. Touch every page in the parent so pages are actually materialized.
 * 3. fork().
 * 4. In the child:
 * 	- "read" mode: read one byte from each page
 * 	- "write" mode: write one byte to each page
 * 5. Measure elapsed time and page faults in the child.
 *
 * Why this works
 * After fork(), the child logically sees the parent's address space,
 * but the kernel avoids copying all pages immediately.
 * Instead, parent and child share physical pages until one side writes.
 *
 * - Read-only access should usually avoid COW copies.
 * - Write access should trigger page faults and private page copies,
 *   typically one per touched page.
 *
 * Notes
 * - We use mmap(..., MAP_PRIVATE | MAP_ANONYMOUSE, ...) so the buffer is
 *   a private anonymous mapping suitable for observing COW after fork().
 * - We pre-touch pages in the parent before fork() to separate:
 *   	"first allocation / first-touch cost"
 * - We collect ru_minflt / ru_majflt using getrusage(RUSAGE_SELF).
 *   COW-related faults usually show up as minor faults, not major faults.
 * - The child prints one CSV line so run.sh can aggregate results.
 */

typedef struct {
	const char *mode;	/* "read" or "write" */
	size_t pages;		/* number of pages in the test buffer */
	size_t page_size;	/* system page size in bytes */
	size_t total_bytes;	/* pages * page_size */
	int warmup;		/* whether parent pre-touches every pages */
	int pin_cpu;		/* target CPU, -1 means no pinning */
	uint64_t elapsed_ns;	/* elapsed time inside child critical section */
	long minflt_delta;	/* child minor page faults during the measured section */
	long majflt_delta;	/* child major page faults during the measured section */
	double ns_per_page;	/* elapsed_ns / pages */
} result_t;

/* Return monotonic time in nanosecons. */
static uint64_t now_ns(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(1);
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/*
 * Pin the current thread/process to a specific CPU.
 * This is optional, but helps reduce scheduler noise.
 */
static void pin_to_cpu_if_requested(int cpu) {
	if (cpu < 0) {
		return;
	}

	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		fprintf(stderr, "warning: sched_setaffinity(cpu=%d) failed: %s\n", cpu, strerror(errno));
	}
}

/*
 * Parse a non-negative integer from an environment variable.
 * If missing, return default_value.
 */
static long env_long(const char *name, long default_value) {
	const char *s = getenv(name);
	if (!s || !*s) {
		return default_value;
	}

	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0') {
		fprintf(stderr, "invalid value for %s: %s\n", name, s);
		exit(1);
	}
	return v;
}

/*
 * Touch every page in the mapping in the parent before fork().
 * We write one byte per page to ensure the page is backed and present.
 *
 * This intentionally removs first-touch allocation noise from the child-side 
 * measurement, so the post-fork write case primarlily reflects COW behavior.
 */
static void warmup_pages(uint8_t *buf, size_t pages, size_t page_size) {
	for (size_t i = 0; i < pages; ++i) {
		buf[i * page_size] = (uint8_t)(i & 0xff);
	}
}

/* 
 * Read one byte from each page.
 * The volatile accumulator prevents the compiler from optimizing away the loop.
 */
static uint64_t touch_read_each_page(const uint8_t *buf, size_t pages, size_t page_size) {
	volatile uint64_t sum = 0;
	for (size_t i = 0; i < pages; ++i) {
		sum += buf[i * page_size];
	}
	return sum;
}

/*
 * Write one byte to each page.
 * This is the key operation that should trigger Copy-on-Write after fork().
 * 
 * We modify the first byte of each page only once, because a single write is
 * enough to force the kernel to give the child a private copy of that page.
 */
static void touch_write_each_page(uint8_t *buf, size_t pages, size_t page_size) {
	for (size_t i = 0; i < pages; ++i) {
		buf[i * page_size] ^= 1u;
	}
}

/*
 * Execute the measured access pattern inside the child.
 * Returns a populated result_t.
 */
static result_t run_child(const char *mode,
			uint8_t *buf,
			size_t pages,
			size_t page_size,
			int warmup,
			int pin_cpu) {
	struct rusage ru_before, ru_after;
	result_t r;
	memset(&r, 0, sizeof(r));

	r.mode = mode;
	r.pages = pages;
	r.page_size = page_size;
	r.total_bytes = pages * page_size;
	r.warmup = warmup;
	r.pin_cpu = pin_cpu;

	pin_to_cpu_if_requested(pin_cpu);

	if (getrusage(RUSAGE_SELF, &ru_before) != 0) {
		perror("getrusage(before)");
		exit(1);
	}

	uint64_t t0 = now_ns();

	if (strcmp(mode, "read") == 0) {
		/*
		 * We keep the return value in a local variable and use a branch that
		 * will never realistically trigger, purely to preserve the side effect.
		 */
		uint64_t sum = touch_read_each_page(buf, pages, page_size);
		if (sum == UINT64_MAX) {
			fprintf(stderr, "impossible sum=%" PRIu64 "\n", sum);
		}
	} else if (strcmp(mode, "write") == 0) {
		touch_write_each_page(buf, pages, page_size);
	} else {
		fprintf(stderr, "unknown mode:%s\n", mode);
		exit(1);
	}
	
	uint64_t t1 = now_ns();

	if (getrusage(RUSAGE_SELF, &ru_after) != 0) {
		perror("getrusage(after)");
		exit(1);
	}

	r.elapsed_ns = t1 - t0;
	r.minflt_delta = ru_after.ru_minflt - ru_before.ru_minflt;
	r.majflt_delta = ru_after.ru_majflt - ru_before.ru_majflt;
	r.ns_per_page = (pages == 0) ? 0.0 : (double)r.elapsed_ns / (double)pages;

	return r;
}

/* Print one CSV record */
static void print_csv_row(const result_t *r) {
	printf("%s,%zu,%zu,%zu,%d,%d,%" PRIu64 ",%ld,%ld,%.2f\n",
		r->mode,
		r->pages,
		r->page_size,
		r->total_bytes,
		r->warmup,
		r->pin_cpu,
		r->elapsed_ns,
		r->minflt_delta,
		r->majflt_delta,
		r->ns_per_page);
	fflush(stdout);
}

/*
 * Program usage:
 *   ./artifacts/bin/cow <mode>
 *
 * mode:
 *   read   - read one byte per page in the child
 *   write  - write one byte per page in the child
 *
 * Envinronment variables:
 *   PAGES=<n>		number of pages to test		(default: 1024)
 *   WARMUP=<0|1>	pre-touch in parent before fork	(default: 1)
 *   CPU=<n>		pin process to CPU n		(default: -1, no pinning)
 *
 * Example:
 *   PAGES=4096 WARMUP=1 CPU=0 ./artifacts/bin/cow write
 */
int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr,
			"usage: %s <read|write?\n"
			"env: PAGES=<n> WARMUP=<0|1> CPU=<n>\n",
			argv[0]);
		return 1;
	}

	const char *mode = argv[1];
	if (strcmp(mode, "read") != 0 && strcmp(mode, "write") != 0) {
		fprintf(stderr, "mode must be 'read' or 'write'\n");
		return 1;
	}

	long page_size_l = sysconf(_SC_PAGESIZE);
	if (page_size_l <= 0) {
		perror("sysconf(_SC_PAGESIZE)");
		return 1;
	}
	size_t page_size = (size_t)page_size_l;

	long pages_l = env_long("PAGES", 1024);
	long warmup_l = env_long("WARMUP", 1);
	long cpu_l = env_long("CPU", -1);
	if (cpu_l < -1) {
		fprintf(stderr, "CPU must be >= -1\n");
		exit(1);
	}

	if (pages_l <= 0) {
		fprintf(stderr, "PAGES must be > 0\n");
		return 1;
	}
	if (!(warmup_l == 0 || warmup_l == 1)) {
		fprintf(stderr, "WARMUP must be 0 or 1\n");
		return 1;
	}

	size_t pages = (size_t)pages_l;
	size_t total_bytes = pages * page_size;

	/*
	 * Create a private anonymous mapping.
	 *
	 * MAP_PRIVATE:
	 *   Changes are private to the process and appropriate for COW semantics.
	 *
	 * MAP_ANONYMOUS:
	 *   The mappint is not backed by a file.
	 */
	uint8_t *buf = mmap(NULL,
			    total_bytes,
			    PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS,
			    -1,
			    0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	pin_to_cpu_if_requested((int)cpu_l);

	if (warmup_l) {
		warmup_pages(buf, pages, page_size);
	}

	/*
	 * Fork the process.
	 *
	 * After this point, parent and child logically have the same address space.
	 * Physically, the kernel tries to avoid copying pages immediately.
	 */
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		munmap(buf, total_bytes);
		return 1;
	}

	if (pid == 0) {
		/* Child: perform the measured access pattern and print a single CSV row. */
		result_t r = run_child(mode, buf, pages, page_size, (int)warmup_l, (int)cpu_l);
		print_csv_row(&r);

		if (munmap(buf, total_bytes) != 0) {
			perror("munmap(child)");
			exit(1);
		}
		exit(0);
	}

	/* Parent: wait for the child so the shell sees a clean completion. */
	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		munmap(buf, total_bytes);
		return 1;
	}

	if (munmap(buf, total_bytes) != 0) {
		perror("munmap(parent)");
		return 1;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "child failed\n");
		return 1;
	}

	return 0;
}
	
