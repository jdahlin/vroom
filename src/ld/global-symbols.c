/* The global symbol table: a lock-free open-addressing hash of names to ld_global_symbol records, resolved by atomic minimum on a priority key. */

/* ---------------- global symbols ---------------- */

typedef struct {
	uint8_t kind;			/* LD_VALUE_KIND_ABSOLUTE / LD_VALUE_KIND_OUTPUT_SECTION / LD_VALUE_KIND_INPUT_SECTION */
	uint8_t fabs, type;
	uint32_t idx;
	uint64_t val, align;
} ld_expression_value;
enum { LD_VALUE_KIND_ABSOLUTE, LD_VALUE_KIND_OUTPUT_SECTION, LD_VALUE_KIND_INPUT_SECTION };

enum { LD_SYMBOL_STATE_UNDEFINED, LD_SYMBOL_STATE_OBJECT, LD_SYMBOL_STATE_SCRIPT };

typedef struct ld_global_symbol {
	_Atomic uint64_t def;		/* best definition: rank<<62 | obj<<32 | symidx (rank: 0 strong, 1 common, 2 weak) */
	_Atomic uint64_t first;		/* first occurrence, for symbol table order */
	_Atomic uint8_t visrank;	/* min visibility (rank, see ld_visibility_rank()) */
	uint8_t state, type, bind, vis, hidden_script;
	uint8_t demoted, provide_live, scriptdef, dup, common;
	_Atomic uint8_t strongref;	/* some non-weak undefined reference */
	uint32_t obj;			/* defining object, LD_NONE for script */
	uint32_t symidx;
	uint32_t sec;			/* LD_SYMBOL_STATE_OBJECT: ISec id or LD_NONE (absolute) */
	uint64_t val, size;
	ld_expression_value ev;				/* LD_SYMBOL_STATE_SCRIPT: section + offset */
	uint32_t order;			/* index among globals in insertion order */
	uint32_t outidx;		/* index in the output .symtab */
	uint32_t thunk;
	uint64_t fva;			/* after layout: ld_global_symbol_address() */
	bool fabs, fundef;		/* ld_global_symbol_is_absolute(); undefined or demoted */
	_Atomic uint8_t needgot;	/* some GOT relocation names it */
	uint32_t gotidx;		/* its .got slot */
	uint16_t ver;			/* VERSION index: 0 local, 1 global, 2.. named */
	uint32_t dynidx;		/* index in .dynsym, 0 if not exported */
} ld_global_symbol;

static _Atomic(const char *) *hnames;	/* per slot */
static _Atomic uint64_t *hword;		/* per slot: hash << 32 | id + 1, 0 until published */
static const char **gnames;		/* per id */
static ld_global_symbol *gsyms;			/* per id */
static _Atomic uint32_t ngsyms;
static uint32_t hmask;
static VVEC(uint32_t) gorder;		/* ids in symbol table order */

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
	for (uint32_t i = h & hmask;; i = (i + 1) & hmask) {
		const char *cur = atomic_load_explicit(&hnames[i], memory_order_acquire);
		if (!cur) {
			if (atomic_compare_exchange_strong(&hnames[i], &cur, name)) {
				uint32_t d = atomic_fetch_add_explicit(&ngsyms, 1, memory_order_relaxed);
				gnames[d] = name;
				atomic_store_explicit(&hword[i], (uint64_t)h << 32 | (d + 1), memory_order_release);
				return d;
			}
		}
		uint64_t w = atomic_load_explicit(&hword[i], memory_order_acquire);
		if (w && (uint32_t)(w >> 32) != h)
			continue;
		if (cur == name || !strcmp(cur, name)) {
			while (!w) {
				sched_yield();
				w = atomic_load_explicit(&hword[i], memory_order_acquire);
			}
			return (uint32_t)w - 1;
		}
	}
}
static uint32_t ld_global_name_find(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = ld_name_hash(name, n);
	for (uint32_t i = h & hmask;; i = (i + 1) & hmask) {
		const char *cur = atomic_load(&hnames[i]);
		if (!cur)
			return LD_NONE;
		uint64_t w = atomic_load(&hword[i]);
		if ((uint32_t)(w >> 32) == h && !strcmp(cur, name))
			return (uint32_t)w - 1;
	}
}
static inline const char *ld_global_name(uint32_t g) { return gnames[g]; }
