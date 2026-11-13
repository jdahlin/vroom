/* Per-thread bump arenas that every allocation of a link comes from, and the release of big mappings. */

/* ---------------- arenas ---------------- */

#define ARENA_BLOCK (8u << 20)
typedef struct Block { struct Block *next; size_t size; } Block;
static Block *blocks;			/* all blocks, freed after the link */
static pthread_mutex_t blocklock = PTHREAD_MUTEX_INITIALIZER;
static __thread uint8_t *acur, *aend;
static _Atomic unsigned arena_gen;	/* bumped when all arenas are freed */
static __thread unsigned my_gen;

/* set by main for the last link of the process: exit tears the mappings down
 * in bulk, cheaper than ~900 munmap calls */
static bool keep_maps;
static void unmap(const void *p, size_t n)
{
	if (!keep_maps)
		munmap((void *)p, n);
}

static void *bigalloc(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "vld: out of memory (%zu bytes)\n", n);
		abort();
	}
	return p;
}

/* zeroed memory from this thread's arena */
static void *alloc(size_t n)
{
	n = (n + 15) & ~(size_t)15;
	if (my_gen != atomic_load_explicit(&arena_gen, memory_order_relaxed)) {
		acur = aend = NULL;
		my_gen = arena_gen;
	}
	if ((size_t)(aend - acur) < n) {
		size_t bs = n + sizeof(Block) + 16 > ARENA_BLOCK ? n + sizeof(Block) + 16 : ARENA_BLOCK;
		Block *b = bigalloc(bs);
		b->size = bs;
		{
			VLOCK(blocklock);
			b->next = blocks;
			blocks = b;
		}
		acur = (uint8_t *)b + 32;
		aend = (uint8_t *)b + bs;
	}
	void *p = acur;
	acur += n;
	return p;
}
static void free_arenas(void)
{
	/* Every thread's current block is in the list; drop all of them. */
	for (Block *b = blocks, *n; b; b = n) {
		n = b->next;
		unmap(b, b->size);
	}
	blocks = NULL;
	atomic_fetch_add(&arena_gen, 1);
}
static char *astrdup(const char *s)
{
	size_t n = strlen(s);
	char *p = alloc(n + 1);
	memcpy(p, s, n + 1);
	return p;
}
