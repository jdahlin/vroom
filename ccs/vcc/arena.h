/*
 * vmreg: anonymous regions kept by a thread from one compile job to the next.
 *
 * A job used to mmap its tables (MAP_NORESERVE, gigabytes of address space)
 * and munmap them at the end: two syscalls per table and a page fault for
 * every page it touched, every job.  A vmreg is mapped once per thread and
 * handed back with vmreg_put, which zeroes the bytes the job may have written
 * (memset of dirty pages: ~0.15 us per 16 KB page against ~1 us for a fault),
 * so the next job on the thread starts on zero, already-faulted memory.
 *
 *   static _Thread_local vmreg r;
 *   char *p = vmreg_get(&r, n, "name");	// >= n bytes, all zero
 *   ...
 *   vmreg_put(&r, used);			// bytes [0, used) may be dirty
 *   vmreg_put(&r, VMREG_SCRATCH);		// contents need not be zero next time
 *
 * A scratch region is one the code always writes before it reads (stacks,
 * per-function buffers): it is handed back as is.  Beyond VMREG_KEEP bytes a
 * zeroed region gets fresh pages (one mmap) instead, so a huge job does not
 * leave the thread holding its memory.
 *
 * Debugging (environment, read once): VCC_VMREG_CHECK=1 verifies at every put
 * that no resident page past `used` holds a nonzero byte (a wrong hint) and
 * fills the resident pages of scratch regions with 0xa5 (a wrong scratch
 * claim then changes the output); VCC_VMREG_STATS=1 prints per region the
 * resident bytes handed back.  Both use mincore and are slow.
 */
#ifndef VCC_ARENA_H
#define VCC_ARENA_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct vmreg {
	char *p;
	size_t cap;
	const char *name;
	int busy;	/* got, not yet put (a job that never put it: map afresh) */
} vmreg;

#define VMREG_SCRATCH ((size_t)-1)
#define VMREG_KEEP ((size_t)64 << 20)
#define VMREG_PAGE ((size_t)16384)	/* arm64 macOS; a smaller real page only means more zeroing */

static inline void *vmreg_map(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

/* ---- debugging: VCC_VMREG_CHECK / VCC_VMREG_STATS ---- */

static int vmreg_dbg_mode = -1;	/* bit 0 check, bit 1 stats */

static struct { const char *name; unsigned long long res, puts, bad; } vmreg_dbg_tab[64];
static pthread_mutex_t vmreg_dbg_mu = PTHREAD_MUTEX_INITIALIZER;

static void vmreg_dbg_report(void)
{
	for (int i = 0; i < 64 && vmreg_dbg_tab[i].name; i++)
		fprintf(stderr, "vmreg %-14s %6llu puts, %9.1f MB resident at put, %llu bad\n", vmreg_dbg_tab[i].name,
			vmreg_dbg_tab[i].puts, vmreg_dbg_tab[i].res / 1e6, vmreg_dbg_tab[i].bad);
}

static int vmreg_dbg(void)
{
	if (__builtin_expect(vmreg_dbg_mode < 0, 0)) {
		int m = (getenv("VCC_VMREG_CHECK") ? 1 : 0) | (getenv("VCC_VMREG_STATS") ? 2 : 0);

		if (m & 2)
			atexit(vmreg_dbg_report);
		vmreg_dbg_mode = m;
	}
	return vmreg_dbg_mode;
}

__attribute__((noinline, cold)) static void vmreg_dbg_put(vmreg *r, size_t used)
{
	size_t pg = (size_t)getpagesize(), lim = r->cap < ((size_t)8 << 30) ? r->cap : ((size_t)8 << 30);
	size_t np = lim / pg, res = 0, bad = 0;
	char *vec = malloc(np);
	int i;

	mincore(r->p, lim, vec);
	for (size_t k = 0; k < np; k++) {
		char *pp = r->p + k * pg;

		if (!(vec[k] & 1))
			continue;
		res += pg;
		if (used == VMREG_SCRATCH) {
			if (vmreg_dbg_mode & 1)
				memset(pp, 0xa5, pg);
		} else if ((vmreg_dbg_mode & 1) && k * pg + pg > used) {
			size_t from = k * pg < used ? used - k * pg : 0;

			for (size_t j = from; j < pg; j++)
				if (pp[j]) {
					if (!bad)
						fprintf(stderr, "vmreg %s: nonzero byte at %zu, past used %zu\n", r->name,
							k * pg + j, used);
					bad++;
					break;
				}
		}
	}
	free(vec);
	pthread_mutex_lock(&vmreg_dbg_mu);
	for (i = 0; i < 63 && vmreg_dbg_tab[i].name && strcmp(vmreg_dbg_tab[i].name, r->name); i++)
		;
	vmreg_dbg_tab[i].name = r->name;
	vmreg_dbg_tab[i].res += res;
	vmreg_dbg_tab[i].puts++;
	vmreg_dbg_tab[i].bad += bad;
	pthread_mutex_unlock(&vmreg_dbg_mu);
	if (bad && (vmreg_dbg_mode & 1))
		abort();
}

/* ---- the regions ---- */

/* at least n bytes, zero (or, for a scratch region, whatever the last job left) */
static inline void *vmreg_get(vmreg *r, size_t n, const char *name)
{
	if (__builtin_expect(r->cap < n || r->busy, 0)) {
		if (r->p)
			munmap(r->p, r->cap);
		n = (n + VMREG_PAGE - 1) & ~(VMREG_PAGE - 1);
		if (r->cap < n)	/* sizes that grow with the TU: double */
			r->cap = r->cap * 2 > n ? r->cap * 2 : n;
		r->p = vmreg_map(r->cap);
	}
	r->name = name;
	r->busy = 1;
	return r->p;
}

/* the job is done with it; [0, used) may have been written (VMREG_SCRATCH: keep as is) */
static inline void vmreg_put(vmreg *r, size_t used)
{
	r->busy = 0;
	if (__builtin_expect(vmreg_dbg(), 0))
		vmreg_dbg_put(r, used);
	if (used == VMREG_SCRATCH)
		return;
	used = (used + VMREG_PAGE - 1) & ~(VMREG_PAGE - 1);
	if (used > r->cap)
		used = r->cap;
	if (used <= VMREG_KEEP) {
		memset(r->p, 0, used);
	} else {
		memset(r->p, 0, VMREG_KEEP);
		if (mmap(r->p + VMREG_KEEP, used - VMREG_KEEP, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANON | MAP_NORESERVE | MAP_FIXED, -1, 0) == MAP_FAILED) {
			perror("vcc: mmap");
			abort();
		}
	}
}

#endif
