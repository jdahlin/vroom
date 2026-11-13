/*
 * vmreg: anonymous regions kept from one compile job to the next.
 *
 * A job used to mmap its tables (MAP_NORESERVE, gigabytes of address space)
 * and munmap them at the end: two syscalls per table and a page fault for
 * every page it touched, every job.  A vmreg is mapped once and handed back
 * with vmreg_put, which zeroes the bytes the job may have written (memset of
 * dirty pages: ~0.15 us per 16 KB page against ~1 us for a fault), so the
 * next job starts on zero, already-faulted memory.
 *
 * The regions of one job come as a set from a process-wide pool (one mutex
 * operation per job), so they outlive the thread that used them (vpp --batch
 * starts threads per pass) and there are only as many sets as jobs that ran
 * at the same time:
 *
 *   static vmpool pool = VMPOOL_INIT(3);
 *   vmreg *r = vmpool_take(&pool);
 *   char *p = vmreg_get(&r[0], n, "name");	// >= n bytes, all zero
 *   ...
 *   vmreg_put(&r[0], used);			// bytes [0, used) may be dirty
 *   vmreg_put(&r[1], VMREG_SCRATCH);		// contents need not be zero next time
 *   vmpool_give(&pool, r);
 *
 * A scratch region is one the code always writes before it reads (stacks,
 * per-function buffers): it is handed back as is.  Beyond VMREG_KEEP bytes a
 * zeroed region gets fresh pages (one mmap) instead, so a huge job does not
 * leave the pool holding its memory.
 *
 * Debugging (environment, read once): VCC_VMREG_CHECK=1 verifies at every put
 * that no resident page past `used` holds a nonzero byte (a wrong hint) and
 * fills the resident pages of scratch regions with 0xa5 (a wrong scratch
 * claim then changes the output); VCC_VMREG_STATS=1 prints per region the
 * resident bytes handed back.  Both use mincore and are slow.
 */
#ifndef VCC_JOB_REGIONS_H
#define VCC_JOB_REGIONS_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common/attr.h"

typedef struct vmreg {
	char *p;
	size_t cap;
	const char *name;
	int busy;	/* got, not yet put (a job that never put it: map afresh) */
} vmreg;

#define VMREG_SCRATCH ((size_t)-1)
#define VMREG_KEEP ((size_t)64 << 20)
#define VMREG_MIN ((size_t)64 << 20)
#define VMREG_PAGE ((size_t)16384)	/* arm64 macOS; a smaller real page only means more zeroing */

static inline VUSE VRETNONNULL void *vmreg_map(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

/* ---- debugging: VCC_VMREG_CHECK / VCC_VMREG_STATS (job-regions.c) ---- */

extern int vmreg_dbg_mode;	/* -1 until read from the environment; bit 0 check, bit 1 stats */
int vmreg_dbg_init(void);	/* reads the environment (once), returns the mode */
__attribute__((noinline, cold)) void vmreg_dbg_put(vmreg *r, size_t used);

static inline int vmreg_dbg(void)
{
	int m = __atomic_load_n(&vmreg_dbg_mode, __ATOMIC_RELAXED);	/* (threads race to set it: same value) */

	if (__builtin_expect(m < 0, 0))
		m = vmreg_dbg_init();
	return m;
}

/* ---- the regions ---- */

/* at least n bytes, zero (or, for a scratch region, whatever the last job left) */
static inline VUSE VNONNULL(1) void *vmreg_get(vmreg *r, size_t n, const char *name)
{
	if (__builtin_expect(r->cap < n || r->busy, 0)) {
		if (r->p)
			munmap(r->p, r->cap);
		n = (n + VMREG_PAGE - 1) & ~(VMREG_PAGE - 1);
		if (r->cap < n)	/* sizes that grow with the TU: double, from 64 MB of address space */
			r->cap = r->cap * 2 > n ? r->cap * 2 : n > VMREG_MIN ? n : VMREG_MIN;
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

/* ---- pools of region sets ---- */

typedef struct vmpool {
	pthread_mutex_t mu;
	int n;		/* regions per set */
	void *free;	/* sets handed back: a list through the word before the set */
} vmpool;

#define VMPOOL_INIT(nreg) { PTHREAD_MUTEX_INITIALIZER, (nreg), NULL }

static inline vmreg *vmpool_take(vmpool *pl)
{
	void **s;

	pthread_mutex_lock(&pl->mu);
	s = pl->free;
	if (s)
		pl->free = *s;
	pthread_mutex_unlock(&pl->mu);
	if (!s) {
		s = calloc(1, sizeof(void *) + (size_t)pl->n * sizeof(vmreg));
		if (!s)
			abort();
	}
	return (vmreg *)(s + 1);
}

static inline void vmpool_give(vmpool *pl, vmreg *r)
{
	void **s = (void **)r - 1;

	pthread_mutex_lock(&pl->mu);
	*s = pl->free;
	pl->free = s;
	pthread_mutex_unlock(&pl->mu);
}

/* ---------------- reservations, regions, arenas ---------------- */

/* n bytes of anonymous memory (flags: MAP_NORESERVE or 0); perror(msg) and abort on failure */
static inline VUSE VRETNONNULL void *vreserve(size_t n, int flags, const char *msg)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | flags, -1, 0);

	if (p == MAP_FAILED) {
		perror(msg);
		abort();
	}
	return p;
}

#endif
