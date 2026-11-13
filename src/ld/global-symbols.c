/* The global symbol table: a lock-free open-addressing hash of names to ld_global_symbol records, resolved by atomic minimum on a priority key. */

/* ---------------- global symbols ---------------- */

static inline uint32_t ld_name_hash(const char *s, size_t n) { return (uint32_t)vdigest_xxh3(s, n); }

static uint32_t ld_visibility_rank(uint8_t v)
{
	switch (v & 3) {
	case STV_INTERNAL: return 0;
	case STV_HIDDEN: return 1;
	case STV_PROTECTED: return 2;
	default: return 3;
	}
}
static uint8_t ld_visibility_from_rank(uint32_t r)
{
	static const uint8_t m[4] = {STV_INTERNAL, STV_HIDDEN, STV_PROTECTED, STV_DEFAULT};
	return m[r];
}

/*
 * Slots live in zeroed mmap memory, so "unset" must be 0: keys are stored
 * complemented and combined with atomic max, which is a min on the key.
 */
static void ld_atomic_u64_min(_Atomic uint64_t *p, uint64_t v)
{
	v = ~v;
	uint64_t cur = atomic_load_explicit(p, memory_order_relaxed);
	while (v > cur && !atomic_compare_exchange_weak(p, &cur, v))
		;
}
static void ld_atomic_u8_min(_Atomic uint8_t *p, uint8_t v)
{
	v = ~v;
	uint8_t cur = atomic_load_explicit(p, memory_order_relaxed);
	while (v > cur && !atomic_compare_exchange_weak(p, &cur, v))
		;
}
static inline uint64_t ld_key_load(_Atomic uint64_t *p) { return ~atomic_load_explicit(p, memory_order_relaxed); }
static inline uint32_t ld_visibility_load(_Atomic uint8_t *p)
{
	uint32_t r = (uint8_t)~atomic_load_explicit(p, memory_order_relaxed);
	return r > 3 ? 3 : r;
}

/*
 * Id of name, inserting if needed (lock-free).  The open-addressing slots
 * hold only the name and a published word (hash << 32 | id + 1); symbol
 * state lives in a dense array indexed by id, so the ~12k globals touch
 * ~1 MB instead of a sparse 100-byte-per-slot table.  Ids depend on thread
 * timing; nothing orders by them.
 */
static uint32_t ld_global_name_intern(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = ld_name_hash(name, n);
	for (uint32_t i = h & ctx->symbols.slot_mask;; i = (i + 1) & ctx->symbols.slot_mask) {
		const char *cur = atomic_load_explicit(&ctx->symbols.slot_names[i], memory_order_acquire);
		if (!cur) {
			if (atomic_compare_exchange_strong(&ctx->symbols.slot_names[i], &cur, name)) {
				uint32_t d = atomic_fetch_add_explicit(&ctx->symbols.count, 1, memory_order_relaxed);
				ctx->symbols.names[d] = name;
				atomic_store_explicit(&ctx->symbols.slot_words[i], (uint64_t)h << 32 | (d + 1), memory_order_release);
				return d;
			}
		}
		uint64_t w = atomic_load_explicit(&ctx->symbols.slot_words[i], memory_order_acquire);
		if (w && (uint32_t)(w >> 32) != h)
			continue;
		if (cur == name || !strcmp(cur, name)) {
			while (!w) {
				sched_yield();
				w = atomic_load_explicit(&ctx->symbols.slot_words[i], memory_order_acquire);
			}
			return (uint32_t)w - 1;
		}
	}
}
static uint32_t ld_global_name_find(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = ld_name_hash(name, n);
	for (uint32_t i = h & ctx->symbols.slot_mask;; i = (i + 1) & ctx->symbols.slot_mask) {
		const char *cur = atomic_load(&ctx->symbols.slot_names[i]);
		if (!cur)
			return LD_NONE;
		uint64_t w = atomic_load(&ctx->symbols.slot_words[i]);
		if ((uint32_t)(w >> 32) == h && !strcmp(cur, name))
			return (uint32_t)w - 1;
	}
}
static inline const char *ld_global_name(uint32_t g) { return ctx->symbols.names[g]; }
