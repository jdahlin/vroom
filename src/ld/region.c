/* Per-thread bump arenas that every allocation of a link comes from, and the release of big mappings. */

/* ---------------- arenas ---------------- */

#define LD_REGION_BLOCK_SIZE (8u << 20)
typedef struct ld_region_block { struct ld_region_block *next; size_t size; } ld_region_block;
static ld_region_block *ld_region_blocks;			/* all blocks, freed after the link */
static pthread_mutex_t ld_region_blocks_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread uint8_t *ld_region_current, *ld_region_end;
static _Atomic unsigned ld_region_generation;	/* bumped when all arenas are freed */
static __thread unsigned ld_region_thread_generation;

/* set by main for the last link of the process: exit tears the mappings down
 * in bulk, cheaper than ~900 munmap calls */
static bool ld_keep_mappings;
static void ld_region_unmap(const void *p, size_t n)
{
	if (!ld_keep_mappings)
		munmap((void *)p, n);
}

static void *ld_region_large_alloc(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "vld: out of memory (%zu bytes)\n", n);
		abort();
	}
	return p;
}

/* zeroed memory from this thread's arena */
static void *ld_region_alloc(size_t n)
{
	n = (n + 15) & ~(size_t)15;
	if (ld_region_thread_generation != atomic_load_explicit(&ld_region_generation, memory_order_relaxed)) {
		ld_region_current = ld_region_end = NULL;
		ld_region_thread_generation = ld_region_generation;
	}
	if ((size_t)(ld_region_end - ld_region_current) < n) {
		size_t bs = n + sizeof(ld_region_block) + 16 > LD_REGION_BLOCK_SIZE ? n + sizeof(ld_region_block) + 16 : LD_REGION_BLOCK_SIZE;
		ld_region_block *b = ld_region_large_alloc(bs);
		b->size = bs;
		{
			VLOCK(ld_region_blocks_lock);
			b->next = ld_region_blocks;
			ld_region_blocks = b;
		}
		ld_region_current = (uint8_t *)b + 32;
		ld_region_end = (uint8_t *)b + bs;
	}
	void *p = ld_region_current;
	ld_region_current += n;
	return p;
}
static void ld_regions_free(void)
{
	/* Every thread's current block is in the list; drop all of them. */
	for (ld_region_block *b = ld_region_blocks, *n; b; b = n) {
		n = b->next;
		ld_region_unmap(b, b->size);
	}
	ld_region_blocks = NULL;
	atomic_fetch_add(&ld_region_generation, 1);
}
static char *ld_region_string_duplicate(const char *s)
{
	size_t n = strlen(s);
	char *p = ld_region_alloc(n + 1);
	memcpy(p, s, n + 1);
	return p;
}
