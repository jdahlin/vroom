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

#include "frontend/pp/dircache.h"
#include "common/intern.h"
#include "frontend/lex/lex.h"

enum { PP_DIRECTORY_UNKNOWN, PP_DIRECTORY_PROBING, PP_DIRECTORY_EXISTS, PP_DIRECTORY_MISSING };

typedef struct pp_directory_node {
	struct pp_directory_node *next;	/* the bucket's chain (see lex.c's lex_cache_entry_get) */
	uint32_t hash, path_length;
	_Atomic int state;
	_Atomic uint32_t epoch;	/* the cache epoch (lex.h) a PP_DIRECTORY_MISSING answer was last checked in */
	char path[];
} pp_directory_node;

#define PP_DIRECTORY_CACHE_TABLE_BITS 14
static _Atomic(pp_directory_node *) pp_directory_cache_table[1u << PP_DIRECTORY_CACHE_TABLE_BITS];

static char *pp_directory_cache_base;
static _Atomic size_t pp_directory_cache_used;
#define PP_DIRECTORY_CACHE_SIZE (64ull << 20)

_Atomic unsigned long pp_directory_probe_count;	/* VPP_TIME */

void pp_directory_cache_flush(void)	/* (lex.c's lex_cache_flush) */
{
	for (size_t i = 0; i < (1u << PP_DIRECTORY_CACHE_TABLE_BITS); i++)
		atomic_store_explicit(&pp_directory_cache_table[i], NULL, memory_order_release);
}

static void *pp_directory_cache_allocate(size_t n)
{
	char *b0 = __atomic_load_n(&pp_directory_cache_base, __ATOMIC_ACQUIRE);
	size_t off;

	if (!b0) {
		char *b = mmap(NULL, PP_DIRECTORY_CACHE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0), *z = NULL;

		if (b == MAP_FAILED)
			abort();
		if (__atomic_compare_exchange_n(&pp_directory_cache_base, &z, b, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			b0 = b;
		else
			munmap(b, PP_DIRECTORY_CACHE_SIZE), b0 = z;	/* the winner's; never re-read base plainly (TSan) */
	}
	off = atomic_fetch_add(&pp_directory_cache_used, (n + 15) & ~(size_t)15);
	if (off + n > PP_DIRECTORY_CACHE_SIZE)
		abort();
	return b0 + off;
}

static pp_directory_node *pp_directory_cache_node_get(const char *path, size_t plen)
{
	uint32_t h = vintern_hash(path, plen);
	_Atomic(pp_directory_node *) *b = &pp_directory_cache_table[h >> (32 - PP_DIRECTORY_CACHE_TABLE_BITS)];
	pp_directory_node *head = atomic_load_explicit(b, memory_order_acquire), *seen = NULL, *mine = NULL;

	for (;;) {
		for (pp_directory_node *e = head; e != seen; e = e->next)
			if (e->hash == h && e->path_length == plen && !memcmp(e->path, path, plen))
				return e;
		if (!mine) {
			mine = pp_directory_cache_allocate(sizeof(*mine) + plen + 17);
			mine->hash = h;
			mine->path_length = (uint32_t)plen;
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

static int pp_directory_state_probe(pp_directory_node *n)
{
	int s = atomic_load_explicit(&n->state, memory_order_acquire);
	struct stat st;

	uint32_t cur = vcache_epoch();

	if (s == PP_DIRECTORY_EXISTS)
		return s;
	if (s == PP_DIRECTORY_MISSING) {	/* a directory that was missing may have been made since */
		if (atomic_load_explicit(&n->epoch, memory_order_relaxed) == cur)
			return s;
		if (!stat(n->path_length ? n->path : ".", &st) && S_ISDIR(st.st_mode)) {
			atomic_store_explicit(&n->state, PP_DIRECTORY_EXISTS, memory_order_release);
			return PP_DIRECTORY_EXISTS;
		}
		atomic_store_explicit(&n->epoch, cur, memory_order_relaxed);
		return s;
	}
	if (s == PP_DIRECTORY_UNKNOWN && atomic_compare_exchange_strong(&n->state, &s, PP_DIRECTORY_PROBING)) {
		atomic_fetch_add_explicit(&pp_directory_probe_count, 1, memory_order_relaxed);
		s = !stat(n->path_length ? n->path : ".", &st) && S_ISDIR(st.st_mode) ? PP_DIRECTORY_EXISTS : PP_DIRECTORY_MISSING;
		atomic_store_explicit(&n->epoch, cur, memory_order_relaxed);
		atomic_store_explicit(&n->state, s, memory_order_release);
		return s;
	}
	return PP_DIRECTORY_UNKNOWN;	/* another thread is probing: don't wait */
}

int pp_directory_exists(const char *path, size_t len)
{
	size_t dl;

	for (dl = len; dl > 0 && path[dl - 1] != '/'; dl--)
		;
	if (dl <= 1)
		return -1;
	return pp_directory_state_probe(pp_directory_cache_node_get(path, dl - 1)) == PP_DIRECTORY_MISSING ? 0 : -1;
}
