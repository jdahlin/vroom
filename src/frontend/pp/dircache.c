/*
 * Directory cache for #include resolution: which directories exist.
 *
 * An #include <linux/foo.h> is tried in every search directory until one
 * has it, and each miss is an open() that fails (~3 us, and serialised
 * system-wide on macOS: 8 threads miss no faster than 1).  Measured over
 * the kernel TUs, 94% of the distinct misses are in directories that do
 * not exist at all (arch/arm64/include/linux/, .../generated/linux/, ...:
 * kbuild's -I list is the same for every subtree), so the cache only
 * remembers, per directory, whether it exists: one probe per distinct
 * directory, shared by all threads, instead of one failed open() per
 * (directory, name).
 *
 * Listing directories (getattrlistbulk) to answer the remaining misses was
 * tried and measured slower: the few misses in existing directories are
 * cheaper than listing them.
 */
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "dircache.h"
#include "../../common/intern.h"

enum { D_UNKNOWN, D_PROBING, D_EXISTS, D_MISSING };

typedef struct dnode {
	struct dnode *next;	/* the bucket's chain (see lex.c's centry_get) */
	uint32_t hash, plen;
	_Atomic int state;
	char path[];
} dnode;

#define DBITS 14
static _Atomic(dnode *) dtab[1u << DBITS];

static char *base;
static _Atomic size_t used;
#define DC_MAX (64ull << 20)

_Atomic unsigned long vdir_probes;	/* VPP_TIME */

void vdir_flush(void)	/* (lex.c's vlex_cache_flush) */
{
	for (size_t i = 0; i < (1u << DBITS); i++)
		atomic_store_explicit(&dtab[i], NULL, memory_order_release);
}

static void *dalloc(size_t n)
{
	char *b0 = __atomic_load_n(&base, __ATOMIC_ACQUIRE);
	size_t off;

	if (!b0) {
		char *b = mmap(NULL, DC_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0), *z = NULL;

		if (b == MAP_FAILED)
			abort();
		if (__atomic_compare_exchange_n(&base, &z, b, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			b0 = b;
		else
			munmap(b, DC_MAX), b0 = z;	/* the winner's; never re-read base plainly (TSan) */
	}
	off = atomic_fetch_add(&used, (n + 15) & ~(size_t)15);
	if (off + n > DC_MAX)
		abort();
	return b0 + off;
}

static dnode *node_get(const char *path, size_t plen)
{
	uint32_t h = vintern_hash(path, plen);
	_Atomic(dnode *) *b = &dtab[h >> (32 - DBITS)];
	dnode *head = atomic_load_explicit(b, memory_order_acquire), *seen = NULL, *mine = NULL;

	for (;;) {
		for (dnode *e = head; e != seen; e = e->next)
			if (e->hash == h && e->plen == plen && !memcmp(e->path, path, plen))
				return e;
		if (!mine) {
			mine = dalloc(sizeof(*mine) + plen + 17);
			mine->hash = h;
			mine->plen = (uint32_t)plen;
			memcpy(mine->path, path, plen);
			memset(mine->path + plen, 0, 17);
		}
		mine->next = head;
		seen = head;
		if (atomic_compare_exchange_strong_explicit(b, &head, mine, memory_order_acq_rel,
							    memory_order_acquire))
			return mine;
	}
}

static int dir_state(dnode *n)
{
	int s = atomic_load_explicit(&n->state, memory_order_acquire);
	struct stat st;

	if (s == D_EXISTS || s == D_MISSING)
		return s;
	if (s == D_UNKNOWN && atomic_compare_exchange_strong(&n->state, &s, D_PROBING)) {
		atomic_fetch_add_explicit(&vdir_probes, 1, memory_order_relaxed);
		s = !stat(n->plen ? n->path : ".", &st) && S_ISDIR(st.st_mode) ? D_EXISTS : D_MISSING;
		atomic_store_explicit(&n->state, s, memory_order_release);
		return s;
	}
	return D_UNKNOWN;	/* another thread is probing: don't wait */
}

int vdir_exists(const char *path, size_t len)
{
	size_t dl;

	for (dl = len; dl > 0 && path[dl - 1] != '/'; dl--)
		;
	if (dl <= 1)
		return -1;
	return dir_state(node_get(path, dl - 1)) == D_MISSING ? 0 : -1;
}
