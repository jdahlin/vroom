/*
 * vld: ELF linker for the arm64 kernel build (PLAN-fas2 step 2.8).
 *
 * Scope is what the kernel link uses, surveyed from work/build-o0:
 *   - inputs: ELF64 AArch64 relocatable objects, thin and regular archives
 *     (always --whole-archive in the kernel), no COMDAT groups, no COMMON,
 *     no TLS, no debug info;
 *   - relocations: ABS64/ABS32/PREL64/PREL32, CALL26/JUMP26,
 *     ADR_PREL_PG_HI21, ADD_ABS_LO12_NC, LDST{8,16,32,64,128}_ABS_LO12_NC,
 *     LD_PREL_LO19 (the 12 types present, plus the obvious neighbours);
 *   - outputs: the final vmlinux (-shared -Bsymbolic -z notext
 *     --no-apply-dynamic-relocs with the kernel's linker script, so absolute
 *     relocations become R_AARCH64_RELATIVE in .rela.dyn) and ld -r.
 *
 * The output is meant to be byte-identical to ld.lld 23 for the same
 * command line, so lld's observable decisions are reproduced: linker-script
 * expression semantics (ExprValue), the order of passes (the first thunk
 * pass sees symbol values from processSymbolAssignments, which is what
 * makes lld emit ADRP thunks for calls into the -O0 stub symbols), the
 * ADRP+ADD -> NOP+ADR relaxation, SHF_MERGE deduplication in 32 XXH3
 * shards, symbol table order, segment formation and the BLAKE3 build id.
 *
 * Performance: inputs are mmap'ed and parsed in parallel; symbols are
 * resolved in parallel into a lock-free open-addressing table (atomic min
 * on a priority key makes the result independent of thread timing); merge
 * pieces are deduplicated per shard in parallel; layout is a serial walk
 * over ~20k sections (cheap); copying and relocation run in parallel per
 * input section into one anonymous buffer that is written with one write().
 * No per-symbol malloc: everything comes from bump arenas.
 */
#include "ld.h"
#include "ldhash.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#define NONE 0xffffffffu

/* ---------------- ELF definitions ---------------- */

typedef struct {
	uint8_t e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Ehdr;
typedef struct {
	uint32_t sh_name, sh_type;
	uint64_t sh_flags, sh_addr, sh_offset, sh_size;
	uint32_t sh_link, sh_info;
	uint64_t sh_addralign, sh_entsize;
} Shdr;
typedef struct {
	uint32_t st_name;
	uint8_t st_info, st_other;
	uint16_t st_shndx;
	uint64_t st_value, st_size;
} Sym;
typedef struct {
	uint64_t r_offset, r_info;
	int64_t r_addend;
} Rela;
typedef struct {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Phdr;

enum {
	SHT_NULL = 0, SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4,
	SHT_HASH = 5, SHT_DYNAMIC = 6, SHT_NOTE = 7, SHT_NOBITS = 8, SHT_REL = 9,
	SHT_DYNSYM = 11, SHT_INIT_ARRAY = 14, SHT_FINI_ARRAY = 15,
	SHT_PREINIT_ARRAY = 16, SHT_GROUP = 17, SHT_SYMTAB_SHNDX = 18,
	SHT_LLVM_ADDRSIG = 0x6fff4c03,
};
enum {
	SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_MERGE = 0x10,
	SHF_STRINGS = 0x20, SHF_INFO_LINK = 0x40, SHF_GROUP = 0x200, SHF_TLS = 0x400,
	SHF_EXCLUDE = 0x80000000u,
};
enum { STB_LOCAL = 0, STB_GLOBAL = 1, STB_WEAK = 2 };
enum { STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2, STT_SECTION = 3, STT_FILE = 4, STT_TLS = 6 };
enum { STV_DEFAULT = 0, STV_INTERNAL = 1, STV_HIDDEN = 2, STV_PROTECTED = 3 };
enum { SHN_UNDEF = 0, SHN_ABS = 0xfff1, SHN_COMMON = 0xfff2, SHN_XINDEX = 0xffff };
enum { PT_LOAD = 1, PT_DYNAMIC = 2, PT_NOTE = 4, PT_PHDR = 6, PT_GNU_STACK = 0x6474e551 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };
enum {
	R_AARCH64_NONE = 0, R_AARCH64_ABS64 = 257, R_AARCH64_ABS32 = 258,
	R_AARCH64_ABS16 = 259, R_AARCH64_PREL64 = 260, R_AARCH64_PREL32 = 261,
	R_AARCH64_PREL16 = 262, R_AARCH64_LD_PREL_LO19 = 273,
	R_AARCH64_ADR_PREL_LO21 = 274, R_AARCH64_ADR_PREL_PG_HI21 = 275,
	R_AARCH64_ADD_ABS_LO12_NC = 277, R_AARCH64_LDST8_ABS_LO12_NC = 278,
	R_AARCH64_JUMP26 = 282, R_AARCH64_CALL26 = 283,
	R_AARCH64_LDST16_ABS_LO12_NC = 284, R_AARCH64_LDST32_ABS_LO12_NC = 285,
	R_AARCH64_LDST64_ABS_LO12_NC = 286, R_AARCH64_LDST128_ABS_LO12_NC = 299,
	R_AARCH64_RELATIVE = 1027,
};

static inline uint32_t rd32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void wr32(void *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void wr64(void *p, uint64_t v) { memcpy(p, &v, 8); }
static inline uint64_t alignup(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) & ~(a - 1); }
/* smallest x >= v with x % a == skew % a (llvm::alignTo with skew) */
static inline uint64_t alignskew(uint64_t v, uint64_t a, uint64_t skew)
{
	skew %= a;
	return ((v + a - 1 - skew) / a) * a + skew;
}

/* ---------------- diagnostics ---------------- */

static FILE *errf;
static atomic_int nerrors;
static int error_limit = 20;
static pthread_mutex_t errlock = PTHREAD_MUTEX_INITIALIZER;
static const char *argv0 = "vld";

static void diag(const char *kind, const char *fmt, va_list ap)
{
	pthread_mutex_lock(&errlock);
	fprintf(errf, "%s: %s: ", argv0, kind);
	vfprintf(errf, fmt, ap);
	fputc('\n', errf);
	pthread_mutex_unlock(&errlock);
}
static void error(const char *fmt, ...)
{
	int n = atomic_fetch_add(&nerrors, 1);
	if (error_limit && n >= error_limit) {
		if (n == error_limit) {
			pthread_mutex_lock(&errlock);
			fprintf(errf, "%s: error: too many errors emitted, stopping now\n", argv0);
			pthread_mutex_unlock(&errlock);
		}
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	diag("error", fmt, ap);
	va_end(ap);
}
static void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	diag("warning", fmt, ap);
	va_end(ap);
}

/* ---------------- timing (VLD_TIME=1) ---------------- */

static bool timing;
static double t_last;
static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void phase(const char *name)
{
	if (!timing)
		return;
	double t = now();
	fprintf(errf, "  %-28s %7.2f ms\n", name, (t - t_last) * 1e3);
	t_last = t;
}

/* ---------------- arenas ---------------- */

#define ARENA_BLOCK (8u << 20)
typedef struct Block { struct Block *next; size_t size; } Block;
static Block *blocks;			/* all blocks, freed after the link */
static pthread_mutex_t blocklock = PTHREAD_MUTEX_INITIALIZER;
static __thread uint8_t *acur, *aend;
static _Atomic unsigned arena_gen;	/* bumped when all arenas are freed */
static __thread unsigned my_gen;

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
		pthread_mutex_lock(&blocklock);
		b->next = blocks;
		blocks = b;
		pthread_mutex_unlock(&blocklock);
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
		munmap(b, b->size);
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

/* growable array in the arena (amortized doubling) */
#define VEC(T) struct { T *v; uint32_t n, cap; }
#define VPUSH(a, x) do { \
	if ((a).n == (a).cap) { \
		uint32_t nc = (a).cap ? (a).cap * 2 : 8; \
		void *nv = alloc(nc * sizeof(*(a).v)); \
		if ((a).n) memcpy(nv, (a).v, (a).n * sizeof(*(a).v)); \
		(a).v = nv; (a).cap = nc; \
	} \
	(a).v[(a).n++] = (x); \
} while (0)

/* ---------------- thread pool ---------------- */

typedef void (*pfn)(void *arg, size_t i);
static int nthreads = 1;
static int pool_started;
static pthread_t pool[64];
static pthread_mutex_t plock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcv = PTHREAD_COND_INITIALIZER, pdone = PTHREAD_COND_INITIALIZER;
static struct {
	pfn fn;
	void *arg;
	size_t n;
	atomic_size_t next;
	int active;
	unsigned gen;
} job;

static void run_job(void)
{
	size_t i;
	while ((i = atomic_fetch_add_explicit(&job.next, 1, memory_order_relaxed)) < job.n)
		job.fn(job.arg, i);
}

static void *worker(void *unused)
{
	(void)unused;
	unsigned seen = 0;
	for (;;) {
		pthread_mutex_lock(&plock);
		while (job.gen == seen)
			pthread_cond_wait(&pcv, &plock);
		seen = job.gen;
		pthread_mutex_unlock(&plock);
		run_job();
		pthread_mutex_lock(&plock);
		if (--job.active == 0)
			pthread_cond_signal(&pdone);
		pthread_mutex_unlock(&plock);
	}
	return NULL;
}

/* fn(arg, i) for i in [0, n), dynamically scheduled over the pool */
static void pfor(size_t n, pfn fn, void *arg)
{
	if (n == 0)
		return;
	if (nthreads <= 1 || n == 1) {
		for (size_t i = 0; i < n; i++)
			fn(arg, i);
		return;
	}
	if (!pool_started) {
		for (int t = 0; t < nthreads - 1; t++)
			pthread_create(&pool[t], NULL, worker, NULL);
		pool_started = nthreads;
	}
	pthread_mutex_lock(&plock);
	job.fn = fn;
	job.arg = arg;
	job.n = n;
	atomic_store(&job.next, 0);
	job.active = pool_started - 1;
	job.gen++;
	pthread_cond_broadcast(&pcv);
	pthread_mutex_unlock(&plock);
	run_job();
	pthread_mutex_lock(&plock);
	while (job.active)
		pthread_cond_wait(&pdone, &plock);
	pthread_mutex_unlock(&plock);
}

/* ---------------- options ---------------- */

typedef struct {
	const char *path;
	bool whole;
} InputArg;

static struct {
	const char *out;
	const char *script;
	const char *soname;
	const char *comment;		/* .comment linker string */
	const char *build_id;		/* NULL, "sha1", "md5", "fast" */
	const char *map;
	bool relocatable, shared, bsymbolic, notext, no_apply_dyn, no_undefined;
	bool discard_locals, discard_all, strip_debug, pic_veneer, nmagic;
	bool stubs;			/* define unresolved symbols as -O0 stubs */
	bool relax;
	uint64_t max_page;
	VEC(InputArg) inputs;
} opt;

static int dirfd_base = AT_FDCWD;

/* ---------------- input files ---------------- */

typedef struct Obj {
	const char *path;		/* for diagnostics */
	const uint8_t *buf;
	size_t size;
	const Shdr *sh;
	uint32_t nsh;
	const Sym *sym;
	uint32_t nsym, firstg;
	const char *str;		/* symbol string table */
	const char *shstr;
	uint32_t isec0;			/* ISec id of section 0 */
	uint32_t *gs;			/* global slot of sym[firstg + i] */
	uint32_t idx;
	/* symbol table output */
	uint32_t nloc, loc0;		/* kept locals, first output index */
	uint64_t locstr;		/* strtab bytes of kept locals */
	uint64_t str0;
	bool mapped;
} Obj;

static VEC(Obj *) objs;

enum { K_NONE, K_NORMAL, K_MERGE_IN, K_MERGE, K_THUNK, K_RELADYN, K_BUILDID,
       K_SYMTAB, K_STRTAB, K_SHSTRTAB, K_COMMENT };

typedef struct ISec {
	const uint8_t *data;
	const char *name;
	uint64_t size, flags, off;
	uint32_t type, align, entsize;
	uint32_t obj;			/* owning object or NONE */
	uint32_t shndx;
	uint32_t osec;			/* output section, NONE, or OS_DISCARD */
	uint32_t isd;
	const Rela *rela;
	uint32_t nrela;
	uint32_t aux;			/* K_MERGE_IN: MergeIn idx; K_MERGE: Merge idx; K_THUNK: ThunkSec idx */
	uint32_t ndyn;			/* R_AARCH64_RELATIVE entries produced */
	uint32_t dyn0;
	uint8_t kind;
} ISec;

#define OS_DISCARD 0xfffffffeu
static VEC(ISec) isecs;

/* SHF_MERGE input split into pieces */
typedef struct {
	uint32_t isec;
	uint32_t syn;			/* merge synthetic ISec id */
	uint32_t npieces;
	uint32_t *inoff;		/* piece input offsets */
	uint32_t *hash;			/* 31-bit hash */
	uint32_t *outoff;		/* offset in the synthetic section */
} MergeIn;
static VEC(MergeIn) mergeins;

typedef struct {
	uint32_t isec;			/* the synthetic ISec */
	uint32_t osec;
	uint64_t flags;
	uint32_t entsize, align;
	VEC(uint32_t) members;		/* MergeIn ids in order */
	uint64_t shardoff[32], shardsize[32];
	/* piece lists per shard for writing: (mergein, piece) of unique pieces */
	VEC(uint64_t) uniq[32];
} Merge;
static VEC(Merge) merges;

/* ---------------- global symbols ---------------- */

typedef struct {
	uint8_t kind;			/* EV_ABS / EV_OSEC / EV_ISEC */
	uint8_t fabs, type;
	uint32_t idx;
	uint64_t val, align;
} EV;
enum { EV_ABS, EV_OSEC, EV_ISEC };

enum { GS_UNDEF, GS_OBJ, GS_SCRIPT };

typedef struct G {
	_Atomic uint64_t def;		/* best definition: weak<<63 | obj<<32 | symidx */
	_Atomic uint64_t first;		/* first occurrence, for symbol table order */
	_Atomic uint8_t visrank;	/* min visibility (rank, see visrank()) */
	uint8_t state, type, bind, vis, hidden_script;
	uint8_t demoted, provide_live, scriptdef, dup;
	_Atomic uint8_t strongref;	/* some non-weak undefined reference */
	uint32_t obj;			/* defining object, NONE for script */
	uint32_t symidx;
	uint32_t sec;			/* GS_OBJ: ISec id or NONE (absolute) */
	uint64_t val, size;
	EV ev;				/* GS_SCRIPT: section + offset */
	uint32_t order;			/* index among globals in insertion order */
	uint32_t outidx;		/* index in the output .symtab */
	uint32_t thunk;
} G;

static _Atomic(const char *) *hnames;
static uint32_t *hhash;
static G *gsyms;
static uint32_t hmask;
static VEC(uint32_t) gorder;		/* slots in insertion order */

static inline uint32_t strhash(const char *s, size_t n) { return (uint32_t)ld_xxh3(s, n); }

static uint32_t visrank(uint8_t v)
{
	switch (v & 3) {
	case STV_INTERNAL: return 0;
	case STV_HIDDEN: return 1;
	case STV_PROTECTED: return 2;
	default: return 3;
	}
}
static uint8_t rankvis(uint32_t r)
{
	static const uint8_t m[4] = {STV_INTERNAL, STV_HIDDEN, STV_PROTECTED, STV_DEFAULT};
	return m[r];
}

/*
 * Slots live in zeroed mmap memory, so "unset" must be 0: keys are stored
 * complemented and combined with atomic max, which is a min on the key.
 */
static void atomic_min64(_Atomic uint64_t *p, uint64_t v)
{
	v = ~v;
	uint64_t cur = atomic_load_explicit(p, memory_order_relaxed);
	while (v > cur && !atomic_compare_exchange_weak(p, &cur, v))
		;
}
static void atomic_min8(_Atomic uint8_t *p, uint8_t v)
{
	v = ~v;
	uint8_t cur = atomic_load_explicit(p, memory_order_relaxed);
	while (v > cur && !atomic_compare_exchange_weak(p, &cur, v))
		;
}
static inline uint64_t kget(_Atomic uint64_t *p) { return ~atomic_load_explicit(p, memory_order_relaxed); }
static inline uint32_t vget(_Atomic uint8_t *p)
{
	uint32_t r = (uint8_t)~atomic_load_explicit(p, memory_order_relaxed);
	return r > 3 ? 3 : r;
}

/* slot of name, inserting if needed (lock-free) */
static uint32_t gintern(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = strhash(name, n);
	for (uint32_t i = h & hmask;; i = (i + 1) & hmask) {
		const char *cur = atomic_load_explicit(&hnames[i], memory_order_acquire);
		if (!cur) {
			if (atomic_compare_exchange_strong(&hnames[i], &cur, name)) {
				hhash[i] = h;
				return i;
			}
		}
		if (cur == name || !strcmp(cur, name))
			return i;
	}
}
static uint32_t gfind(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = strhash(name, n);
	for (uint32_t i = h & hmask;; i = (i + 1) & hmask) {
		const char *cur = atomic_load(&hnames[i]);
		if (!cur)
			return NONE;
		if (!strcmp(cur, name))
			return i;
	}
}
static inline const char *gname(uint32_t g) { return hnames[g]; }

/* ---------------- linker script ---------------- */

typedef struct Expr Expr;
enum {
	E_NUM, E_SYM, E_DOT, E_ADDR, E_LOADADDR, E_SIZEOF, E_ALIGNOF, E_ALIGN1, E_ALIGN2,
	E_ABSOLUTE, E_BIN, E_NEG, E_NOT, E_LNOT, E_TERN, E_ASSERT, E_DEFINED, E_MAX, E_MIN,
	E_LOG2CEIL, E_SIZEOF_HEADERS,
};
struct Expr {
	uint8_t op;
	char bop[4];
	Expr *a, *b, *c;
	uint64_t num;
	uint32_t sym;			/* E_SYM/E_DEFINED: global slot; E_ADDR etc: osec */
	const char *msg;
};

typedef struct {
	char pat[16][64];		/* not used; patterns live in Pat */
} Unused;

typedef struct {
	const char **globs;		/* section name globs */
	uint32_t nglobs;
	uint8_t sort;			/* SORT_NONE, SORT_NAME, SORT_ALIGN */
} Pat;
enum { SORT_NONE, SORT_NAME, SORT_ALIGN };

typedef struct ISD {
	uint32_t osec;
	Pat *pats;
	uint32_t npats;
	bool keep;
	VEC(uint32_t) secs;		/* ISec ids, in final order */
	uint32_t thunksec;		/* ISec of this ISD's thunk section, or NONE */
	uint64_t thunk_off;		/* pass-0 isecLimit */
} ISD;
static VEC(ISD) isds;

enum { C_ASSIGN, C_OSEC, C_ISD };
typedef struct Assign {
	uint32_t sym;			/* NONE for '.' */
	Expr *e;
	char op;			/* '=' or the operator of op= */
	bool provide, hidden;
	bool defined;			/* shouldDefineSym() */
	bool assert_;
} Assign;
typedef struct Cmd {
	uint8_t kind;
	uint32_t idx;			/* C_OSEC: osec, C_ISD: isd */
	Assign *as;
} Cmd;

typedef struct OSec {
	const char *name;
	Expr *addr, *alignx, *lma, *subalign;
	VEC(Cmd) cmds;
	bool nonalloc, noload, discard, used_in_expr, has_input, live, type_set;
	bool uses_syms;			/* expressionsUseSymbols */
	uint32_t type, addralign, entsize;
	uint64_t flags;
	uint64_t va, size, offset;
	uint32_t shidx, nameoff;
	int ptload;			/* index into phdrs, -1 */
	uint32_t link, info;
} OSec;
static VEC(OSec) osecs;
static VEC(Cmd) topcmds;
static const char *entry_name;

/* names referenced by expressions, in parse order (lld referencedSymbols) */
static VEC(uint32_t) refsyms;
typedef struct { uint32_t sym; VEC(uint32_t) refs; } Provide;
static VEC(Provide) provides;

#define AETHER 0xfffffffdu

/* ---------------- address-assignment state ---------------- */

static uint64_t dot;
static uint32_t cur_osec;		/* AETHER or an osec */
static bool in_layout;

static uint64_t osec_addr(uint32_t o) { return o == AETHER ? 0 : osecs.v[o].va; }
static uint32_t osec_shidx(uint32_t o) { return o == AETHER ? 1 : osecs.v[o].shidx; }

/* ---------------- merge pieces ---------------- */

static uint32_t piece_find(const MergeIn *m, uint64_t off)
{
	uint32_t lo = 0, hi = m->npieces;
	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;
		if (m->inoff[mid] <= off)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* offset of input offset `off` of section s within its output section */
static uint64_t isec_osoff(uint32_t s, uint64_t off)
{
	ISec *is = &isecs.v[s];
	if (is->kind == K_MERGE_IN) {
		MergeIn *m = &mergeins.v[is->aux];
		uint32_t p = piece_find(m, off);
		return isecs.v[m->syn].off + m->outoff[p] + (off - m->inoff[p]);
	}
	return is->off + off;
}
static uint32_t isec_osec(uint32_t s)
{
	ISec *is = &isecs.v[s];
	if (is->kind == K_MERGE_IN)
		return isecs.v[mergeins.v[is->aux].syn].osec;
	return is->osec;
}
static uint64_t isec_va(uint32_t s, uint64_t off)
{
	uint32_t o = isec_osec(s);
	if (o == NONE || o == OS_DISCARD)
		return 0;
	return osecs.v[o].va + isec_osoff(s, off);
}

static uint64_t ev_secaddr(EV v)
{
	switch (v.kind) {
	case EV_OSEC: return osec_addr(v.idx);
	case EV_ISEC: return isec_va(v.idx, 0);
	default: return 0;
	}
}
static uint64_t ev_value(EV v)
{
	uint64_t x;
	switch (v.kind) {
	case EV_OSEC: x = osec_addr(v.idx) + v.val; break;
	case EV_ISEC: x = isec_va(v.idx, v.val); break;
	default: x = v.val;
	}
	return alignup(x, v.align);
}
static uint64_t ev_secoff(EV v) { return ev_value(v) - ev_secaddr(v); }
static bool ev_isabs(EV v) { return v.fabs || v.kind == EV_ABS; }
static EV evabs(uint64_t x) { return (EV){.kind = EV_ABS, .val = x, .align = 1}; }
static EV evsec(EV a, bool fabs, uint64_t off)
{
	return (EV){.kind = a.kind, .idx = a.idx, .fabs = fabs, .val = off, .align = 1};
}

/* value of a global symbol as an expression value (getSymbolValue) */
static EV gsym_ev(uint32_t g)
{
	G *s = &gsyms[g];
	EV v = {.align = 1, .type = s->type};
	if (s->state == GS_SCRIPT) {
		v.kind = s->ev.kind;
		v.idx = s->ev.idx;
		v.val = s->ev.val;
		return v;
	}
	if (s->state == GS_OBJ && !s->demoted) {
		if (s->sec == NONE) {
			v.kind = EV_ABS;
			v.val = s->val;
		} else {
			v.kind = EV_ISEC;
			v.idx = s->sec;
			v.val = s->val;
		}
		return v;
	}
	if (in_layout)
		error("symbol not found: %s", gname(g));
	return evabs(0);
}

static uint64_t gsym_va(uint32_t g)
{
	G *s = &gsyms[g];
	if (s->thunk != NONE && 0)
		return 0;
	if (s->state == GS_SCRIPT)
		return s->ev.kind == EV_ABS ? s->ev.val :
		       s->ev.kind == EV_OSEC ? osec_addr(s->ev.idx) + s->ev.val :
		       isec_va(s->ev.idx, s->ev.val);
	if (s->state == GS_OBJ && !s->demoted)
		return s->sec == NONE ? s->val : isec_va(s->sec, s->val);
	return 0;
}
static bool gsym_isabs(uint32_t g)
{
	G *s = &gsyms[g];
	if (s->state == GS_SCRIPT)
		return s->ev.kind == EV_ABS;
	if (s->state == GS_OBJ && !s->demoted)
		return s->sec == NONE;
	return true;
}

/* ---------------- expressions: evaluation (lld ExprValue rules) ---------------- */

static uint64_t log2ceil(uint64_t x)
{
	if (x <= 1)
		return 0;
	return 64 - __builtin_clzll(x - 1);
}
static uint64_t chkalign(uint64_t a)
{
	if (a == 0)
		a = 1;
	if (a & (a - 1)) {
		if (in_layout)
			error("alignment must be power of 2");
		return 1;
	}
	return a;
}

static uint64_t header_size(void);
static EV eval(Expr *e);

static void move_abs_right(EV *a, EV *b)
{
	if (a->kind == EV_ABS || (a->fabs && !ev_isabs(*b))) {
		EV t = *a;
		*a = *b;
		*b = t;
	}
	if (!ev_isabs(*b) && in_layout)
		error("at least one side of the expression must be absolute");
}
static EV ev_add(EV a, EV b)
{
	move_abs_right(&a, &b);
	return evsec(a, a.fabs, ev_secoff(a) + ev_value(b));
}
static EV ev_sub(EV a, EV b)
{
	if (!ev_isabs(a) && !ev_isabs(b))
		return evabs(ev_value(a) - ev_value(b));
	return evsec(a, false, ev_secoff(a) - ev_value(b));
}
static EV ev_bit(EV a, EV b, char op)
{
	move_abs_right(&a, &b);
	uint64_t x = ev_value(a), y = ev_value(b);
	uint64_t r = op == '&' ? x & y : op == '|' ? x | y : x ^ y;
	return evsec(a, a.fabs, r - ev_secaddr(a));
}
static EV binop(const char *op, EV l, EV r)
{
	uint64_t x, y;
	if (!strcmp(op, "+")) return ev_add(l, r);
	if (!strcmp(op, "-")) return ev_sub(l, r);
	if (!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^")) return ev_bit(l, r, op[0]);
	x = ev_value(l);
	y = ev_value(r);
	if (!strcmp(op, "*")) return evabs(x * y);
	if (!strcmp(op, "/")) {
		if (!y) { if (in_layout) error("division by zero"); return evabs(0); }
		return evabs(x / y);
	}
	if (!strcmp(op, "%")) {
		if (!y) { if (in_layout) error("modulo by zero"); return evabs(0); }
		return evabs(x % y);
	}
	if (!strcmp(op, "<<")) return evabs(x << (y % 64));
	if (!strcmp(op, ">>")) return evabs(x >> (y % 64));
	if (!strcmp(op, "<")) return evabs(x < y);
	if (!strcmp(op, ">")) return evabs(x > y);
	if (!strcmp(op, "<=")) return evabs(x <= y);
	if (!strcmp(op, ">=")) return evabs(x >= y);
	if (!strcmp(op, "==")) return evabs(x == y);
	if (!strcmp(op, "!=")) return evabs(x != y);
	if (!strcmp(op, "&&")) return evabs(x && y);
	if (!strcmp(op, "||")) return evabs(x || y);
	error("bad operator %s", op);
	return evabs(0);
}

static EV dot_ev(void)
{
	EV v = {.kind = EV_OSEC, .idx = cur_osec, .val = dot - osec_addr(cur_osec), .align = 1};
	return v;
}

static VEC(const char *) assert_fail;	/* recorded failures of the current pass */

static EV eval(Expr *e)
{
	EV v;
	switch (e->op) {
	case E_NUM: return evabs(e->num);
	case E_SYM: return gsym_ev(e->sym);
	case E_DOT: return dot_ev();
	case E_ADDR:
		return (EV){.kind = EV_OSEC, .idx = e->sym, .val = 0, .align = 1};
	case E_LOADADDR: return evabs(osecs.v[e->sym].va);	/* LMA == VMA here */
	case E_SIZEOF: return evabs(osecs.v[e->sym].size);
	case E_ALIGNOF: return evabs(osecs.v[e->sym].addralign);
	case E_ALIGN1: return evabs(alignup(dot, chkalign(ev_value(eval(e->a)))));
	case E_ALIGN2:
		v = eval(e->a);
		v.align = chkalign(ev_value(eval(e->b)));
		return v;
	case E_ABSOLUTE:
		v = eval(e->a);
		v.fabs = 1;
		return v;
	case E_BIN: return binop(e->bop, eval(e->a), eval(e->b));
	case E_NEG: return evabs(-ev_value(eval(e->a)));
	case E_NOT: return evabs(~ev_value(eval(e->a)));
	case E_LNOT: return evabs(!ev_value(eval(e->a)));
	case E_TERN: return ev_value(eval(e->a)) ? eval(e->b) : eval(e->c);
	case E_ASSERT:
		if (!ev_value(eval(e->a)))
			VPUSH(assert_fail, e->msg);
		return evabs(dot);
	case E_DEFINED: {
		G *s = &gsyms[e->sym];
		return evabs(s->state == GS_OBJ || s->state == GS_SCRIPT);
	}
	case E_MAX: case E_MIN: {
		uint64_t a = ev_value(eval(e->a)), b = ev_value(eval(e->b));
		return evabs(e->op == E_MAX ? (a > b ? a : b) : (a < b ? a : b));
	}
	case E_LOG2CEIL: return evabs(log2ceil(ev_value(eval(e->a))));
	case E_SIZEOF_HEADERS: return evabs(header_size());
	}
	return evabs(0);
}

/* ---------------- script parser ---------------- */

static const char *sp, *sp_end, *sp_file;

static void sskip(void)
{
	for (;;) {
		while (sp < sp_end && (*sp == ' ' || *sp == '\t' || *sp == '\n' || *sp == '\r'))
			sp++;
		if (sp + 1 < sp_end && sp[0] == '/' && sp[1] == '*') {
			const char *e = strstr(sp + 2, "*/");
			sp = e ? e + 2 : sp_end;
			continue;
		}
		break;
	}
}
static bool satend(void) { sskip(); return sp >= sp_end; }
static bool speek(char c) { sskip(); return sp < sp_end && *sp == c; }
static bool sconsume(char c)
{
	if (speek(c)) {
		sp++;
		return true;
	}
	return false;
}
static void serr(const char *what)
{
	int line = 1;
	for (const char *p = sp_file; p < sp; p++)
		line += *p == '\n';
	error("linker script line %d: %s near '%.20s'", line, what, sp);
}
static void sexpect(char c)
{
	if (!sconsume(c)) {
		char m[32];
		snprintf(m, sizeof m, "expected '%c'", c);
		serr(m);
		sp = sp_end;
	}
}
static char *sdup(const char *b, const char *e)
{
	char *p = alloc(e - b + 1);
	memcpy(p, b, e - b);
	p[e - b] = 0;
	return p;
}
/* statement-level name: up to whitespace or one of (){}:;,= */
static char *sname(void)
{
	sskip();
	const char *b = sp;
	if (sp < sp_end && *sp == '"') {
		const char *e = memchr(sp + 1, '"', sp_end - sp - 1);
		if (!e) { serr("unterminated string"); sp = sp_end; return astrdup(""); }
		sp = e + 1;
		return sdup(b + 1, e);
	}
	while (sp < sp_end && !strchr(" \t\r\n(){}:;,=", *sp))
		sp++;
	return sdup(b, sp);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
static char *spatname(void)
{
	sskip();
	const char *b = sp;
	while (sp < sp_end && !strchr(" \t\r\n()", *sp))
		sp++;
	return sdup(b, sp);
}
static bool sword(const char *w)
{
	sskip();
	size_t n = strlen(w);
	if ((size_t)(sp_end - sp) >= n && !memcmp(sp, w, n) &&
	    (sp + n == sp_end || !(isalnum((unsigned char)sp[n]) || sp[n] == '_' || sp[n] == '.'))) {
		sp += n;
		return true;
	}
	return false;
}

static uint32_t get_osec(const char *name);
static Expr *pexpr(void);

static Expr *mk(uint8_t op)
{
	Expr *e = alloc(sizeof *e);
	e->op = op;
	return e;
}

static Provide *cur_provide;

static void note_ref(uint32_t g)
{
	if (cur_provide)
		VPUSH(cur_provide->refs, g);
	else
		VPUSH(refsyms, g);
}

static int prec(const char *op)
{
	static const struct { const char *op; int p; } t[] = {
		{"*", 11}, {"/", 11}, {"%", 11}, {"+", 10}, {"-", 10}, {"<<", 9}, {">>", 9},
		{"<", 8}, {"<=", 8}, {">", 8}, {">=", 8}, {"==", 7}, {"!=", 7}, {"&", 6},
		{"^", 5}, {"|", 4}, {"&&", 3}, {"||", 2}, {"?", 1},
	};
	for (size_t i = 0; i < sizeof t / sizeof *t; i++)
		if (!strcmp(op, t[i].op))
			return t[i].p;
	return -1;
}
/* peek an operator token; returns length or 0 */
static int peekop(char out[4])
{
	sskip();
	static const char *ops[] = {"<<=", ">>=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
				    "+=", "-=", "*=", "/=", "&=", "|=", "^=",
				    "*", "/", "%", "+", "-", "<", ">", "&", "^", "|", "?", ":", "=", NULL};
	for (int i = 0; ops[i]; i++) {
		size_t n = strlen(ops[i]);
		if ((size_t)(sp_end - sp) >= n && !memcmp(sp, ops[i], n)) {
			strcpy(out, ops[i]);
			return (int)n;
		}
	}
	return 0;
}

static Expr *pprimary(void);
static Expr *pexpr1(Expr *lhs, int minp)
{
	char op1[4], op2[4];
	for (;;) {
		int n = peekop(op1);
		if (!n || prec(op1) < minp || (strchr(op1, '=') && strcmp(op1, "==") && strcmp(op1, "!=") &&
		    strcmp(op1, "<=") && strcmp(op1, ">=")))
			break;
		if (!strcmp(op1, ":"))
			break;
		sp += n;
		if (!strcmp(op1, "?")) {
			Expr *t = mk(E_TERN);
			t->a = lhs;
			t->b = pexpr();
			sexpect(':');
			t->c = pexpr();
			return t;
		}
		Expr *rhs = pprimary();
		for (;;) {
			int m = peekop(op2);
			if (!m || prec(op2) <= prec(op1) || !strcmp(op2, ":"))
				break;
			if (strchr(op2, '=') && strcmp(op2, "==") && strcmp(op2, "!=") &&
			    strcmp(op2, "<=") && strcmp(op2, ">="))
				break;
			rhs = pexpr1(rhs, prec(op2));
		}
		Expr *b = mk(E_BIN);
		strcpy(b->bop, op1);
		b->a = lhs;
		b->b = rhs;
		lhs = b;
	}
	return lhs;
}
static Expr *pexpr(void) { return pexpr1(pprimary(), 0); }

static Expr *pparen(void)
{
	sexpect('(');
	Expr *e = pexpr();
	sexpect(')');
	return e;
}
static uint32_t pparen_osec(void)
{
	sexpect('(');
	char *n = spatname();
	sexpect(')');
	return get_osec(n);
}

static Expr *pprimary(void)
{
	sskip();
	if (speek('('))
		return pparen();
	if (sconsume('~')) { Expr *e = mk(E_NOT); e->a = pprimary(); return e; }
	if (sconsume('!')) { Expr *e = mk(E_LNOT); e->a = pprimary(); return e; }
	if (sconsume('-')) { Expr *e = mk(E_NEG); e->a = pprimary(); return e; }
	if (sconsume('+')) return pprimary();
	sskip();
	const char *b = sp;
	if (sp < sp_end && *sp == '"') {
		char *n = sname();
		uint32_t g = gintern(n);
		note_ref(g);
		Expr *e = mk(E_SYM);
		e->sym = g;
		return e;
	}
	while (sp < sp_end && (isalnum((unsigned char)*sp) || strchr("_.$", *sp)))
		sp++;
	if (sp == b) {
		serr("expected expression");
		sp = sp_end;
		return mk(E_NUM);
	}
	char *tok = sdup(b, sp);
	if (!strcmp(tok, ".")) return mk(E_DOT);
	if (isdigit((unsigned char)tok[0])) {
		Expr *e = mk(E_NUM);
		char *end;
		uint64_t v;
		size_t n = strlen(tok);
		if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') && !(tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))) {
			tok[n - 1] = 0;
			v = strtoull(tok, &end, 16);
		} else {
			uint64_t mul = 1;
			if (tok[n - 1] == 'K' || tok[n - 1] == 'k') { mul = 1024; tok[n - 1] = 0; }
			else if (tok[n - 1] == 'M' || tok[n - 1] == 'm') { mul = 1024 * 1024; tok[n - 1] = 0; }
			v = strtoull(tok, &end, 0) * mul;
		}
		if (*end)
			serr("malformed number");
		e->num = v;
		return e;
	}
	Expr *e;
	if (!strcmp(tok, "ABSOLUTE")) { e = mk(E_ABSOLUTE); e->a = pparen(); return e; }
	if (!strcmp(tok, "ADDR")) {
		e = mk(E_ADDR);
		e->sym = pparen_osec();
		osecs.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "LOADADDR")) {
		e = mk(E_LOADADDR);
		e->sym = pparen_osec();
		osecs.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "SIZEOF")) { e = mk(E_SIZEOF); e->sym = pparen_osec(); return e; }
	if (!strcmp(tok, "ALIGNOF")) { e = mk(E_ALIGNOF); e->sym = pparen_osec(); return e; }
	if (!strcmp(tok, "SIZEOF_HEADERS")) return mk(E_SIZEOF_HEADERS);
	if (!strcmp(tok, "ALIGN")) {
		sexpect('(');
		Expr *a = pexpr();
		if (sconsume(')')) { e = mk(E_ALIGN1); e->a = a; return e; }
		sexpect(',');
		e = mk(E_ALIGN2);
		e->a = a;
		e->b = pexpr();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "ASSERT")) {
		sexpect('(');
		e = mk(E_ASSERT);
		e->a = pexpr();
		sexpect(',');
		e->msg = sname();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "DEFINED")) {
		sexpect('(');
		e = mk(E_DEFINED);
		e->sym = gintern(spatname());
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "MAX") || !strcmp(tok, "MIN")) {
		e = mk(tok[1] == 'A' ? E_MAX : E_MIN);
		sexpect('(');
		e->a = pexpr();
		sexpect(',');
		e->b = pexpr();
		sexpect(')');
		return e;
	}
	if (!strcmp(tok, "LOG2CEIL")) { e = mk(E_LOG2CEIL); e->a = pparen(); return e; }
	if (!strcmp(tok, "CONSTANT")) {
		sexpect('(');
		char *n = spatname();
		sexpect(')');
		e = mk(E_NUM);
		e->num = !strcmp(n, "MAXPAGESIZE") ? opt.max_page : 4096;
		return e;
	}
	uint32_t g = gintern(tok);
	note_ref(g);
	e = mk(E_SYM);
	e->sym = g;
	return e;
}

static uint32_t get_osec(const char *name)
{
	if (strcmp(name, "/DISCARD/"))
		for (uint32_t i = 0; i < osecs.n; i++)
			if (!strcmp(osecs.v[i].name, name))
				return i;
	OSec o = {.name = name, .type = SHT_PROGBITS, .addralign = 1, .ptload = -1};
	o.discard = !strcmp(name, "/DISCARD/");
	VPUSH(osecs, o);
	return osecs.n - 1;
}

/* After `name` was read: op= expr.  Returns NULL if not an assignment. */
static Assign *passign_rest(const char *name)
{
	char op[4];
	int n = peekop(op);
	if (!n || !strchr(op, '=') || !strcmp(op, "==") || !strcmp(op, "<=") || !strcmp(op, ">=") || !strcmp(op, "!="))
		return NULL;
	sp += n;
	Assign *a = alloc(sizeof *a);
	a->op = op[0] == '=' ? '=' : op[0];
	a->sym = strcmp(name, ".") ? gintern(name) : NONE;
	a->e = pexpr();
	return a;
}

static Assign *pprovide(bool hidden)
{
	sexpect('(');
	char *name = sname();
	Assign *a;
	VPUSH(provides, ((Provide){.sym = gintern(name)}));
	cur_provide = &provides.v[provides.n - 1];
	a = passign_rest(name);
	cur_provide = NULL;
	if (!a) {
		serr("expected assignment");
		return NULL;
	}
	a->provide = true;
	a->hidden = hidden;
	sexpect(')');
	return a;
}

static Assign *passert(void)
{
	Assign *a = alloc(sizeof *a);
	a->sym = NONE;
	a->op = '=';
	a->assert_ = true;
	Expr *e = mk(E_ASSERT);
	sexpect('(');
	e->a = pexpr();
	sexpect(',');
	e->msg = sname();
	sexpect(')');
	a->e = e;
	return a;
}

static void pinput(OSec *os, uint32_t osi, const char *filepat, bool keep)
{
	if (strcmp(filepat, "*"))
		error("linker script: file pattern '%s' not supported", filepat);
	ISD d = {.osec = osi, .keep = keep, .thunksec = NONE};
	VEC(Pat) pats = {0};
	sexpect('(');
	while (!satend() && !speek(')')) {
		Pat p = {0};
		VEC(const char *) g = {0};
		const char *save = sp;
		char *w = spatname();
		if ((!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_ALIGNMENT")) && speek('(')) {
			p.sort = w[5] == 'B' && w[8] == 'A' ? SORT_ALIGN : SORT_NAME;
			sexpect('(');
			while (!satend() && !speek(')'))
				VPUSH(g, spatname());
			sexpect(')');
		} else if (!strcmp(w, "EXCLUDE_FILE")) {
			error("linker script: EXCLUDE_FILE not supported");
			sp = save;
			return;
		} else {
			VPUSH(g, w);
			/* consecutive plain globs belong to the same pattern */
			for (;;) {
				const char *s2 = sp;
				sskip();
				if (speek(')'))
					break;
				char *w2 = spatname();
				if (!strncmp(w2, "SORT", 4) || !strcmp(w2, "EXCLUDE_FILE")) {
					sp = s2;
					break;
				}
				VPUSH(g, w2);
			}
		}
		p.globs = g.v;
		p.nglobs = g.n;
		VPUSH(pats, p);
	}
	sexpect(')');
	d.pats = pats.v;
	d.npats = pats.n;
	VPUSH(isds, d);
	Cmd c = {.kind = C_ISD, .idx = isds.n - 1};
	VPUSH(os->cmds, c);
}

static void poutsec(const char *name)
{
	uint32_t osi = get_osec(name);
	uint32_t nref0 = refsyms.n;
	OSec *os;
	Expr *addr = NULL;
	bool info = false, noload = false;
	/* optional address and (TYPE) */
	if (!speek(':')) {
		if (speek('(')) {
			const char *save = sp;
			sp++;
			char *t = spatname();
			if (!strcmp(t, "INFO") || !strcmp(t, "NOLOAD") || !strcmp(t, "COPY") || !strcmp(t, "OVERLAY")) {
				sexpect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			} else {
				sp = save;
				addr = pexpr();
			}
		} else {
			addr = pexpr();
			if (speek('(')) {
				sp++;
				char *t = spatname();
				sexpect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			}
		}
	}
	sexpect(':');
	os = &osecs.v[osi];
	os->addr = addr;
	os->nonalloc |= info;
	os->noload |= noload;
	for (;;) {
		if (sword("AT")) { Expr *e = pparen(); osecs.v[osi].lma = e; continue; }
		if (sword("ALIGN")) { Expr *e = pparen(); osecs.v[osi].alignx = e; continue; }
		if (sword("SUBALIGN")) { Expr *e = pparen(); osecs.v[osi].subalign = e; continue; }
		break;
	}
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		if (sword("KEEP")) {
			sexpect('(');
			char *fp = spatname();
			pinput(&osecs.v[osi], osi, fp, true);
			sexpect(')');
			continue;
		}
		if (sword("CONSTRUCTORS"))
			continue;
		if (sword("ASSERT")) {
			Assign *a = passert();
			Cmd c = {.kind = C_ASSIGN, .as = a};
			VPUSH(osecs.v[osi].cmds, c);
			sconsume(';');
			continue;
		}
		int pk = sword("PROVIDE") ? 1 : sword("PROVIDE_HIDDEN") ? 2 : sword("HIDDEN") ? 3 : 0;
		if (pk) {
			bool hid = pk >= 2, prov = pk <= 2;
			Assign *a = pprovide(hid);
			if (a) {
				a->provide = prov;
				Cmd c = {.kind = C_ASSIGN, .as = a};
				VPUSH(osecs.v[osi].cmds, c);
			}
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			Cmd c = {.kind = C_ASSIGN, .as = a};
			VPUSH(osecs.v[osi].cmds, c);
			sexpect(';');
			continue;
		}
		sp = save;
		w = spatname();
		if (!*w) {
			serr("unexpected token in output section");
			sp = sp_end;
			break;
		}
		pinput(&osecs.v[osi], osi, w, false);
	}
	sexpect('}');
	/* trailers: >region, AT>region, :phdr, =fill */
	for (;;) {
		sskip();
		if (sp < sp_end && (*sp == '>' || *sp == '=')) {
			error("linker script: memory regions/fill not supported in %s", name);
			sp = sp_end;
			break;
		}
		if (speek(':') && sp + 1 < sp_end && sp[1] != ':') {
			error("linker script: PHDRS not supported in %s", name);
			sp = sp_end;
			break;
		}
		break;
	}
	sconsume(',');
	if (refsyms.n > nref0)
		osecs.v[osi].uses_syms = true;
	Cmd c = {.kind = C_OSEC, .idx = osi};
	VPUSH(topcmds, c);
}

static void psections(void)
{
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		if (sword("ASSERT")) {
			Assign *a = passert();
			VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		if (!*w) {
			sp = save;
			serr("unexpected token in SECTIONS");
			sp = sp_end;
			return;
		}
		poutsec(w);
	}
	sexpect('}');
}

static void parse_script(const char *text, size_t len)
{
	sp = sp_file = text;
	sp_end = text + len;
	while (!satend()) {
		if (sconsume(';'))
			continue;
		if (sword("SECTIONS")) { psections(); continue; }
		if (sword("OUTPUT_ARCH") || sword("OUTPUT_FORMAT") || sword("TARGET") || sword("SEARCH_DIR")) {
			sexpect('(');
			while (!satend() && !speek(')'))
				sp++;
			sexpect(')');
			continue;
		}
		if (sword("ENTRY")) {
			sexpect('(');
			entry_name = sname();
			sexpect(')');
			continue;
		}
		if (sword("ASSERT")) {
			Assign *a = passert();
			VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VPUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}));
			sexpect(';');
			continue;
		}
		sp = save;
		serr("unsupported linker script command");
		return;
	}
}

/* ---------------- glob matching ---------------- */

static bool glob(const char *p, const char *s)
{
	const char *star_p = NULL, *star_s = NULL;
	while (*s) {
		if (*p == '*') {
			star_p = ++p;
			star_s = s;
			continue;
		}
		if (*p == '[') {
			const char *q = p + 1;
			bool neg = false, ok = false;
			if (*q == '^' || *q == '!') { neg = true; q++; }
			const char *start = q;
			while (*q && (*q != ']' || q == start)) {
				if (q[1] == '-' && q[2] && q[2] != ']') {
					if ((unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2])
						ok = true;
					q += 3;
				} else {
					if (*q == *s)
						ok = true;
					q++;
				}
			}
			if (*q == ']' && ok != neg) {
				p = q + 1;
				s++;
				continue;
			}
		} else if (*p == '?' || (*p && *p == *s)) {
			if (*p == '\\' && p[1]) { }
			p++;
			s++;
			continue;
		}
		if (!star_p)
			return false;
		p = star_p;
		s = ++star_s;
	}
	while (*p == '*')
		p++;
	return !*p;
}

static bool pat_match(const Pat *p, const char *name)
{
	for (uint32_t i = 0; i < p->nglobs; i++)
		if (glob(p->globs[i], name))
			return true;
	return false;
}

/* ---------------- loading ---------------- */

static uint8_t *map_file(const char *path, size_t *size)
{
	int fd = openat(dirfd_base, path, O_RDONLY);
	if (fd < 0) {
		error("cannot open %s: %s", path, strerror(errno));
		return NULL;
	}
	struct stat st;
	fstat(fd, &st);
	*size = st.st_size;
	void *p = st.st_size ? mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : (void *)"";
	close(fd);
	if (p == MAP_FAILED) {
		error("cannot mmap %s", path);
		return NULL;
	}
	return p;
}

typedef struct { const char *path; const uint8_t *buf; size_t size; } Pending;
static VEC(Pending) pending;

static void add_archive(const char *path, const uint8_t *b, size_t n)
{
	bool thin = !memcmp(b, "!<thin>\n", 8);
	const char *dir = "";
	const char *slash = strrchr(path, '/');
	if (thin && slash)
		dir = sdup(path, slash + 1);
	size_t off = 8;
	const char *longn = NULL;
	size_t longlen = 0;
	while (off + 60 <= n) {
		const char *h = (const char *)b + off;
		char sz[11];
		memcpy(sz, h + 48, 10);
		sz[10] = 0;
		size_t size = strtoull(sz, NULL, 10);
		off += 60;
		char nm[17];
		memcpy(nm, h, 16);
		nm[16] = 0;
		for (int i = 15; i >= 0 && nm[i] == ' '; i--)
			nm[i] = 0;
		const char *name;
		if (!strcmp(nm, "/") || !strcmp(nm, "/SYM64/")) {
			off += size + (size & 1);
			continue;
		}
		if (!strcmp(nm, "//")) {
			longn = (const char *)b + off;
			longlen = size;
			off += size + (size & 1);
			continue;
		}
		if (nm[0] == '/' && longn) {
			size_t i = strtoull(nm + 1, NULL, 10);
			const char *e = longn + i;
			while (e < longn + longlen && *e != '\n')
				e++;
			if (e > longn + i && e[-1] == '/')
				e--;
			name = sdup(longn + i, e);
		} else {
			size_t l = strlen(nm);
			if (l && nm[l - 1] == '/')
				nm[l - 1] = 0;
			name = astrdup(nm);
		}
		if (thin) {
			char *full = alloc(strlen(dir) + strlen(name) + 1);
			strcpy(full, name[0] == '/' ? "" : dir);
			strcat(full, name);
			VPUSH(pending, ((Pending){.path = full}));
		} else {
			char *full = alloc(strlen(path) + strlen(name) + 3);
			sprintf(full, "%s(%s)", path, name);
			VPUSH(pending, ((Pending){.path = full, .buf = b + off, .size = size}));
			off += size + (size & 1);
		}
	}
}

static void parse_obj(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf) {
		o->buf = map_file(o->path, &o->size);
		o->mapped = true;
		if (!o->buf)
			return;
	}
	const Ehdr *eh = (const Ehdr *)o->buf;
	if (o->size < sizeof(Ehdr) || memcmp(eh->e_ident, "\177ELF", 4) || eh->e_ident[4] != 2 ||
	    eh->e_ident[5] != 1) {
		error("%s: not an ELF64 little-endian file", o->path);
		o->buf = NULL;
		return;
	}
	if (eh->e_type != 1 || eh->e_machine != 183) {
		error("%s: not an AArch64 relocatable object", o->path);
		o->buf = NULL;
		return;
	}
	o->sh = (const Shdr *)(o->buf + eh->e_shoff);
	o->nsh = eh->e_shnum;
	o->shstr = (const char *)o->buf + o->sh[eh->e_shstrndx].sh_offset;
	for (uint32_t s = 0; s < o->nsh; s++) {
		if (o->sh[s].sh_type == SHT_SYMTAB) {
			o->sym = (const Sym *)(o->buf + o->sh[s].sh_offset);
			o->nsym = o->sh[s].sh_size / sizeof(Sym);
			o->firstg = o->sh[s].sh_info;
			o->str = (const char *)o->buf + o->sh[o->sh[s].sh_link].sh_offset;
		} else if (o->sh[s].sh_type == SHT_GROUP) {
			error("%s: section groups not supported", o->path);
		}
	}
	if (o->nsym > o->firstg)
		o->gs = alloc((o->nsym - o->firstg) * sizeof(uint32_t));
}

static bool is_content(const Shdr *s)
{
	switch (s->sh_type) {
	case SHT_PROGBITS: case SHT_NOBITS: case SHT_NOTE: case SHT_INIT_ARRAY:
	case SHT_FINI_ARRAY: case SHT_PREINIT_ARRAY:
		return true;
	default:
		return false;
	}
}

static void fill_isecs(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];
		ISec *is = &isecs.v[o->isec0 + s];
		is->obj = o->idx;
		is->shndx = s;
		is->osec = NONE;
		is->isd = NONE;
		is->aux = NONE;
		is->name = o->shstr + h->sh_name;
		if (h->sh_type == SHT_RELA) {
			if (h->sh_info < o->nsh) {
				ISec *t = &isecs.v[o->isec0 + h->sh_info];
				t->rela = (const Rela *)(o->buf + h->sh_offset);
				t->nrela = h->sh_size / sizeof(Rela);
			}
			continue;
		}
		if (h->sh_type == SHT_REL) {
			error("%s: SHT_REL not supported", o->path);
			continue;
		}
		if (!is_content(h) || (h->sh_flags & SHF_EXCLUDE && !opt.relocatable))
			continue;
		if (opt.strip_debug && !(h->sh_flags & SHF_ALLOC) && !strncmp(is->name, ".debug", 6))
			continue;
		is->kind = K_NORMAL;
		is->data = h->sh_type == SHT_NOBITS ? NULL : o->buf + h->sh_offset;
		is->size = h->sh_size;
		is->flags = h->sh_flags & ~(uint64_t)SHF_GROUP;
		is->type = h->sh_type;
		is->align = h->sh_addralign ? h->sh_addralign : 1;
		is->entsize = h->sh_entsize;
		if (!opt.relocatable && (h->sh_flags & SHF_MERGE) && h->sh_size && h->sh_entsize) {
			if (h->sh_size % h->sh_entsize)
				error("%s:(%s): SHF_MERGE section size must be a multiple of sh_entsize", o->path, is->name);
			if (h->sh_flags & SHF_WRITE)
				error("%s:(%s): writable SHF_MERGE section is not supported", o->path, is->name);
			is->kind = K_MERGE_IN;
		}
	}
}

/* ---------------- symbol resolution ---------------- */

static inline uint64_t symkey(uint32_t obj, uint32_t idx) { return (uint64_t)obj << 32 | idx; }

static void resolve_obj(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = o->firstg; s < o->nsym; s++) {
		const Sym *y = &o->sym[s];
		uint8_t bind = y->st_info >> 4;
		if (bind == STB_LOCAL) {
			error("%s: local symbol after sh_info", o->path);
			continue;
		}
		uint32_t g = gintern(o->str + y->st_name);
		o->gs[s - o->firstg] = g;
		G *gs = &gsyms[g];
		uint64_t k = symkey(o->idx, s);
		atomic_min64(&gs->first, k);
		atomic_min8(&gs->visrank, visrank(y->st_other));
		if (y->st_shndx == SHN_COMMON)
			error("%s: common symbol %s not supported", o->path, o->str + y->st_name);
		else if (y->st_shndx == SHN_UNDEF) {
			if (bind != STB_WEAK && !atomic_load_explicit(&gs->strongref, memory_order_relaxed))
				atomic_store_explicit(&gs->strongref, 1, memory_order_relaxed);
		} else
			atomic_min64(&gs->def, (uint64_t)(bind == STB_WEAK) << 63 | k);
	}
}

static void check_dups(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = o->firstg; s < o->nsym; s++) {
		const Sym *y = &o->sym[s];
		if (y->st_shndx == SHN_UNDEF || (y->st_info >> 4) == STB_WEAK)
			continue;
		G *g = &gsyms[o->gs[s - o->firstg]];
		uint64_t d = kget(&g->def);
		if ((d >> 63) == 0 && d != symkey(o->idx, s)) {
			Obj *first = objs.v[(d >> 32) & 0x7fffffff];
			error("duplicate symbol: %s\n>>> defined in %s\n>>> defined in %s",
			      o->str + y->st_name, first->path, o->path);
		}
	}
}

/* fill state/value of every global from its winning definition */
static void finish_sym(void *arg, size_t i)
{
	(void)arg;
	uint32_t g = gorder.v[i];
	G *s = &gsyms[g];
	s->vis = rankvis(vget(&s->visrank));
	s->thunk = NONE;
	uint64_t d = kget(&s->def);
	if (s->state == GS_SCRIPT)
		return;
	if (d == UINT64_MAX) {
		s->state = GS_UNDEF;
		s->obj = NONE;
		/* weak only if every reference is weak; type from the first one */
		s->bind = s->strongref ? STB_GLOBAL : STB_WEAK;
		uint64_t f = kget(&s->first);
		if ((f >> 32) < objs.n) {
			Obj *o = objs.v[f >> 32];
			const Sym *y = &o->sym[(uint32_t)f];
			s->type = y->st_info & 15;
		} else {
			s->bind = STB_GLOBAL;
		}
		return;
	}
	uint32_t oi = (d >> 32) & 0x7fffffff, si = (uint32_t)d;
	Obj *o = objs.v[oi];
	const Sym *y = &o->sym[si];
	s->state = GS_OBJ;
	s->obj = oi;
	s->symidx = si;
	s->bind = y->st_info >> 4;
	s->type = y->st_info & 15;
	s->size = y->st_size;
	s->val = y->st_value;
	s->sec = y->st_shndx == SHN_ABS ? NONE : o->isec0 + y->st_shndx;
	if (y->st_shndx >= SHN_ABS && y->st_shndx != SHN_ABS)
		error("%s: unsupported section index for %s", o->path, o->str + y->st_name);
}

/* ---------------- relocation targets ---------------- */

typedef struct {
	uint64_t va;			/* S + A, merge-aware */
	uint64_t s;			/* S (without addend; for relaxation) */
	bool abs;			/* target is absolute (no dynamic relocation) */
	bool undef;
	bool discarded;
	uint32_t key;			/* identity for ADRP relaxation */
	bool global;
} Target;

static void reloc_target(const Obj *o, uint32_t symidx, int64_t addend, Target *t)
{
	t->undef = t->discarded = false;
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		G *s = &gsyms[g];
		t->key = g;
		t->global = true;
		if (s->state == GS_UNDEF || s->demoted) {
			t->undef = true;
			t->abs = true;
			t->s = 0;
			t->va = addend;
			return;
		}
		t->abs = gsym_isabs(g);
		t->s = gsym_va(g);
		if (s->state == GS_OBJ && s->sec != NONE) {
			ISec *is = &isecs.v[s->sec];
			if (is->kind == K_MERGE_IN) {
				t->va = isec_va(s->sec, s->val + addend) ;
				/* non-section symbol: piece of value, then + addend */
				t->va = isec_va(s->sec, s->val) + addend;
				return;
			}
		}
		t->va = t->s + addend;
		return;
	}
	const Sym *y = &o->sym[symidx];
	t->key = symidx;
	t->global = false;
	if (y->st_shndx == SHN_ABS) {
		t->abs = true;
		t->s = y->st_value;
		t->va = t->s + addend;
		return;
	}
	if (y->st_shndx == SHN_UNDEF || y->st_shndx >= 0xff00) {
		t->undef = true;
		t->abs = true;
		t->s = 0;
		t->va = addend;
		return;
	}
	uint32_t sec = o->isec0 + y->st_shndx;
	ISec *is = &isecs.v[sec];
	t->abs = false;
	uint32_t os = isec_osec(sec);
	if (is->kind == K_NONE || os == OS_DISCARD || os == NONE) {
		t->discarded = true;
		t->s = 0;
		t->va = addend;
		return;
	}
	if ((y->st_info & 15) == STT_SECTION) {
		t->s = isec_va(sec, 0);
		t->va = isec_va(sec, (uint64_t)addend);
		if (is->kind != K_MERGE_IN)
			t->va = t->s + addend;
		return;
	}
	t->s = isec_va(sec, y->st_value);
	t->va = t->s + addend;
}

/* ---------------- output state ---------------- */

static uint8_t *out;			/* output file image */
static uint64_t filesize, shoff;
static VEC(Phdr) phdrs;
typedef struct { int first, last; bool lma_set; } PhdrSecs;
static VEC(PhdrSecs) phsecs;		/* first/last osec of each phdr */
static VEC(uint32_t) outsecs;		/* live output sections in header order */
static uint64_t ndyn;			/* RELATIVE relocations */
static uint32_t reladyn_isec = NONE, buildid_isec = NONE, comment_isec = NONE;
static uint32_t symtab_isec = NONE, strtab_isec = NONE, shstrtab_isec = NONE;
static uint32_t nobj_real;		/* objects excluding synthetic */

static uint32_t new_isec(const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ISec is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = NONE, .osec = NONE, .isd = NONE, .aux = NONE, .shndx = NONE};
	VPUSH(isecs, is);
	return isecs.n - 1;
}

/* ---------------- thunks ---------------- */

typedef struct {
	uint32_t g;			/* destination global (or NONE) */
	uint32_t sec;			/* destination ISec for local/section targets */
	uint64_t val;
	int64_t addend;
	uint32_t tsec;			/* thunk ISec */
	uint32_t off;			/* offset in thunk section */
	uint32_t size;			/* 4 (b) or 12 (adrp/add/br) */
	bool lng;			/* long form; sticky once chosen (lld) */
	const char *name;
} Thunk;
static VEC(Thunk) thunks;
typedef struct { VEC(uint32_t) list; } ThunkSec;
static VEC(ThunkSec) thunksecs;

/* thunk redirection: (isec, reloc index) -> thunk; open hash */
static uint64_t *tr_keys;
static uint32_t *tr_vals;
static uint32_t tr_mask, tr_n;

static void tr_put(uint32_t isec, uint32_t ri, uint32_t th)
{
	if (!tr_keys || tr_n * 2 >= tr_mask) {
		uint32_t nm = tr_keys ? tr_mask * 2 + 1 : 1023;
		uint64_t *nk = alloc((uint64_t)(nm + 1) * 8);
		uint32_t *nv = alloc((uint64_t)(nm + 1) * 4);
		memset(nk, 0xff, (uint64_t)(nm + 1) * 8);
		for (uint32_t i = 0; tr_keys && i <= tr_mask; i++)
			if (tr_keys[i] != UINT64_MAX) {
				uint32_t j = (uint32_t)(tr_keys[i] * 0x9E3779B97F4A7C15ULL >> 32) & nm;
				while (nk[j] != UINT64_MAX)
					j = (j + 1) & nm;
				nk[j] = tr_keys[i];
				nv[j] = tr_vals[i];
			}
		tr_keys = nk;
		tr_vals = nv;
		tr_mask = nm;
	}
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & tr_mask;
	while (tr_keys[j] != UINT64_MAX && tr_keys[j] != k)
		j = (j + 1) & tr_mask;
	if (tr_keys[j] == UINT64_MAX)
		tr_n++;
	tr_keys[j] = k;
	tr_vals[j] = th;
}
static uint32_t tr_get(uint32_t isec, uint32_t ri)
{
	if (!tr_keys)
		return NONE;
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & tr_mask;
	while (tr_keys[j] != UINT64_MAX) {
		if (tr_keys[j] == k)
			return tr_vals[j];
		j = (j + 1) & tr_mask;
	}
	return NONE;
}

static bool fits_int(int64_t v, int bits);
static uint64_t thunk_va(uint32_t t)
{
	Thunk *th = &thunks.v[t];
	return isec_va(th->tsec, th->off);
}
static uint64_t thunk_dest(uint32_t t)
{
	Thunk *th = &thunks.v[t];
	if (th->g != NONE)
		return gsym_va(th->g) + th->addend;
	return isec_va(th->sec, th->val) + th->addend;
}

static bool in_branch_range(uint64_t src, uint64_t dst)
{
	uint64_t range = 128 * 1024 * 1024;
	if (dst > src)
		return dst - src <= range - 4;
	return src - dst <= range;
}

/* ---------------- layout ---------------- */

static uint64_t header_size(void)
{
	return sizeof(Ehdr) + (uint64_t)phdrs.n * sizeof(Phdr);
}

/* ---------------- section assignment ---------------- */

typedef struct { const char *name; uint32_t h; uint32_t isd; } NameEnt;
static NameEnt *ncache;
static uint32_t ncmask;

static uint32_t match_isd(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = strhash(name, n);
	uint32_t j = h & ncmask;
	while (ncache[j].name) {
		if (ncache[j].h == h && !strcmp(ncache[j].name, name))
			return ncache[j].isd;
		j = (j + 1) & ncmask;
	}
	uint32_t res = NONE;
	for (uint32_t i = 0; i < topcmds.n && res == NONE; i++) {
		Cmd *c = &topcmds.v[i];
		if (c->kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[c->idx];
		for (uint32_t k = 0; k < os->cmds.n && res == NONE; k++) {
			Cmd *cc = &os->cmds.v[k];
			if (cc->kind != C_ISD)
				continue;
			ISD *d = &isds.v[cc->idx];
			for (uint32_t p = 0; p < d->npats; p++)
				if (pat_match(&d->pats[p], name)) {
					res = cc->idx;
					break;
				}
		}
	}
	ncache[j] = (NameEnt){.name = name, .h = h, .isd = res};
	return res;
}

static int cmp_name_stable(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	int c = strcmp(isecs.v[x].name, isecs.v[y].name);
	return c ? c : (x > y) - (x < y);
}
static int cmp_align_stable(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	uint32_t ax = isecs.v[x].align, ay = isecs.v[y].align;
	if (ax != ay)
		return ax > ay ? -1 : 1;
	return (x > y) - (x < y);
}

static VEC(uint32_t) orphans;

static void commit_section(OSec *os, ISec *is)
{
	if (os->type != is->type) {
		if (!os->has_input)
			os->type = is->type;
		else if (os->type != SHT_NOBITS || is->type != SHT_NOBITS)
			os->type = SHT_PROGBITS;
	}
	if (!os->has_input) {
		os->has_input = true;
		os->entsize = is->entsize;
		os->flags = is->flags;
	} else {
		if (os->entsize != is->entsize)
			os->entsize = 0;
		os->flags |= is->flags;
	}
	if (os->nonalloc)
		os->flags &= ~(uint64_t)SHF_ALLOC;
	if (is->align > os->addralign)
		os->addralign = is->align;
}

static void assign_sections(void)
{
	uint32_t ncap = 1024;
	while (ncap < isecs.n / 4)
		ncap *= 2;
	ncache = alloc(ncap * sizeof *ncache);
	ncmask = ncap - 1;
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];
		if (is->kind == K_NONE)
			continue;
		uint32_t d = match_isd(is->name);
		if (d == NONE) {
			if (is->kind != K_SYMTAB && is->kind != K_STRTAB && is->kind != K_SHSTRTAB)
				VPUSH(orphans, i);
			continue;
		}
		ISD *isd = &isds.v[d];
		if (osecs.v[isd->osec].discard) {
			is->osec = OS_DISCARD;
			continue;
		}
		is->isd = d;
		VPUSH(isd->secs, i);
	}
	for (uint32_t i = 0; i < isds.n; i++) {
		ISD *d = &isds.v[i];
		if (d->npats == 1 && d->pats[0].sort != SORT_NONE)
			qsort(d->secs.v, d->secs.n, sizeof(uint32_t),
			      d->pats[0].sort == SORT_NAME ? cmp_name_stable : cmp_align_stable);
		else
			for (uint32_t p = 0; p < d->npats; p++)
				if (d->pats[p].sort != SORT_NONE)
					error("linker script: SORT mixed with other patterns not supported");
	}
	for (uint32_t i = 0; i < orphans.n; i++) {
		ISec *is = &isecs.v[orphans.v[i]];
		if (!strcmp(is->name, ".note.GNU-stack") || (is->flags & SHF_EXCLUDE))
			is->osec = OS_DISCARD;
		else
			warn("%s:(%s) is being placed in '%s'", is->obj != NONE ? objs.v[is->obj]->path : "<internal>",
			     is->name, is->name);
	}
	/* orphans: own output section after everything (rare; lld would rank them) */
	for (uint32_t i = 0; i < orphans.n; i++) {
		ISec *is = &isecs.v[orphans.v[i]];
		if (is->osec == OS_DISCARD)
			continue;
		uint32_t found = NONE;
		for (uint32_t k = 0; k < osecs.n; k++)
			if (!strcmp(osecs.v[k].name, is->name))
				found = k;
		if (found == NONE) {
			found = get_osec(is->name);
			VPUSH(topcmds, ((Cmd){.kind = C_OSEC, .idx = found}));
		}
		OSec *os = &osecs.v[found];
		uint32_t di = NONE;
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD)
				di = os->cmds.v[k].idx;
		if (di == NONE) {
			ISD d = {.osec = found, .thunksec = NONE};
			VPUSH(isds, d);
			di = isds.n - 1;
			VPUSH(osecs.v[found].cmds, ((Cmd){.kind = C_ISD, .idx = di}));
		}
		is->isd = di;
		VPUSH(isds.v[di].secs, orphans.v[i]);
	}

	/* merge sections: one synthetic per (flags, entsize[, align]) per output section */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		uint32_t oi = topcmds.v[c].idx;
		OSec *os = &osecs.v[oi];
		uint32_t first_merge = merges.n;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != C_ISD)
				continue;
			ISD *d = &isds.v[os->cmds.v[k].idx];
			uint32_t w = 0;
			for (uint32_t s = 0; s < d->secs.n; s++) {
				uint32_t si = d->secs.v[s];
				ISec *is = &isecs.v[si];
				if (is->kind != K_MERGE_IN) {
					d->secs.v[w++] = si;
					continue;
				}
				uint32_t m;
				for (m = first_merge; m < merges.n; m++) {
					Merge *mg = &merges.v[m];
					if (mg->flags == is->flags && mg->entsize == is->entsize &&
					    (mg->align == is->align || !(mg->flags & SHF_STRINGS)))
						break;
				}
				if (m == merges.n) {
					Merge mg = {.flags = is->flags, .entsize = is->entsize, .align = is->align,
						    .osec = oi};
					uint32_t syn = new_isec(isecs.v[si].name, K_MERGE, isecs.v[si].type,
								isecs.v[si].flags, isecs.v[si].align);
					is = &isecs.v[si];
					mg.isec = syn;
					isecs.v[syn].aux = m;
					isecs.v[syn].entsize = is->entsize;
					isecs.v[syn].isd = os->cmds.v[k].idx;
					VPUSH(merges, mg);
					d->secs.v[w++] = syn;
				}
				Merge *mg = &merges.v[m];
				if (is->align > mg->align)
					mg->align = is->align;
				MergeIn mi = {.isec = si, .syn = mg->isec};
				VPUSH(mergeins, mi);
				is->aux = mergeins.n - 1;
				VPUSH(mg->members, mergeins.n - 1);
			}
			d->secs.n = w;
		}
		for (uint32_t m = first_merge; m < merges.n; m++)
			isecs.v[merges.v[m].isec].align = merges.v[m].align;
	}
	/* output section ownership */
	for (uint32_t i = 0; i < isds.n; i++)
		for (uint32_t s = 0; s < isds.v[i].secs.n; s++)
			isecs.v[isds.v[i].secs.v[s]].osec = isds.v[i].osec;
}

/* ---------------- merge pieces ---------------- */

static void split_pieces(void *arg, size_t i)
{
	(void)arg;
	MergeIn *m = &mergeins.v[i];
	ISec *is = &isecs.v[m->isec];
	const uint8_t *d = is->data;
	uint64_t n = is->size, es = is->entsize;
	uint32_t cnt = 0;
	if (is->flags & SHF_STRINGS) {
		/* count */
		for (uint64_t p = 0; p < n;) {
			uint64_t q = p;
			if (es == 1) {
				const uint8_t *z = memchr(d + p, 0, n - p);
				if (!z) { error("%s: string is not null terminated", is->name); return; }
				q = z - d + 1;
			} else {
				while (q + es <= n) {
					bool zero = true;
					for (uint64_t k = 0; k < es; k++)
						zero &= d[q + k] == 0;
					q += es;
					if (zero)
						break;
				}
			}
			cnt++;
			p = q;
		}
		m->inoff = alloc(cnt * 4);
		m->hash = alloc(cnt * 4);
		m->outoff = alloc(cnt * 4);
		uint32_t k = 0;
		for (uint64_t p = 0; p < n;) {
			uint64_t q, len;
			if (es == 1) {
				len = strlen((const char *)d + p);
				q = p + len + 1;
			} else {
				q = p;
				for (;;) {
					bool zero = true;
					for (uint64_t j = 0; j < es; j++)
						zero &= d[q + j] == 0;
					if (zero)
						break;
					q += es;
				}
				len = q - p;
				q += es;
			}
			m->inoff[k] = (uint32_t)p;
			m->hash[k] = (uint32_t)ld_xxh3(d + p, len) >> 1;
			k++;
			p = q;
		}
	} else {
		cnt = n / es;
		m->inoff = alloc(cnt * 4);
		m->hash = alloc(cnt * 4);
		m->outoff = alloc(cnt * 4);
		for (uint32_t k = 0; k < cnt; k++) {
			m->inoff[k] = k * es;
			m->hash[k] = (uint32_t)ld_xxh3(d + (uint64_t)k * es, es) >> 1;
		}
	}
	m->npieces = cnt;
}

static inline uint64_t piece_len(const MergeIn *m, uint32_t p)
{
	uint64_t end = p + 1 < m->npieces ? m->inoff[p + 1] : isecs.v[m->isec].size;
	return end - m->inoff[p];
}

/* one shard of one merge section: dedup in insertion order */
static void merge_shard(void *arg, size_t job_i)
{
	(void)arg;
	uint32_t mi = job_i / 32, shard = job_i % 32;
	Merge *mg = &merges.v[mi];
	uint32_t total = 0;
	for (uint32_t k = 0; k < mg->members.n; k++)
		total += mergeins.v[mg->members.v[k]].npieces;
	uint32_t cap = 16;
	while (cap < total / 16 + 16)
		cap *= 2;
	/* table of (mergein, piece) packed in u64; key compare by content */
	uint64_t *tab = alloc((uint64_t)cap * 8);
	memset(tab, 0xff, (uint64_t)cap * 8);
	uint32_t *thash = alloc((uint64_t)cap * 4);
	uint32_t used = 0;
	uint64_t size = 0;
	for (uint32_t k = 0; k < mg->members.n; k++) {
		uint32_t mid = mg->members.v[k];
		MergeIn *m = &mergeins.v[mid];
		const uint8_t *d = isecs.v[m->isec].data;
		for (uint32_t p = 0; p < m->npieces; p++) {
			uint32_t h = m->hash[p];
			if ((h >> 26) != shard)
				continue;
			uint64_t len = piece_len(m, p);
			if ((used + 1) * 2 > cap) {
				/* grow */
				uint32_t nc = cap * 2;
				uint64_t *nt = alloc((uint64_t)nc * 8);
				uint32_t *nh = alloc((uint64_t)nc * 4);
				memset(nt, 0xff, (uint64_t)nc * 8);
				for (uint32_t j = 0; j < cap; j++)
					if (tab[j] != UINT64_MAX) {
						uint32_t x = (thash[j] * 0x9E3779B1u) & (nc - 1);
						while (nt[x] != UINT64_MAX)
							x = (x + 1) & (nc - 1);
						nt[x] = tab[j];
						nh[x] = thash[j];
					}
				tab = nt;
				thash = nh;
				cap = nc;
			}
			uint32_t x = (h * 0x9E3779B1u) & (cap - 1);
			for (;;) {
				if (tab[x] == UINT64_MAX) {
					size = alignup(size, mg->align);
					m->outoff[p] = (uint32_t)size;
					size += len;
					tab[x] = (uint64_t)mid << 32 | p;
					thash[x] = h;
					used++;
					VPUSH(mg->uniq[shard], tab[x]);
					break;
				}
				if (thash[x] == h) {
					MergeIn *m2 = &mergeins.v[tab[x] >> 32];
					uint32_t p2 = (uint32_t)tab[x];
					if (piece_len(m2, p2) == len &&
					    !memcmp(isecs.v[m2->isec].data + m2->inoff[p2], d + m->inoff[p], len)) {
						m->outoff[p] = m2->outoff[p2];
						break;
					}
				}
				x = (x + 1) & (cap - 1);
			}
		}
	}
	mg->shardsize[shard] = size;
}

static void merge_fix(void *arg, size_t i)
{
	(void)arg;
	MergeIn *m = &mergeins.v[i];
	Merge *mg = &merges.v[isecs.v[m->syn].aux];
	for (uint32_t p = 0; p < m->npieces; p++)
		m->outoff[p] += (uint32_t)mg->shardoff[m->hash[p] >> 26];
}

static void finalize_merges(void)
{
	pfor(mergeins.n, split_pieces, NULL);
	pfor((size_t)merges.n * 32, merge_shard, NULL);
	for (uint32_t i = 0; i < merges.n; i++) {
		Merge *mg = &merges.v[i];
		uint64_t off = 0;
		for (int s = 0; s < 32; s++) {
			if (mg->shardsize[s])
				off = alignup(off, mg->align);
			mg->shardoff[s] = off;
			off += mg->shardsize[s];
		}
		isecs.v[mg->isec].size = off;
	}
	pfor(mergeins.n, merge_fix, NULL);
}

/* ---------------- relocation scan ---------------- */

static bool pic;

static void scan_relocs(void *arg, size_t i)
{
	(void)arg;
	ISec *is = &isecs.v[i];
	if (!is->nrela || is->kind == K_NONE || is->obj == NONE)
		return;
	uint32_t os = isec_osec(i);
	if (os == OS_DISCARD || os == NONE)
		return;
	Obj *o = objs.v[is->obj];
	uint32_t n = 0;
	Target t;
	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		if (symi >= o->nsym) {
			error("%s: bad symbol index in relocation", o->path);
			continue;
		}
		reloc_target(o, symi, rl->r_addend, &t);
		if (t.discarded && (is->flags & SHF_ALLOC))
			error("%s:(%s+0x%llx): relocation refers to a discarded section", o->path, is->name,
			      (unsigned long long)rl->r_offset);
		switch (type) {
		case R_AARCH64_ABS64:
			if (pic && (is->flags & SHF_ALLOC) && !t.abs)
				n++;
			break;
		case R_AARCH64_ABS32: case R_AARCH64_ABS16:
			if (pic && (is->flags & SHF_ALLOC) && !t.abs && !t.undef)
				error("%s:(%s+0x%llx): relocation R_AARCH64_ABS32/16 cannot be used against "
				      "a non-absolute symbol; recompile with -fPIC", o->path, is->name,
				      (unsigned long long)rl->r_offset);
			break;
		case R_AARCH64_NONE: case R_AARCH64_PREL64: case R_AARCH64_PREL32: case R_AARCH64_PREL16:
		case R_AARCH64_CALL26: case R_AARCH64_JUMP26: case R_AARCH64_ADR_PREL_PG_HI21:
		case R_AARCH64_ADR_PREL_LO21: case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC: case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC: case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC: case R_AARCH64_LD_PREL_LO19:
			break;
		default:
			error("%s:(%s+0x%llx): unsupported relocation type %u", o->path, is->name,
			      (unsigned long long)rl->r_offset, type);
		}
	}
	is->ndyn = n;
}

/* ---------------- thunk pass ---------------- */

static bool create_thunks(int pass)
{
	bool changed = false;
	Target t;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		uint32_t oi = topcmds.v[c].idx;
		OSec *os = &osecs.v[oi];
		if (!os->live || (os->flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR))
			continue;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != C_ISD)
				continue;
			uint32_t di = os->cmds.v[k].idx;
			ISD *d = &isds.v[di];
			if (pass == 0 && d->secs.n) {
				ISec *last = &isecs.v[d->secs.v[d->secs.n - 1]];
				d->thunk_off = last->off + last->size;
			}
			for (uint32_t s = 0; s < d->secs.n; s++) {
				uint32_t si = d->secs.v[s];
				ISec *is = &isecs.v[si];
				if (is->obj == NONE || !is->nrela)
					continue;
				Obj *o = objs.v[is->obj];
				for (uint32_t r = 0; r < is->nrela; r++) {
					const Rela *rl = &is->rela[r];
					uint32_t type = (uint32_t)rl->r_info;
					if (type != R_AARCH64_CALL26 && type != R_AARCH64_JUMP26)
						continue;
					uint64_t src = isec_va(si, rl->r_offset);
					uint32_t th = tr_get(si, r);
					if (th != NONE) {
						if (in_branch_range(src, thunk_va(th)))
							continue;
					}
					reloc_target(o, rl->r_info >> 32, rl->r_addend, &t);
					if (t.undef)
						continue;
					if (in_branch_range(src, t.va))
						continue;
					/* find a compatible thunk in range */
					uint32_t dg = NONE, dsec = NONE;
					uint64_t dval = 0;
					uint32_t symi = rl->r_info >> 32;
					if (symi >= o->firstg) {
						dg = o->gs[symi - o->firstg];
						G *gs = &gsyms[dg];
						if (gs->state == GS_OBJ && gs->sec != NONE) {
							dsec = gs->sec;
							dval = gs->val;
						}
					} else {
						const Sym *y = &o->sym[symi];
						dsec = o->isec0 + y->st_shndx;
						dval = y->st_value;
					}
					uint32_t found = NONE;
					for (uint32_t x = 0; x < thunks.n; x++) {
						Thunk *tx = &thunks.v[x];
						bool same = dsec != NONE ? (tx->sec == dsec && tx->val == dval)
									 : (tx->g == dg && tx->sec == NONE);
						if (same && tx->addend == rl->r_addend &&
						    in_branch_range(src, thunk_va(x))) {
							found = x;
							break;
						}
					}
					if (found == NONE) {
						if (d->thunksec == NONE) {
							uint32_t ts = new_isec(".text.thunk", K_THUNK, SHT_PROGBITS,
									       SHF_ALLOC | SHF_EXECINSTR, 4);
							d = &isds.v[di];
							VPUSH(thunksecs, ((ThunkSec){0}));
							isecs.v[ts].aux = thunksecs.n - 1;
							isecs.v[ts].osec = oi;
							isecs.v[ts].isd = di;
							isecs.v[ts].off = d->thunk_off;
							d->thunksec = ts;
						}
						Thunk tn = {.g = dsec == NONE ? dg : NONE, .sec = dsec, .val = dval,
							    .addend = rl->r_addend, .tsec = d->thunksec};
						if (dg != NONE)
							tn.g = dsec == NONE ? dg : NONE;
						const char *dn = dg != NONE ? gname(dg) : o->str + o->sym[symi].st_name;
						char *nm = alloc(strlen(dn) + 20);
						sprintf(nm, "__AArch64ADRPThunk_%s", dn);
						tn.name = nm;
						ThunkSec *tsx = &thunksecs.v[isecs.v[d->thunksec].aux];
						tn.off = 0;
						VPUSH(thunks, tn);
						VPUSH(tsx->list, thunks.n - 1);
						found = thunks.n - 1;
						changed = true;
					}
					tr_put(si, r, found);
				}
			}
			/* assign thunk offsets in this ISD's thunk section */
			if (d->thunksec != NONE) {
				ISec *ts = &isecs.v[d->thunksec];
				ThunkSec *tsx = &thunksecs.v[ts->aux];
				uint64_t off = 0;
				for (uint32_t x = 0; x < tsx->list.n; x++) {
					Thunk *tx = &thunks.v[tsx->list.v[x]];
					tx->off = off;
					/* getMayUseShortThunk(): b if the destination is within
					   +-128 MiB, decided against the current layout */
					if (!tx->lng) {
						int64_t dd = (int64_t)(thunk_dest(tsx->list.v[x]) - thunk_va(tsx->list.v[x]));
						tx->lng = !fits_int(dd, 28);
					}
					tx->size = tx->lng ? 12 : 4;
					off += tx->size;
				}
				if (off != ts->size)
					changed = true;
				ts->size = off;
			}
		}
	}
	/* merge newly created thunk sections into their ISD lists */
	for (uint32_t i = 0; i < isds.n; i++) {
		ISD *d = &isds.v[i];
		if (d->thunksec == NONE)
			continue;
		bool present = false;
		for (uint32_t s = d->secs.n; s-- > 0;)
			if (d->secs.v[s] == d->thunksec) {
				present = true;
				break;
			}
		if (present)
			continue;
		/* before trailing sections that start at or after the thunk offset */
		uint32_t pos = d->secs.n;
		while (pos > 0 && isecs.v[d->secs.v[pos - 1]].off >= d->thunk_off)
			pos--;
		VPUSH(d->secs, 0);
		memmove(&d->secs.v[pos + 1], &d->secs.v[pos], (d->secs.n - 1 - pos) * sizeof(uint32_t));
		d->secs.v[pos] = d->thunksec;
	}
	return changed;
}

/* ---------------- output sections: liveness, flags ---------------- */

/* ---------------- phdrs / file layout ---------------- */

static bool sec_in_load(OSec *os) { return (os->flags & SHF_ALLOC) && !((os->flags & SHF_TLS) && os->type == SHT_NOBITS); }
static uint32_t phflags(OSec *os)
{
	uint32_t f = PF_R;
	if (os->flags & SHF_WRITE) f |= PF_W;
	if (os->flags & SHF_EXECINSTR) f |= PF_X;
	return f;
}


static void phadd(int ph, uint32_t oi)
{
	PhdrSecs *p = &phsecs.v[ph];
	if (p->first < 0)
		p->first = oi;
	p->last = oi;
	OSec *os = &osecs.v[oi];
	if (os->addralign > phdrs.v[ph].p_align)
		phdrs.v[ph].p_align = os->addralign;
	if (phdrs.v[ph].p_type == PT_LOAD)
		os->ptload = ph;
}
static int phnew(uint32_t type, uint32_t flags)
{
	Phdr p = {.p_type = type, .p_flags = flags, .p_align = type == PT_LOAD ? opt.max_page : 0};
	VPUSH(phdrs, p);
	VPUSH(phsecs, ((PhdrSecs){.first = -1, .last = -1}));
	return phdrs.n - 1;
}

static void assign_file_offsets(void)
{
	uint64_t off = sizeof(Ehdr) + (uint64_t)phdrs.n * sizeof(Phdr);
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		if (!(os->flags & SHF_ALLOC))
			continue;
		if (os->ptload >= 0 && phsecs.v[os->ptload].first == (int)outsecs.v[i])
			off = alignskew(off, phdrs.v[os->ptload].p_align, os->va);
		else if (os->type == SHT_NOBITS)
			;
		else if (os->ptload < 0)
			off = alignup(off, os->addralign);
		else {
			OSec *f = &osecs.v[phsecs.v[os->ptload].first];
			off = f->offset + os->va - f->va;
		}
		os->offset = off;
		if (os->type != SHT_NOBITS)
			off += os->size;
	}
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		os->offset = alignup(off, os->addralign);
		off = os->offset + os->size;
	}
	shoff = alignup(off, 8);
	filesize = shoff + (uint64_t)(outsecs.n + 1) * sizeof(Shdr);
	for (uint32_t i = 0; i < phdrs.n; i++) {
		Phdr *p = &phdrs.v[i];
		PhdrSecs *s = &phsecs.v[i];
		if (s->first < 0)
			continue;
		OSec *f = &osecs.v[s->first], *l = &osecs.v[s->last];
		p->p_filesz = l->offset - f->offset + (l->type != SHT_NOBITS ? l->size : 0);
		p->p_memsz = l->va + l->size - f->va;
		p->p_offset = f->offset;
		p->p_vaddr = p->p_paddr = f->va;
	}
}

/* ---------------- symbol table ---------------- */

static bool keep_local(const Obj *o, const Sym *y)
{
	uint8_t type = y->st_info & 15;
	if (type == STT_SECTION)
		return false;
	const char *n = o->str + y->st_name;
	if (opt.discard_all)
		return false;
	if (y->st_shndx == SHN_UNDEF)
		return false;		/* not a Defined */
	if (y->st_shndx != SHN_ABS && y->st_shndx < 0xff00) {
		uint32_t s = o->isec0 + y->st_shndx;
		ISec *is = &isecs.v[s];
		uint32_t os = isec_osec(s);
		if (is->kind == K_NONE || os == NONE || os == OS_DISCARD)
			return false;
		if (n[0] == '.' && n[1] == 'L' && (opt.discard_locals || (is->flags & SHF_MERGE)))
			return false;
	} else if (n[0] == '.' && n[1] == 'L' && opt.discard_locals) {
		return false;
	}
	return true;
}

static void count_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	uint32_t n = 0;
	uint64_t str = 0;
	if (o->buf)
		for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++)
			if (keep_local(o, &o->sym[s])) {
				n++;
				str += strlen(o->str + o->sym[s].st_name) + 1;
			}
	o->nloc = n;
	o->locstr = str;
}

static uint16_t sym_shndx_isec(uint32_t sec)
{
	uint32_t os = isec_osec(sec);
	if (os == NONE || os == OS_DISCARD)
		return 0;
	return osecs.v[os].shidx;
}

static void put_sym(Sym *d, uint8_t *strbuf, uint64_t *stroff, const char *name, uint8_t info,
		    uint8_t other, uint16_t shndx, uint64_t val, uint64_t size)
{
	size_t n = strlen(name);
	memcpy(strbuf + *stroff, name, n + 1);
	d->st_name = (uint32_t)*stroff;
	*stroff += n + 1;
	d->st_info = info;
	d->st_other = other;
	d->st_shndx = shndx;
	d->st_value = val;
	d->st_size = size;
}

/* ---------------- writing sections ---------------- */

static void write_le_imm(uint8_t *loc, uint32_t mask, uint32_t v)
{
	wr32(loc, (rd32(loc) & ~mask) | v);
}

static int reloc_error(const Obj *o, const ISec *is, const Rela *rl, const char *what, int64_t v)
{
	error("%s:(%s+0x%llx): relocation %u %s (value 0x%llx)", o->path, is->name,
	      (unsigned long long)rl->r_offset, (unsigned)rl->r_info, what, (unsigned long long)v);
	return 0;
}

static bool fits_int(int64_t v, int bits)
{ return v >= -(INT64_C(1) << (bits - 1)) && v < (INT64_C(1) << (bits - 1)); }
/* checkIntUInt: representable as either signed or unsigned */
static bool fits_intuint(int64_t v, int bits) { return fits_int(v, bits) || (uint64_t)v < (UINT64_C(1) << bits); }

static Rela *reladyn_buf;

static void relocate_isec(uint32_t si, uint8_t *buf)
{
	ISec *is = &isecs.v[si];
	Obj *o = objs.v[is->obj];
	uint64_t secva = isec_va(si, 0);
	Rela *dyn = reladyn_buf ? reladyn_buf + is->dyn0 : NULL;
	uint32_t nd = 0;
	bool alloc_ = is->flags & SHF_ALLOC;
	Target t, t2;
	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		uint8_t *loc = buf + rl->r_offset;
		uint64_t P = secva + rl->r_offset;
		if (rl->r_offset >= is->size + (type == R_AARCH64_NONE)) {
			error("%s:(%s): relocation offset out of range", o->path, is->name);
			continue;
		}
		reloc_target(o, symi, rl->r_addend, &t);
		int64_t v;
		switch (type) {
		case R_AARCH64_NONE:
			break;
		case R_AARCH64_ABS64:
			if (pic && alloc_ && !t.abs) {
				dyn[nd].r_offset = P;
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)t.va;
				nd++;
				if (!opt.no_apply_dyn)
					wr64(loc, t.va);
			} else {
				wr64(loc, t.va);
			}
			break;
		case R_AARCH64_ABS32:
			v = (int64_t)t.va;
			if (!fits_intuint(v, 32))
				reloc_error(o, is, rl, "out of range", v);
			wr32(loc, (uint32_t)v);
			break;
		case R_AARCH64_ABS16: {
			v = (int64_t)t.va;
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_PREL64:
			wr64(loc, t.va - P);
			break;
		case R_AARCH64_PREL32:
			v = (int64_t)(t.va - P);
			if (!fits_intuint(v, 32))
				reloc_error(o, is, rl, "out of range", v);
			wr32(loc, (uint32_t)v);
			break;
		case R_AARCH64_PREL16: {
			v = (int64_t)(t.va - P);
			uint16_t x = (uint16_t)v;
			memcpy(loc, &x, 2);
			break;
		}
		case R_AARCH64_CALL26:
		case R_AARCH64_JUMP26: {
			uint32_t th = tr_get(si, r);
			uint64_t dst = th != NONE ? thunk_va(th) : t.va;
			if (t.undef && th == NONE)
				dst = P + 4;	/* undefined weak: next instruction */
			v = (int64_t)(dst - P);
			if (v & 3)
				reloc_error(o, is, rl, "misaligned branch", v);
			if (!fits_int(v, 28))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, 0x03ffffff, ((uint64_t)v & 0x0ffffffc) >> 2);
			break;
		}
		case R_AARCH64_ADR_PREL_PG_HI21: {
			if (opt.relax && r + 1 < is->nrela) {
				const Rela *r2 = &is->rela[r + 1];
				if ((uint32_t)r2->r_info == R_AARCH64_ADD_ABS_LO12_NC &&
				    rl->r_offset + 4 == r2->r_offset && (rl->r_info >> 32) == (r2->r_info >> 32) &&
				    rl->r_addend == 0 && r2->r_addend == 0) {
					uint32_t adrp = rd32(loc), add = rd32(buf + r2->r_offset);
					uint32_t rd = adrp & 0x1f;
					if ((adrp & 0x9f000000) == 0x90000000 && (add & 0xffc00000) == 0x91000000 &&
					    rd == (add & 0x1f) && rd == ((add >> 5) & 0x1f)) {
						reloc_target(o, r2->r_info >> 32, 0, &t2);
						int64_t d = (int64_t)(t2.s - (secva + r2->r_offset));
						if (d >= -1024 * 1024 && d < 1024 * 1024 && !t2.undef) {
							wr32(loc, 0xd503201f);
							uint32_t adr = 0x10000000 | rd;
							adr |= ((uint32_t)d & 3) << 29;
							adr |= (((uint32_t)(d >> 2)) & 0x7ffff) << 5;
							wr32(buf + r2->r_offset, adr);
							r++;
							break;
						}
					}
				}
			}
			v = (int64_t)((t.va & ~0xfffULL) - (P & ~0xfffULL));
			if (!fits_int(v, 33))
				reloc_error(o, is, rl, "out of range", v);
			uint64_t imm = (uint64_t)v >> 12;
			write_le_imm(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5));
			break;
		}
		case R_AARCH64_ADR_PREL_LO21:
			v = (int64_t)(t.va - P);
			if (!fits_int(v, 21))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, (3u << 29) | (0x7ffffu << 5),
				     (uint32_t)(((uint64_t)v & 3) << 29 | (((uint64_t)v >> 2) & 0x7ffff) << 5));
			break;
		case R_AARCH64_ADD_ABS_LO12_NC:
			write_le_imm(loc, 0xfffu << 10, (uint32_t)(t.va & 0xfff) << 10);
			break;
		case R_AARCH64_LDST8_ABS_LO12_NC:
			write_le_imm(loc, 0xfffu << 10, (uint32_t)(t.va & 0xfff) << 10);
			break;
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC: {
			int sh = type == R_AARCH64_LDST16_ABS_LO12_NC ? 1 : type == R_AARCH64_LDST32_ABS_LO12_NC ? 2 :
				 type == R_AARCH64_LDST64_ABS_LO12_NC ? 3 : 4;
			if (t.va & ((1u << sh) - 1))
				reloc_error(o, is, rl, "improper alignment", (int64_t)t.va);
			write_le_imm(loc, 0xfffu << 10, (uint32_t)((t.va & 0xfff) >> sh) << 10);
			break;
		}
		case R_AARCH64_LD_PREL_LO19:
			v = (int64_t)(t.va - P);
			if (v & 3)
				reloc_error(o, is, rl, "improper alignment", v);
			if (!fits_int(v, 21))
				reloc_error(o, is, rl, "out of range", v);
			write_le_imm(loc, 0x7ffffu << 5, (uint32_t)(((uint64_t)v & 0x1ffffc) << 3));
			break;
		default:
			break;
		}
	}
	/* entries are sorted by offset within the section */
	for (uint32_t a = 1; a < nd; a++) {
		Rela x = dyn[a];
		uint32_t b = a;
		while (b > 0 && dyn[b - 1].r_offset > x.r_offset) {
			dyn[b] = dyn[b - 1];
			b--;
		}
		dyn[b] = x;
	}
}

static VEC(uint32_t) wjobs;		/* ISecs to write */

static void write_isec(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = wjobs.v[j];
	ISec *is = &isecs.v[si];
	uint32_t oi = isec_osec(si);
	OSec *os = &osecs.v[oi];
	if (os->type == SHT_NOBITS || is->type == SHT_NOBITS)
		return;
	uint8_t *dst = out + os->offset + is->off;
	switch (is->kind) {
	case K_NORMAL:
		memcpy(dst, is->data, is->size);
		if (is->nrela && (is->flags & SHF_ALLOC || !opt.relocatable))
			relocate_isec(si, dst);
		break;
	case K_MERGE: {
		Merge *mg = &merges.v[is->aux];
		for (int s = 0; s < 32; s++)
			for (uint32_t k = 0; k < mg->uniq[s].n; k++) {
				uint64_t u = mg->uniq[s].v[k];
				MergeIn *m = &mergeins.v[u >> 32];
				uint32_t p = (uint32_t)u;
				memcpy(dst + m->outoff[p], isecs.v[m->isec].data + m->inoff[p], piece_len(m, p));
			}
		break;
	}
	case K_THUNK: {
		ThunkSec *ts = &thunksecs.v[is->aux];
		for (uint32_t k = 0; k < ts->list.n; k++) {
			uint32_t t = ts->list.v[k];
			uint8_t *b = dst + thunks.v[t].off;
			uint64_t s = thunk_dest(t), p = thunk_va(t);
			if (!thunks.v[t].lng) {
				wr32(b, 0x14000000 | (uint32_t)(((s - p) & 0x0ffffffc) >> 2));
				continue;
			}
			int64_t pg = (int64_t)((s & ~0xfffULL) - (p & ~0xfffULL));
			uint64_t imm = (uint64_t)pg >> 12;
			uint32_t adrp = 0x90000010 | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5);
			wr32(b, adrp);
			wr32(b + 4, 0x91000210 | (uint32_t)(s & 0xfff) << 10);
			wr32(b + 8, 0xd61f0200);
		}
		break;
	}
	case K_BUILDID:
		wr32(dst, 4);
		wr32(dst + 4, (uint32_t)is->size - 16);
		wr32(dst + 8, 3);
		memcpy(dst + 12, "GNU", 4);
		break;
	default:
		break;
	}
}

/* ---------------- stubs for unresolved symbols (-O0 dead branches) ---------------- */

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/*
 * The -O0 kernel references symbols that only exist in configurations
 * where -O2 would have deleted the reference.  vroom-ld retried the link
 * with an assembled stub object: each symbol becomes 64 zero bytes in
 * .data (writable so static keys work, NX so stray calls trap).  The same
 * object is synthesized here, in memory, so the result is identical to
 * that retry without running anything.
 */
static Obj *make_stub_obj(const char **names, uint32_t n)
{
	size_t strsz = 1 + 3;
	for (uint32_t i = 0; i < n; i++)
		strsz += strlen(names[i]) + 1;
	size_t datasz = 64 * (size_t)n;
	uint8_t *b = alloc(sizeof(Ehdr) + 5 * sizeof(Shdr) + (n + 2) * sizeof(Sym) + strsz + 64 + datasz);
	Ehdr *eh = (Ehdr *)b;
	Shdr *sh = (Shdr *)(b + sizeof(Ehdr));
	Sym *sy = (Sym *)(sh + 5);
	char *str = (char *)(sy + n + 2);
	char *shstr = str + strsz;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = 183;
	eh->e_shoff = sizeof(Ehdr);
	eh->e_shnum = 5;
	eh->e_shstrndx = 1;
	/* shstr: "\0.text\0.data\0.symtab\0" in a small separate buffer */
	memcpy(shstr, "\0.text\0.data\0.symtab\0.strtab\0", 30);
	sh[1] = (Shdr){.sh_name = 22, .sh_type = SHT_STRTAB, .sh_offset = (uint64_t)(shstr - (char *)b), .sh_size = 30, .sh_addralign = 1};
	sh[2] = (Shdr){.sh_name = 1, .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_EXECINSTR, .sh_addralign = 4};
	sh[3] = (Shdr){.sh_name = 7, .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_WRITE, .sh_size = datasz,
		       .sh_offset = (uint64_t)(shstr + 32 - (char *)b), .sh_addralign = 16};
	sh[4] = (Shdr){.sh_name = 13, .sh_type = SHT_SYMTAB, .sh_offset = (uint64_t)((uint8_t *)sy - b),
		       .sh_size = (n + 2) * sizeof(Sym), .sh_link = 1, .sh_info = 2, .sh_entsize = sizeof(Sym), .sh_addralign = 8};
	/* the symbol strtab shares section 1 in the real object; here it's separate memory */
	str[0] = 0;
	memcpy(str + 1, "$d", 3);
	size_t so = 4;
	sy[1] = (Sym){.st_name = 1, .st_info = STB_LOCAL << 4 | STT_NOTYPE, .st_shndx = 3};
	for (uint32_t i = 0; i < n; i++) {
		size_t l = strlen(names[i]);
		memcpy(str + so, names[i], l + 1);
		sy[i + 2] = (Sym){.st_name = (uint32_t)so, .st_info = STB_GLOBAL << 4 | STT_NOTYPE,
				  .st_shndx = 3, .st_value = 64 * (uint64_t)i};
		so += l + 1;
	}
	Obj *o = alloc(sizeof *o);
	o->path = "<vroom-stubs>";
	o->buf = b;
	o->size = (uint8_t *)shstr + 32 + datasz - b;
	o->sh = sh;
	o->nsh = 5;
	o->shstr = shstr;
	o->sym = sy;
	o->nsym = n + 2;
	o->firstg = 2;
	o->str = str;
	o->gs = alloc(n * sizeof(uint32_t));
	return o;
}

/* ---------------- main link ---------------- */

static void usage_error(const char *a)
{
	error("unknown argument: %s", a);
}

static bool parse_args(int argc, char **argv)
{
	bool whole = false;
	opt.max_page = 65536;
	opt.relax = true;
	opt.comment = "Linker: vld (vroom)";
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(name) (!strcmp(a, name))
#define VAL(name) (!strncmp(a, name "=", strlen(name) + 1) ? (a + strlen(name) + 1) : \
		   (!strcmp(a, name) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if (a[0] != '-' || !a[1]) {
			VPUSH(opt.inputs, ((InputArg){.path = a, .whole = whole}));
			continue;
		}
		if (ARG("-o") && i + 1 < argc) { opt.out = argv[++i]; continue; }
		if (!strncmp(a, "-o", 2) && a[2]) { opt.out = a + 2; continue; }
		if (ARG("-r") || ARG("--relocatable") || ARG("-i")) { opt.relocatable = true; continue; }
		if (ARG("-shared") || ARG("--shared") || ARG("-Bshareable")) { opt.shared = true; continue; }
		if (ARG("-Bsymbolic") || ARG("--Bsymbolic")) { opt.bsymbolic = true; continue; }
		if (ARG("--whole-archive") || ARG("-whole-archive")) { whole = true; continue; }
		if (ARG("--no-whole-archive") || ARG("-no-whole-archive")) { whole = false; continue; }
		if (ARG("--start-group") || ARG("--end-group") || ARG("-(") || ARG("-)")) continue;
		if (ARG("--pic-veneer")) { opt.pic_veneer = true; continue; }
		if (ARG("-EL") || ARG("--no-undefined-version") || ARG("--eh-frame-hdr")) continue;
		if (ARG("--no-undefined") || ARG("-zdefs")) { opt.no_undefined = true; continue; }
		if (ARG("-X") || ARG("--discard-locals")) { opt.discard_locals = true; continue; }
		if (ARG("-x") || ARG("--discard-all")) { opt.discard_all = true; continue; }
		if (ARG("-S") || ARG("--strip-debug")) { opt.strip_debug = true; continue; }
		if (ARG("-n") || ARG("--nmagic")) { opt.nmagic = true; continue; }
		if (ARG("--no-apply-dynamic-relocs")) { opt.no_apply_dyn = true; continue; }
		if (ARG("--apply-dynamic-relocs")) { opt.no_apply_dyn = false; continue; }
		if (ARG("--no-relax")) { opt.relax = false; continue; }
		if (ARG("--vroom-stubs")) { opt.stubs = true; continue; }
		if (ARG("--emit-relocs") || ARG("-q")) { error("--emit-relocs is not supported"); continue; }
		if (ARG("-m") && i + 1 < argc) { i++; continue; }
		if (!strncmp(a, "-m", 2) && !strncmp(a + 2, "aarch64", 7)) continue;
		if ((v = VAL("--script")) || (v = VAL("-T"))) { opt.script = v; continue; }
		if (!strncmp(a, "-T", 2) && a[2]) { opt.script = a + 2; continue; }
		if ((v = VAL("-soname")) || (v = VAL("--soname")) || (v = VAL("-h"))) { opt.soname = v; continue; }
		if ((v = VAL("--build-id"))) { opt.build_id = strcmp(v, "none") ? v : NULL; continue; }
		if (ARG("--build-id")) { opt.build_id = "fast"; continue; }
		if ((v = VAL("--orphan-handling"))) continue;
		if ((v = VAL("--error-limit"))) { error_limit = atoi(v); continue; }
		if ((v = VAL("--threads"))) { nthreads = atoi(v) > 0 ? atoi(v) : 1; continue; }
		if ((v = VAL("-Map")) || (v = VAL("--Map"))) { opt.map = v; continue; }
		if ((v = VAL("--vroom-comment"))) { opt.comment = v; continue; }
		if ((v = VAL("-z"))) {
			if (!strcmp(v, "notext")) opt.notext = true;
			else if (!strcmp(v, "max-page-size") || !strncmp(v, "max-page-size=", 14))
				opt.max_page = strtoull(v + 14, NULL, 0);
			else if (strcmp(v, "norelro") && strcmp(v, "noexecstack") && strcmp(v, "relro") &&
				 strcmp(v, "text") && strcmp(v, "now") && strcmp(v, "lazy") && strcmp(v, "nodefaultlib"))
				warn("ignoring -z %s", v);
			continue;
		}
		if (!strncmp(a, "-z", 2) && a[2]) continue;
		usage_error(a);
	}
	if (!opt.out)
		opt.out = "a.out";
	return true;
}

static void load_inputs(void)
{
	for (uint32_t i = 0; i < opt.inputs.n; i++) {
		const char *p = opt.inputs.v[i].path;
		size_t n;
		uint8_t *b = map_file(p, &n);
		if (!b)
			continue;
		if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8))) {
			if (!opt.inputs.v[i].whole && !opt.relocatable) {
				/* lazy archives are not needed by the kernel link */
				bool empty = true;
				uint32_t before = pending.n;
				add_archive(p, b, n);
				empty = pending.n == before;
				pending.n = before;
				if (!empty)
					error("%s: archives without --whole-archive are not supported", p);
				continue;
			}
			add_archive(p, b, n);
			continue;
		}
		VPUSH(pending, ((Pending){.path = p, .buf = b, .size = n}));
	}
	for (uint32_t i = 0; i < pending.n; i++) {
		Obj *o = alloc(sizeof *o);
		o->path = pending.v[i].path;
		o->buf = pending.v[i].buf;
		o->size = pending.v[i].size;
		o->idx = objs.n;
		VPUSH(objs, o);
	}
	pfor(objs.n, parse_obj, NULL);
}

static void alloc_isecs(uint32_t from)
{
	uint32_t total = isecs.n;
	for (uint32_t i = from; i < objs.n; i++) {
		objs.v[i]->isec0 = total;
		total += objs.v[i]->nsh;
	}
	if (total > isecs.cap) {
		uint32_t nc = isecs.cap ? isecs.cap : 1024;
		while (nc < total + 1024)
			nc *= 2;
		ISec *nv = alloc((size_t)nc * sizeof(ISec));
		if (isecs.n)
			memcpy(nv, isecs.v, isecs.n * sizeof(ISec));
		isecs.v = nv;
		isecs.cap = nc;
	}
	memset(&isecs.v[isecs.n], 0, (size_t)(total - isecs.n) * sizeof(ISec));
	isecs.n = total;
}

static void gtable_init(void)
{
	uint64_t total = 1024;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->nsym > objs.v[i]->firstg)
			total += objs.v[i]->nsym - objs.v[i]->firstg;
	uint32_t cap = 4096;
	while (cap < total * 2 + 4096)
		cap *= 2;
	hmask = cap - 1;
	hnames = bigalloc((size_t)cap * sizeof(*hnames));
	hhash = bigalloc((size_t)cap * sizeof(*hhash));
	gsyms = bigalloc((size_t)cap * sizeof(G));
}

static void gtable_free(void)
{
	munmap((void *)hnames, (size_t)(hmask + 1) * sizeof(*hnames));
	munmap(hhash, (size_t)(hmask + 1) * sizeof(*hhash));
	munmap(gsyms, (size_t)(hmask + 1) * sizeof(G));
	hnames = NULL;
}

static int write_output(void)
{
	/* replace, don't truncate in place: the old file may be mmap'ed by a reader */
	unlinkat(dirfd_base, opt.out, 0);
	int fd = openat(dirfd_base, opt.out, O_WRONLY | O_CREAT | O_TRUNC, opt.relocatable ? 0666 : 0777);
	if (fd < 0) {
		error("cannot open output file %s: %s", opt.out, strerror(errno));
		return 1;
	}
	uint64_t off = 0;
	while (off < filesize) {
		ssize_t w = write(fd, out + off, filesize - off > (1 << 30) ? (1 << 30) : filesize - off);
		if (w <= 0) {
			error("write %s: %s", opt.out, strerror(errno));
			close(fd);
			return 1;
		}
		off += w;
	}
	close(fd);
	return 0;
}

static void hash_chunk(void *arg, size_t i)
{
	uint8_t *hashes = arg;
	uint64_t b = (uint64_t)i << 20, n = filesize - b < (1u << 20) ? filesize - b : (1u << 20);
	ld_blake3(out + b, n, hashes + i * 20, 20);
}


/* ================= script symbols and address assignment ================= */

/* op= of a symbol assignment (lld readSymbolAssignment) */
static EV apply_op(char op, EV lhs, EV v)
{
	uint64_t x, y;
	switch (op) {
	case '=': return v;
	case '+': return ev_add(lhs, v);
	case '-': return ev_sub(lhs, v);
	}
	x = ev_value(lhs);
	y = ev_value(v);
	switch (op) {
	case '*': return evabs(x * y);
	case '/':
		if (!y)
			error("division by zero");
		return evabs(y ? x / y : 0);
	case '<': return evabs(x << (y % 64));
	case '>': return evabs(x >> (y % 64));
	case '&': return evabs(x & y);
	case '^': return evabs(x ^ y);
	case '|': return evabs(x | y);
	}
	return v;
}

static VEC(const char *) recorded;	/* errors of the current address pass */

/* assignSymbol(); returns true if the symbol's (section, value) changed */
static bool assign_symbol(Assign *a, bool insec)
{
	if (a->assert_) {
		eval(a->e);
		return false;
	}
	if (a->sym == NONE) {
		EV v = apply_op(a->op, dot_ev(), eval(a->e));
		uint64_t val = ev_value(v);
		if (insec && val < dot) {
			char *m = alloc(160);
			snprintf(m, 160, "unable to move location counter (0x%llx) backward to 0x%llx for section '%s'",
				 (unsigned long long)dot, (unsigned long long)val, osecs.v[cur_osec].name);
			VPUSH(recorded, m);
		}
		dot = val;
		return false;
	}
	if (!a->defined)
		return false;
	G *s = &gsyms[a->sym];
	EV v = apply_op(a->op, gsym_ev(a->sym), eval(a->e));
	EV old = s->ev;
	if (ev_isabs(v))
		s->ev = (EV){.kind = EV_ABS, .val = ev_value(v), .align = 1};
	else
		s->ev = (EV){.kind = v.kind, .idx = v.idx, .val = ev_secoff(v), .align = 1};
	s->type = v.type;
	return old.kind != s->ev.kind || old.idx != s->ev.idx || old.val != s->ev.val;
}

/* assignOffsets(); returns true if the section address changed */
static bool assign_offsets(uint32_t oi, bool *symchg)
{
	OSec *os = &osecs.v[oi];
	uint64_t saved = dot;
	if (!(os->flags & SHF_ALLOC))
		dot = 0;
	else if (os->addr)
		dot = ev_value(eval(os->addr));
	cur_osec = oi;
	if (!os->addr)
		dot = alignup(dot, os->addralign);
	bool changed = os->va != dot;
	os->va = dot;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		Cmd *cm = &os->cmds.v[c];
		if (cm->kind == C_ASSIGN) {
			*symchg |= assign_symbol(cm->as, true);
			continue;
		}
		ISD *d = &isds.v[cm->idx];
		for (uint32_t k = 0; k < d->secs.n; k++) {
			ISec *is = &isecs.v[d->secs.v[k]];
			dot = alignup(dot, is->align);
			is->off = dot - os->va;
			dot += is->size;
		}
	}
	os->size = dot - os->va;
	if (!(os->flags & SHF_ALLOC))
		dot = saved;
	return changed;
}

static bool assign_addresses(void)
{
	bool changed = false, symchg = false;
	dot = 0;
	cur_osec = AETHER;
	recorded.n = 0;
	assert_fail.n = 0;
	for (uint32_t i = 0; i < topcmds.n; i++) {
		Cmd *c = &topcmds.v[i];
		if (c->kind == C_ASSIGN) {
			symchg |= assign_symbol(c->as, false);
			continue;
		}
		if (!osecs.v[c->idx].live)
			continue;
		changed |= assign_offsets(c->idx, &symchg);
	}
	return changed || symchg;
}

/* processSymbolAssignments(): first definition of script symbols, dot = 0
   in the aether.  A section-relative value starts at the section's start. */
static void add_script_symbol(Assign *a)
{
	if (a->sym == NONE || !a->defined)
		return;
	G *s = &gsyms[a->sym];
	EV v = apply_op(a->op, gsym_ev(a->sym), eval(a->e));
	s->state = GS_SCRIPT;
	s->obj = NONE;
	s->bind = STB_GLOBAL;
	s->size = 0;
	s->type = v.type;
	if (a->hidden && visrank(s->vis) > visrank(STV_HIDDEN))
		s->vis = STV_HIDDEN;
	if (ev_isabs(v))
		s->ev = (EV){.kind = EV_ABS, .val = v.kind != EV_ABS ? 0 : ev_value(v), .align = 1};
	else
		s->ev = (EV){.kind = v.kind, .idx = v.idx, .val = 0, .align = 1};
}

static void process_symbol_assignments(void)
{
	dot = 0;
	cur_osec = AETHER;
	for (uint32_t i = 0; i < topcmds.n; i++) {
		Cmd *c = &topcmds.v[i];
		if (c->kind == C_ASSIGN) {
			add_script_symbol(c->as);
			continue;
		}
		OSec *os = &osecs.v[c->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ASSIGN)
				add_script_symbol(os->cmds.v[k].as);
	}
}

/* ---------------- adjustOutputSections ---------------- */

static bool osec_discardable(OSec *os)
{
	if (os->discard)
		return true;
	if (os->uses_syms || os->used_in_expr)
		return false;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		Cmd *cm = &os->cmds.v[c];
		if (cm->kind == C_ASSIGN) {
			if (cm->as->sym != NONE && !cm->as->defined && !cm->as->assert_)
				continue;
			return false;
		}
	}
	return true;
}

static void adjust_output_sections(void)
{
	uint64_t flags = SHF_ALLOC;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		if (os->alignx) {
			uint64_t a = ev_value(eval(os->alignx));
			if (a > os->addralign)
				os->addralign = a;
		}
		bool empty = true;
		for (uint32_t k = 0; k < os->cmds.n && empty; k++)
			if (os->cmds.v[k].kind == C_ISD && isds.v[os->cmds.v[k].idx].secs.n)
				empty = false;
		bool discardable = empty && osec_discardable(os);
		if (os->has_input && !discardable)
			flags = os->flags;
		if (empty)
			os->flags = flags & ((os->nonalloc ? 0 : SHF_ALLOC) | SHF_WRITE);
		os->live = !discardable;
	}
}

/* ---------------- phdrs (lld createPhdrs) ---------------- */

static bool headers_loaded;

static void create_phdrs(void)
{
	phdrs.n = phsecs.n = 0;
	phnew(PT_PHDR, PF_R);
	int hdrload = phnew(PT_LOAD, PF_R);
	int load = hdrload;
	uint32_t flags = PF_R;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		uint32_t oi = outsecs.v[i];
		OSec *os = &osecs.v[oi];
		os->ptload = -1;
		if (!sec_in_load(os))
			continue;
		uint32_t nf = phflags(os);
		if (flags ^ nf)
			load = -1;
		bool last_is_hdr = load == hdrload && phsecs.v[load].last < 0;
		if (load >= 0 && (!os->lma || last_is_hdr)) {
			phdrs.v[load].p_flags |= nf;
		} else {
			load = phnew(PT_LOAD, nf);
			flags = nf;
		}
		phadd(load, oi);
	}
	phnew(PT_GNU_STACK, PF_R | PF_W);
	int note = -1;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		uint32_t oi = outsecs.v[i];
		OSec *os = &osecs.v[oi];
		if (os->type == SHT_NOTE && (os->flags & SHF_ALLOC)) {
			if (note < 0 || os->lma || osecs.v[phsecs.v[note].last].addralign != os->addralign)
				note = phnew(PT_NOTE, PF_R);
			phadd(note, oi);
		} else {
			note = -1;
		}
	}
}

/* allocateHeaders() + removeEmptyPTLoad() */
static void finish_phdrs(void)
{
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		if ((os->flags & SHF_ALLOC) && os->va < min)
			min = os->va;
	}
	uint64_t hsize = sizeof(Ehdr) + (uint64_t)phdrs.n * sizeof(Phdr);
	uint64_t base = min & ~(opt.max_page - 1);
	headers_loaded = !opt.nmagic && hsize <= min - base;
	if (headers_loaded)
		error("ELF headers in the first PT_LOAD are not supported (no SECTIONS gap below the first section)");
	int *map = alloc(phdrs.n * sizeof(int));
	uint32_t w = 0;
	for (uint32_t i = 0; i < phdrs.n; i++) {
		Phdr *p = &phdrs.v[i];
		PhdrSecs *s = &phsecs.v[i];
		bool keep = true;
		if (p->p_type == PT_PHDR)
			keep = headers_loaded;
		else if (p->p_type == PT_LOAD) {
			if (s->first < 0)
				keep = false;
			else {
				OSec *f = &osecs.v[s->first], *l = &osecs.v[s->last];
				keep = l->va + l->size - f->va != 0;
			}
		}
		map[i] = keep ? (int)w : -1;
		if (keep) {
			phdrs.v[w] = *p;
			phsecs.v[w] = *s;
			w++;
		}
	}
	phdrs.n = phsecs.n = w;
	for (uint32_t k = 0; k < outsecs.n; k++) {
		OSec *os = &osecs.v[outsecs.v[k]];
		if (os->ptload >= 0)
			os->ptload = map[os->ptload];
	}
}

/* ---------------- symbol table ---------------- */

typedef struct { uint32_t g; uint32_t idx; uint64_t str; bool local; } GOut;
static GOut *gouts;
static uint32_t ngouts;
static uint8_t *symtab_buf, *strtab_buf;
static uint64_t nsymout, strsize;
static uint32_t nlocal_out;

static void put_global(Sym *d, uint64_t stroff, uint32_t g, bool local)
{
	G *s = &gsyms[g];
	uint8_t bind = local ? STB_LOCAL : s->bind;
	uint16_t shndx;
	uint64_t val, size = s->size;
	if (s->state == GS_SCRIPT) {
		shndx = s->ev.kind == EV_ABS ? SHN_ABS : s->ev.kind == EV_OSEC ? osec_shidx(s->ev.idx)
										: sym_shndx_isec(s->ev.idx);
		val = gsym_va(g);
		size = 0;
	} else if (s->state == GS_OBJ && !s->demoted) {
		shndx = s->sec == NONE ? SHN_ABS : sym_shndx_isec(s->sec);
		val = gsym_va(g);
	} else {
		shndx = SHN_UNDEF;
		val = 0;
		size = 0;
	}
	uint64_t so = stroff;
	put_sym(d, strtab_buf, &so, gname(g), (uint8_t)(bind << 4 | s->type), s->vis, shndx, val, size);
}

static void write_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf || !o->nloc)
		return;
	Sym *d = (Sym *)symtab_buf + o->loc0;
	uint64_t so = o->str0;
	for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
		const Sym *y = &o->sym[s];
		if (!keep_local(o, y))
			continue;
		uint16_t shndx;
		uint64_t val;
		if (y->st_shndx == SHN_ABS) {
			shndx = SHN_ABS;
			val = y->st_value;
		} else {
			uint32_t sec = o->isec0 + y->st_shndx;
			shndx = sym_shndx_isec(sec);
			val = isec_va(sec, y->st_value);
		}
		put_sym(d++, strtab_buf, &so, o->str + y->st_name, y->st_info, y->st_other, shndx, val,
			y->st_size);
	}
}

static void write_globals(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < ngouts ? b + 4096 : ngouts;
	for (uint32_t i = b; i < e; i++)
		put_global((Sym *)symtab_buf + gouts[i].idx, gouts[i].str, gouts[i].g, gouts[i].local);
}

static bool g_in_symtab(uint32_t g)
{
	G *s = &gsyms[g];
	/* isUsedInRegularObj: referenced by an object or the script */
	return s->state != GS_UNDEF || kget(&s->first) != UINT64_MAX;
}
static bool g_local(uint32_t g)
{
	G *s = &gsyms[g];
	return s->vis == STV_HIDDEN || s->vis == STV_INTERNAL;
}

/*
 * lld's order: file locals in file order, each file's group followed by its
 * hidden (now local) globals; then groups of hidden globals whose file had
 * no locals, in order of appearance (every script assignment has its own
 * internal file); then thunk symbols; then the remaining globals in symbol
 * table insertion order.  Strings are appended in the order symbols were
 * added: file locals, all globals, thunk symbols.
 */
static void build_symtab(void)
{
	pfor(objs.n, count_locals, NULL);
	/* per-file hidden counts */
	uint32_t *hcount = alloc((objs.n + 1) * sizeof(uint32_t));
	uint32_t nglob = 0, nhid = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g))
			continue;
		nglob++;
		if (g_local(g)) {
			nhid++;
			G *s = &gsyms[g];
			if (s->state == GS_OBJ)
				hcount[s->obj]++;
		}
	}
	/* local indices */
	uint64_t idx = 1, str = 1;
	uint32_t *hnext = alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		o->loc0 = idx;
		o->str0 = str;
		str += o->locstr;
		idx += o->nloc;
		hnext[i] = idx;
		if (o->nloc)
			idx += hcount[i];
	}
	/* hidden-only groups after all file groups */
	uint32_t *grp_next = alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++)
		grp_next[i] = NONE;
	gouts = alloc((size_t)nglob * sizeof(GOut));
	ngouts = 0;
	uint64_t gstr = str;
	/* first pass: place hidden symbols */
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g) || !g_local(g))
			continue;
		G *s = &gsyms[g];
		uint32_t at;
		if (s->state == GS_OBJ && objs.v[s->obj]->nloc) {
			at = hnext[s->obj]++;
		} else if (s->state == GS_OBJ) {
			if (grp_next[s->obj] == NONE) {
				grp_next[s->obj] = idx;
				idx += hcount[s->obj];
			}
			at = grp_next[s->obj]++;
		} else {
			at = idx++;
		}
		gouts[ngouts++] = (GOut){.g = g, .idx = at, .local = true};
	}
	/* thunk symbols */
	uint64_t thunk0 = idx;
	idx += 2 * (uint64_t)thunks.n;
	nlocal_out = idx;
	/* globals, and string offsets in insertion order */
	uint32_t h = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g))
			continue;
		uint64_t so = gstr;
		gstr += strlen(gname(g)) + 1;
		if (g_local(g)) {
			gouts[h++].str = so;	/* same order as the first pass */
			continue;
		}
		gouts[ngouts++] = (GOut){.g = g, .idx = idx++, .str = so};
	}
	nsymout = idx;
	uint64_t tstr = gstr;
	for (uint32_t t = 0; t < thunks.n; t++)
		tstr += strlen(thunks.v[t].name) + 1 + 3;
	strsize = tstr;
	ISec *st = &isecs.v[symtab_isec], *ss = &isecs.v[strtab_isec];
	st->size = nsymout * sizeof(Sym);
	ss->size = strsize;
	/* thunk symbols are written now: their values are final */
	symtab_buf = alloc(st->size);
	strtab_buf = alloc(ss->size);
	uint64_t so = gstr;
	for (uint32_t t = 0; t < thunks.n; t++) {
		Thunk *th = &thunks.v[t];
		Sym *d = (Sym *)symtab_buf + thunk0 + 2 * t;
		uint16_t shndx = sym_shndx_isec(th->tsec);
		put_sym(d, strtab_buf, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, thunk_va(t), th->size);
		put_sym(d + 1, strtab_buf, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, thunk_va(t), 0);
	}
	(void)nhid;
}

static void fill_symtab(void)
{
	pfor(objs.n, write_locals, NULL);
	pfor((ngouts + 4095) / 4096, write_globals, NULL);
}

/* ---------------- output ---------------- */

static uint8_t *shstrtab_buf;

static void build_shstrtab(void)
{
	/* deduplicated, in section header order (StringTableBuilder in insertion order) */
	size_t cap = 1;
	for (uint32_t i = 0; i < outsecs.n; i++)
		cap += strlen(osecs.v[outsecs.v[i]].name) + 1;
	shstrtab_buf = alloc(cap);
	size_t n = 1;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		size_t l = strlen(os->name);
		uint32_t found = NONE;
		for (uint32_t k = 0; k < i; k++)
			if (!strcmp(osecs.v[outsecs.v[k]].name, os->name)) {
				found = osecs.v[outsecs.v[k]].nameoff;
				break;
			}
		if (found != NONE) {
			os->nameoff = found;
			continue;
		}
		memcpy(shstrtab_buf + n, os->name, l + 1);
		os->nameoff = n;
		n += l + 1;
	}
	isecs.v[shstrtab_isec].size = n;
}

static void write_headers(void)
{
	Ehdr *eh = (Ehdr *)out;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = opt.relocatable ? 1 : opt.shared ? 3 : 2;
	eh->e_machine = 183;
	eh->e_version = 1;
	uint32_t eg = entry_name ? gfind(entry_name) : NONE;
	if (!opt.relocatable)
		eh->e_entry = eg != NONE && gsyms[eg].state != GS_UNDEF ? gsym_va(eg) : 0;
	eh->e_phoff = phdrs.n ? sizeof(Ehdr) : 0;
	eh->e_shoff = shoff;
	eh->e_ehsize = sizeof(Ehdr);
	eh->e_phentsize = phdrs.n ? sizeof(Phdr) : 0;
	eh->e_phnum = phdrs.n;
	eh->e_shentsize = sizeof(Shdr);
	eh->e_shnum = outsecs.n + 1;
	eh->e_shstrndx = osecs.v[isec_osec(shstrtab_isec)].shidx;
	if (phdrs.n)
		memcpy(out + sizeof(Ehdr), phdrs.v, phdrs.n * sizeof(Phdr));
	Shdr *sh = (Shdr *)(out + shoff);
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		Shdr *d = &sh[i + 1];
		d->sh_name = os->nameoff;
		d->sh_type = os->type;
		d->sh_flags = os->flags;
		d->sh_addr = os->va;
		d->sh_offset = os->offset;
		d->sh_size = os->size;
		d->sh_link = os->link;
		d->sh_info = os->info;
		d->sh_addralign = os->addralign;
		d->sh_entsize = os->entsize;
	}
}

static void write_synthetic(void)
{
	ISec *is;
	if (reladyn_isec != NONE && (is = &isecs.v[reladyn_isec])->size)
		memcpy(out + osecs.v[isec_osec(reladyn_isec)].offset + is->off, reladyn_buf, is->size);
	is = &isecs.v[symtab_isec];
	memcpy(out + osecs.v[isec_osec(symtab_isec)].offset + is->off, symtab_buf, is->size);
	is = &isecs.v[strtab_isec];
	memcpy(out + osecs.v[isec_osec(strtab_isec)].offset + is->off, strtab_buf, is->size);
	is = &isecs.v[shstrtab_isec];
	memcpy(out + osecs.v[isec_osec(shstrtab_isec)].offset + is->off, shstrtab_buf, is->size);
}

/* --build-id: lld's "sha1"/"tree" ids are a BLAKE3 tree over 1 MiB chunks */
static void write_build_id(void)
{
	if (buildid_isec == NONE)
		return;
	ISec *is = &isecs.v[buildid_isec];
	uint8_t *dst = out + osecs.v[isec_osec(buildid_isec)].offset + is->off + 16;
	size_t hl = is->size - 16;
	size_t nchunks = (filesize + (1u << 20) - 1) >> 20;
	uint8_t *hashes = alloc(nchunks * 20 + 32);
	if (!strcmp(opt.build_id, "sha1") || !strcmp(opt.build_id, "tree")) {
		pfor(nchunks, hash_chunk, hashes);
		ld_blake3(hashes, nchunks * 20, dst, hl);
	} else if (!strcmp(opt.build_id, "fast")) {
		/* xxh3 of the chunk hashes (lld: --build-id=fast is xxh3_64bits per chunk) */
		uint8_t *h8 = alloc(nchunks * 8);
		for (size_t i = 0; i < nchunks; i++) {
			uint64_t b = (uint64_t)i << 20, n = filesize - b < (1u << 20) ? filesize - b : (1u << 20);
			uint64_t x = ld_xxh3(out + b, n);
			memcpy(h8 + i * 8, &x, 8);
		}
		uint64_t x = ld_xxh3(h8, nchunks * 8);
		memcpy(dst, &x, 8);
	} else {
		error("--build-id=%s is not supported", opt.build_id);
	}
}

/* ---------------- the final link ---------------- */

static void reset_state(void)
{
	memset(&opt, 0, sizeof opt);
	memset(&objs, 0, sizeof objs);
	memset(&isecs, 0, sizeof isecs);
	memset(&mergeins, 0, sizeof mergeins);
	memset(&merges, 0, sizeof merges);
	memset(&gorder, 0, sizeof gorder);
	memset(&isds, 0, sizeof isds);
	memset(&osecs, 0, sizeof osecs);
	memset(&topcmds, 0, sizeof topcmds);
	memset(&refsyms, 0, sizeof refsyms);
	memset(&provides, 0, sizeof provides);
	memset(&pending, 0, sizeof pending);
	memset(&orphans, 0, sizeof orphans);
	memset(&thunks, 0, sizeof thunks);
	memset(&thunksecs, 0, sizeof thunksecs);
	memset(&phdrs, 0, sizeof phdrs);
	memset(&phsecs, 0, sizeof phsecs);
	memset(&outsecs, 0, sizeof outsecs);
	memset(&wjobs, 0, sizeof wjobs);
	memset(&assert_fail, 0, sizeof assert_fail);
	memset(&recorded, 0, sizeof recorded);
	tr_keys = NULL;
	tr_vals = NULL;
	tr_mask = tr_n = 0;
	entry_name = NULL;
	reladyn_isec = buildid_isec = comment_isec = NONE;
	symtab_isec = strtab_isec = shstrtab_isec = NONE;
	reladyn_buf = NULL;
	out = NULL;
	ndyn = 0;
	cur_provide = NULL;
	gouts = NULL;
	ngouts = 0;
	atomic_store(&nerrors, 0);
	error_limit = 20;
	timing = getenv("VLD_TIME") != NULL;
}

static void add_synthetic_sections(void)
{
	/* lld appends synthetic input sections after all files, in this order */
	comment_isec = new_isec(".comment", K_MERGE_IN, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1);
	{
		ISec *c = &isecs.v[comment_isec];
		size_t n = strlen(opt.comment) + 1;
		uint8_t *d = alloc(n);
		memcpy(d, opt.comment, n);
		c->data = d;
		c->size = n;
		c->entsize = 1;
	}
	if (pic)
	{
		reladyn_isec = new_isec(".rela.dyn", K_RELADYN, SHT_RELA, SHF_ALLOC, 8);
		isecs.v[reladyn_isec].entsize = sizeof(Rela);
	}
	if (opt.build_id) {
		buildid_isec = new_isec(".note.gnu.build-id", K_BUILDID, SHT_NOTE, SHF_ALLOC, 4);
		isecs.v[buildid_isec].size = 16 + (!strcmp(opt.build_id, "md5") ? 16 :
						   !strcmp(opt.build_id, "fast") ? 8 : 20);
	}
	symtab_isec = new_isec(".symtab", K_SYMTAB, SHT_SYMTAB, 0, 8);
	isecs.v[symtab_isec].entsize = sizeof(Sym);
	shstrtab_isec = new_isec(".shstrtab", K_SHSTRTAB, SHT_STRTAB, 0, 1);
	strtab_isec = new_isec(".strtab", K_STRTAB, SHT_STRTAB, 0, 1);
}

static void for_assigns(void (*fn)(Assign *))
{
	for (uint32_t c = 0; c < topcmds.n; c++) {
		Cmd *cm = &topcmds.v[c];
		if (cm->kind == C_ASSIGN) {
			fn(cm->as);
			continue;
		}
		OSec *os = &osecs.v[cm->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ASSIGN)
				fn(os->cmds.v[k].as);
	}
}

static bool g_referenced(uint32_t g) { return kget(&gsyms[g].first) != UINT64_MAX; }
static bool g_defined_obj(uint32_t g) { return kget(&gsyms[g].def) != UINT64_MAX; }

static uint64_t script_ord;
static VEC(uint32_t) script_new;	/* slots first inserted by the script, in order */

static void script_insert(uint32_t g)
{
	if (!g_referenced(g))
		VPUSH(script_new, g);
	atomic_min64(&gsyms[g].first, script_ord++);
}

static void decide_define(Assign *a)
{
	if (a->sym == NONE)
		return;
	a->defined = !a->provide || (g_referenced(a->sym) && !g_defined_obj(a->sym));
}
static void declare(Assign *a)
{
	if (a->sym == NONE || !a->defined)
		return;
	script_insert(a->sym);
	G *s = &gsyms[a->sym];
	s->state = GS_SCRIPT;
	s->scriptdef = 1;
	s->ev = (EV){.kind = EV_ABS, .align = 1};
}

/* addScriptReferencedSymbolsToSymTable() + declareSymbols() */
static void script_symbols(void)
{
	script_ord = (uint64_t)(objs.n + 1) << 32;
	for (uint32_t i = 0; i < refsyms.n; i++)
		script_insert(refsyms.v[i]);
	uint8_t *added = alloc(provides.n + 1);
	VEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < provides.n; i++) {
		uint32_t g = provides.v[i].sym;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = provides.v[k].sym == g;
		if (!dup && g_referenced(g) && !g_defined_obj(g)) {
			added[i] = 1;
			VPUSH(stack, i);
		}
	}
	while (stack.n) {
		Provide *p = &provides.v[stack.v[--stack.n]];
		for (uint32_t k = 0; k < p->refs.n; k++) {
			uint32_t g = p->refs.v[k];
			script_insert(g);
			for (uint32_t j = 0; j < provides.n; j++) {
				if (provides.v[j].sym != g)
					continue;
				bool seen = false;
				for (uint32_t q = 0; q < provides.n && !seen; q++)
					seen = provides.v[q].sym == g && added[q];
				if (!seen && !g_defined_obj(g)) {
					added[j] = 1;
					VPUSH(stack, j);
				}
				break;
			}
		}
	}
	for_assigns(decide_define);
	for_assigns(declare);
}

/* names of undefined symbols that the -O0 stub object defines */
static const char **stub_names;
static uint32_t nstubs;
static void mark_target(Assign *a)
{
	if (a->sym != NONE)
		gsyms[a->sym].hidden_script = 1;	/* reused as "assigned by the script" */
}

static void collect_stubs(void)
{
	for_assigns(mark_target);
	VEC(const char *) names = {0};
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			G *gs = &gsyms[g];
			if (o->sym[s].st_shndx != SHN_UNDEF || g_defined_obj(g) || gs->hidden_script || gs->dup)
				continue;
			if ((o->sym[s].st_info >> 4) == STB_WEAK)
				continue;
			gs->dup = 1;
			VPUSH(names, gname(g));
		}
	}
	for_assigns(mark_target);	/* idempotent */
	qsort(names.v, names.n, sizeof(char *), cmp_str);
	stub_names = names.v;
	nstubs = names.n;
	for (uint32_t i = 0; i < nstubs; i++)
		gsyms[gfind(stub_names[i])].dup = 0;
}

static void write_stub_list(void)
{
	char *p = alloc(strlen(opt.out) + 16);
	sprintf(p, "%s.vroom-stubs", opt.out);
	FILE *f = NULL;
	int fd = openat(dirfd_base, p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
		f = fdopen(fd, "w");
	if (!f) {
		error("cannot write %s", p);
		return;
	}
	for (uint32_t i = 0; i < nstubs; i++)
		fprintf(f, "%s\n", stub_names[i]);
	fclose(f);
}

/* global order: first occurrence in file order, then script insertions */
static void build_gorder(void)
{
	gorder.n = 0;
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			if (kget(&gsyms[g].first) == symkey(o->idx, s))
				VPUSH(gorder, g);
		}
	}
	for (uint32_t i = 0; i < script_new.n; i++)
		VPUSH(gorder, script_new.v[i]);
}

static void report_undefined(void)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		if (s->state != GS_UNDEF || s->bind == STB_WEAK)
			continue;
		if ((kget(&s->first) >> 32) >= objs.n)	/* referenced only by the script */
			continue;
		if (opt.no_undefined || !opt.shared) {
			Obj *o = objs.v[kget(&s->first) >> 32];
			error("undefined symbol: %s\n>>> referenced by %s", gname(gorder.v[i]), o->path);
			n++;
		}
	}
	(void)n;
}

static int cmp_dyn(const void *a, const void *b)
{
	uint64_t x = isec_va(*(const uint32_t *)a, 0), y = isec_va(*(const uint32_t *)b, 0);
	return (x > y) - (x < y);
}

static void demote_symbols(void *arg, size_t i)
{
	(void)arg;
	G *s = &gsyms[gorder.v[i]];
	if (s->state == GS_OBJ && s->sec != NONE) {
		uint32_t os = isec_osec(s->sec);
		if (isecs.v[s->sec].kind == K_NONE || os == NONE || os == OS_DISCARD)
			s->demoted = 1;
	}
}

static void finish_syms(void *arg, size_t i) { finish_sym(arg, i); }

static int ld_relocatable(void);

static int run(int argc, char **argv)
{
	reset_state();
	{
		int n = 0;
		size_t len = sizeof n;
		if (!sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, NULL, 0) && n > 0)
			nthreads = n;
		else
			nthreads = 4;
		if (nthreads > 64)
			nthreads = 64;
	}
	parse_args(argc, argv);
	if (atomic_load(&nerrors))
		return 1;
	double t0 = now();
	t_last = t0;
	pic = opt.shared;

	load_inputs();
	phase("map+parse inputs");
	if (atomic_load(&nerrors))
		return 1;
	gtable_init();
	if (opt.script) {
		size_t n;
		uint8_t *b = map_file(opt.script, &n);
		if (!b)
			return 1;
		parse_script((const char *)b, n);
		if (atomic_load(&nerrors))
			return 1;
	}
	alloc_isecs(0);
	pfor(objs.n, fill_isecs, NULL);
	pfor(objs.n, resolve_obj, NULL);
	pfor(objs.n, check_dups, NULL);
	phase("resolve symbols");
	if (atomic_load(&nerrors))
		return 1;
	if (opt.relocatable)
		return ld_relocatable();

	if (opt.stubs) {
		collect_stubs();
		if (nstubs) {
			Obj *o = make_stub_obj(stub_names, nstubs);
			o->idx = objs.n;
			VPUSH(objs, o);
			alloc_isecs(o->idx);
			fill_isecs(NULL, o->idx);
			resolve_obj(NULL, o->idx);
			write_stub_list();
		}
	}
	nobj_real = objs.n;
	script_symbols();
	build_gorder();
	pfor(gorder.n, finish_syms, NULL);
	phase("script symbols, order");

	add_synthetic_sections();
	assign_sections();
	/* commit sections into their output sections, in order */
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD) {
				ISD *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					commit_section(os, &isecs.v[d->secs.v[s]]);
			}
	}
	pfor(gorder.n, demote_symbols, NULL);
	report_undefined();
	phase("assign sections");
	finalize_merges();
	phase("merge sections");
	if (atomic_load(&nerrors))
		return 1;

	process_symbol_assignments();
	adjust_output_sections();
	pfor(isecs.n, scan_relocs, NULL);
	ndyn = 0;
	for (uint32_t i = 0; i < isecs.n; i++)
		ndyn += isecs.v[i].ndyn;
	if (reladyn_isec != NONE)
		isecs.v[reladyn_isec].size = ndyn * sizeof(Rela);
	phase("scan relocations");
	if (atomic_load(&nerrors))
		return 1;

	/* room for thunk sections without moving isecs */
	if (isecs.cap < isecs.n + 4096) {
		ISec *nv = alloc((size_t)(isecs.n + 4096) * sizeof(ISec));
		memcpy(nv, isecs.v, isecs.n * sizeof(ISec));
		isecs.v = nv;
		isecs.cap = isecs.n + 4096;
	}
	/* output sections in header order (live ones, script order) */
	outsecs.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++)
		if (topcmds.v[c].kind == C_OSEC && osecs.v[topcmds.v[c].idx].live) {
			VPUSH(outsecs, topcmds.v[c].idx);
			osecs.v[topcmds.v[c].idx].shidx = outsecs.n;
		}

	assign_addresses();
	int pass = 0;
	for (;;) {
		bool changed = create_thunks(pass);
		pass++;
		if (changed && pass >= 30) {
			error("address assignment did not converge");
			break;
		}
		bool chg2 = assign_addresses();
		if (!changed && !chg2)
			break;
	}
	in_layout = true;
	assign_addresses();
	in_layout = false;
	for (uint32_t i = 0; i < recorded.n; i++)
		error("%s", recorded.v[i]);
	for (uint32_t i = 0; i < assert_fail.n; i++)
		error("%s", assert_fail.v[i]);
	phase("layout");
	if (atomic_load(&nerrors))
		return 1;

	/* drop empty thunk sections (none are kept by construction) */
	create_phdrs();
	finish_phdrs();
	/* symtab and shstrtab sizes are needed before file offsets */
	build_shstrtab();
	build_symtab();
	{
		OSec *os = &osecs.v[isec_osec(symtab_isec)];
		os->link = osecs.v[isec_osec(strtab_isec)].shidx;
		os->info = nlocal_out;
		os->entsize = sizeof(Sym);
	}
	/* sizes of the non-alloc synthetic sections changed: re-run layout of them */
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		bool dummy = false;
		uint64_t d = dot;
		dot = 0;
		assign_offsets(outsecs.v[i], &dummy);
		dot = d;
	}
	assign_file_offsets();
	phase("phdrs, symtab sizes, offsets");

	out = bigalloc(filesize);
	/* RELATIVE relocations: per section, in address order */
	if (reladyn_isec != NONE && ndyn) {
		VEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < isecs.n; i++)
			if (isecs.v[i].ndyn)
				VPUSH(dl, i);
		qsort(dl.v, dl.n, sizeof(uint32_t), cmp_dyn);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			isecs.v[dl.v[i]].dyn0 = at;
			at += isecs.v[dl.v[i]].ndyn;
		}
		reladyn_buf = bigalloc(ndyn * sizeof(Rela));
	}
	wjobs.n = 0;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC || !osecs.v[topcmds.v[c].idx].live)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD) {
				ISD *d = &isds.v[os->cmds.v[k].idx];
				for (uint32_t s = 0; s < d->secs.n; s++)
					VPUSH(wjobs, d->secs.v[s]);
			}
	}
	pfor(wjobs.n, write_isec, NULL);
	phase("copy + relocate");
	fill_symtab();
	write_synthetic();
	write_headers();
	phase("symtab, headers");
	if (atomic_load(&nerrors))
		return 1;
	write_build_id();
	phase("build id");
	int rc = write_output();
	phase("write");
	if (timing)
		fprintf(errf, "  %-28s %7.2f ms (%u objects, %u sections, %u globals, %llu MB out, %d threads)\n",
			"total", (now() - t0) * 1e3, objs.n, isecs.n, gorder.n,
			(unsigned long long)(filesize >> 20), nthreads);
	munmap(out, filesize);
	if (reladyn_buf)
		munmap(reladyn_buf, ndyn * sizeof(Rela));
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			munmap((void *)objs.v[i]->buf, objs.v[i]->size);
	gtable_free();
	return rc || atomic_load(&nerrors) ? 1 : 0;
}

/* ================= ld -r ================= */

/*
 * Relocatable output (vmlinux.o for modpost).  Input sections are grouped
 * by name into output sections in order of first appearance, contents are
 * copied unrelocated and every relocation is re-emitted: section-symbol
 * references move to the output section's symbol with the input section's
 * offset folded into the addend.  SHF_MERGE sections are concatenated, not
 * deduplicated, and .llvm_addrsig is dropped (its symbol indices would be
 * stale).  Byte identity with lld is not a goal here; modpost's view is.
 */
typedef struct {
	const char *name;
	uint32_t h;
	uint32_t type, align, entsize;
	uint64_t flags, size, off;
	VEC(uint32_t) secs;
	uint64_t nrela, relaoff;
	uint32_t shidx, relaidx, nameoff, relanameoff;
	uint32_t secsym;
} ROut;
static VEC(ROut) routs;
static uint32_t *rmap;			/* name hash table -> ROut */
static uint32_t rmask;
static uint32_t **lmap;			/* per object: local symbol -> output index */
static uint8_t *rbuf;			/* output */
static uint64_t r_symoff, r_stroff, r_shstroff, r_shstrsize;
static uint64_t r_nsyms, r_nlocal;
static uint32_t r_symidx, r_stridx, r_shstridx, r_nsh;
static uint64_t *r_locstr;		/* per object string offset of its locals */
static uint64_t *r_loc0;

static uint32_t rout_get(const char *name)
{
	uint32_t h = strhash(name, strlen(name));
	uint32_t j = h & rmask;
	while (rmap[j] != NONE) {
		ROut *r = &routs.v[rmap[j]];
		if (r->h == h && !strcmp(r->name, name))
			return rmap[j];
		j = (j + 1) & rmask;
	}
	ROut r = {.name = name, .h = h, .align = 1};
	VPUSH(routs, r);
	rmap[j] = routs.n - 1;
	return routs.n - 1;
}

static void r_count_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	uint32_t n = 0;
	uint64_t str = 0;
	if (o->buf)
		for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
			const Sym *y = &o->sym[s];
			if ((y->st_info & 15) == STT_SECTION)
				continue;
			n++;
			str += strlen(o->str + y->st_name) + 1;
		}
	o->nloc = n;
	o->locstr = str;
	lmap[i] = o->firstg ? alloc(o->firstg * sizeof(uint32_t)) : NULL;
}

static void r_write_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf)
		return;
	Sym *d = (Sym *)(rbuf + r_symoff) + r_loc0[i];
	uint32_t idx = r_loc0[i];
	uint64_t so = r_locstr[i];
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t s = 1; s < o->firstg && s < o->nsym; s++) {
		const Sym *y = &o->sym[s];
		if ((y->st_info & 15) == STT_SECTION) {
			uint32_t sec = o->isec0 + y->st_shndx;
			lmap[i][s] = y->st_shndx < o->nsh && isecs.v[sec].kind != K_NONE ?
				     routs.v[isecs.v[sec].osec].secsym : 0;
			continue;
		}
		uint16_t shndx = y->st_shndx;
		uint64_t val = y->st_value;
		if (shndx != SHN_UNDEF && shndx < 0xff00) {
			ISec *is = &isecs.v[o->isec0 + shndx];
			if (is->kind == K_NONE) {
				shndx = SHN_UNDEF;
				val = 0;
			} else {
				shndx = routs.v[is->osec].shidx;
				val += is->off;
			}
		}
		lmap[i][s] = idx++;
		put_sym(d++, strbuf, &so, o->str + y->st_name, y->st_info, y->st_other, shndx, val, y->st_size);
	}
}

static void r_write_globals(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < gorder.n ? b + 4096 : gorder.n;
	uint8_t *strbuf = rbuf + r_stroff;
	for (uint32_t i = b; i < e; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];
		Sym *d = (Sym *)(rbuf + r_symoff) + s->outidx;
		uint64_t so = s->val;		/* string offset parked here, see below */
		uint16_t shndx = SHN_UNDEF;
		uint64_t val = 0, size = 0;
		if (s->state == GS_OBJ) {
			size = s->size;
			if (s->sec == NONE) {
				shndx = SHN_ABS;
				val = objs.v[s->obj]->sym[s->symidx].st_value;
			} else {
				ISec *is = &isecs.v[s->sec];
				shndx = routs.v[is->osec].shidx;
				val = is->off + objs.v[s->obj]->sym[s->symidx].st_value;
			}
		}
		put_sym(d, strbuf, &so, gname(g), (uint8_t)(s->bind << 4 | s->type), s->vis, shndx, val, size);
	}
}

static void r_write_isec(void *arg, size_t j)
{
	(void)arg;
	uint32_t si = wjobs.v[j];
	ISec *is = &isecs.v[si];
	ROut *r = &routs.v[is->osec];
	if (is->type != SHT_NOBITS && r->type != SHT_NOBITS && is->size)
		memcpy(rbuf + r->off + is->off, is->data, is->size);
	if (!is->nrela)
		return;
	Obj *o = objs.v[is->obj];
	Rela *d = (Rela *)(rbuf + r->relaoff) + is->dyn0;
	for (uint32_t k = 0; k < is->nrela; k++) {
		const Rela *rl = &is->rela[k];
		uint32_t symi = rl->r_info >> 32, type = (uint32_t)rl->r_info;
		int64_t addend = rl->r_addend;
		uint32_t ns;
		if (symi == 0) {
			ns = 0;
		} else if (symi >= o->firstg) {
			ns = gsyms[o->gs[symi - o->firstg]].outidx;
		} else {
			const Sym *y = &o->sym[symi];
			ns = lmap[is->obj][symi];
			if ((y->st_info & 15) == STT_SECTION && y->st_shndx < o->nsh) {
				ISec *t = &isecs.v[o->isec0 + y->st_shndx];
				if (t->kind == K_NONE)
					error("%s:(%s): relocation against discarded section %s", o->path, is->name, t->name);
				else
					addend += t->off;
			}
		}
		d[k].r_offset = rl->r_offset + is->off;
		d[k].r_info = (uint64_t)ns << 32 | type;
		d[k].r_addend = addend;
	}
}

static int ld_relocatable(void)
{
	/* global order and final binding */
	build_gorder();
	pfor(gorder.n, finish_syms, NULL);

	/* output sections by name, first appearance */
	uint32_t cap = 1024;
	while (cap < isecs.n)
		cap *= 2;
	rmap = alloc(cap * sizeof(uint32_t));
	memset(rmap, 0xff, cap * sizeof(uint32_t));
	rmask = cap - 1;
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];
		if (is->kind != K_NORMAL)
			continue;
		uint32_t ri = rout_get(is->name);
		ROut *r = &routs.v[ri];
		if (!r->secs.n) {
			r->type = is->type;
			r->flags = is->flags;
			r->entsize = is->entsize;
		} else {
			if (r->type != is->type && !(r->type == SHT_NOBITS && is->type == SHT_NOBITS))
				r->type = SHT_PROGBITS;
			if (r->entsize != is->entsize)
				r->entsize = 0;
			r->flags |= is->flags;
		}
		if (is->align > r->align)
			r->align = is->align;
		is->osec = ri;
		r->size = alignup(r->size, is->align);
		is->off = r->size;
		r->size += is->size;
		is->dyn0 = r->nrela;
		r->nrela += is->nrela;
		VPUSH(r->secs, i);
	}
	phase("-r: output sections");

	/* section header indices and names */
	uint32_t sh = 1;
	uint64_t shs = 1;
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		r->shidx = sh++;
		r->nameoff = shs;
		shs += strlen(r->name) + 1;
		if (r->nrela) {
			r->relaidx = sh++;
			r->relanameoff = shs;
			shs += strlen(r->name) + 6;
		}
	}
	if (sh > 0xff00) {
		error("-r: too many output sections (%u)", sh);
		return 1;
	}
	r_symidx = sh++;
	r_stridx = sh++;
	r_shstridx = sh++;
	r_nsh = sh;
	uint64_t shs_symtab = shs; shs += 8;
	uint64_t shs_strtab = shs; shs += 8;
	uint64_t shs_shstrtab = shs; shs += 10;
	r_shstrsize = shs;

	/* symbols: null, section symbols, locals per file, globals */
	lmap = alloc(objs.n * sizeof(uint32_t *));
	pfor(objs.n, r_count_locals, NULL);
	r_loc0 = alloc(objs.n * sizeof(uint64_t));
	r_locstr = alloc(objs.n * sizeof(uint64_t));
	uint64_t idx = 1 + routs.n, str = 1;
	for (uint32_t i = 0; i < routs.n; i++)
		routs.v[i].secsym = 1 + i;
	for (uint32_t i = 0; i < objs.n; i++) {
		r_loc0[i] = idx;
		r_locstr[i] = str;
		idx += objs.v[i]->nloc;
		str += objs.v[i]->locstr;
	}
	r_nlocal = idx;
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		s->outidx = idx++;
		/* s->val is unused for -r output: park the string offset there */
		s->val = str;
		str += strlen(gname(gorder.v[i])) + 1;
	}
	r_nsyms = idx;
	uint64_t strsz = str;

	/* file layout */
	uint64_t off = sizeof(Ehdr);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		off = alignup(off, r->align);
		r->off = off;
		if (r->type != SHT_NOBITS)
			off += r->size;
	}
	off = alignup(off, 8);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		if (!r->nrela)
			continue;
		r->relaoff = off;
		off += r->nrela * sizeof(Rela);
	}
	r_symoff = off;
	off += r_nsyms * sizeof(Sym);
	r_stroff = off;
	off += strsz;
	r_shstroff = off;
	off += r_shstrsize;
	uint64_t shoff_r = alignup(off, 8);
	uint64_t fsize = shoff_r + (uint64_t)r_nsh * sizeof(Shdr);
	rbuf = bigalloc(fsize);
	phase("-r: layout");

	/* contents and relocations */
	wjobs.n = 0;
	for (uint32_t i = 0; i < routs.n; i++)
		for (uint32_t k = 0; k < routs.v[i].secs.n; k++)
			VPUSH(wjobs, routs.v[i].secs.v[k]);
	/* local maps must exist before relocations are rewritten */
	pfor(objs.n, r_write_locals, NULL);
	pfor(wjobs.n, r_write_isec, NULL);
	pfor((gorder.n + 4095) / 4096, r_write_globals, NULL);
	{
		Sym *sy = (Sym *)(rbuf + r_symoff);
		for (uint32_t i = 0; i < routs.n; i++)
			sy[1 + i] = (Sym){.st_info = STB_LOCAL << 4 | STT_SECTION, .st_shndx = routs.v[i].shidx};
	}
	phase("-r: copy, relocations, symbols");

	/* headers */
	char *shstr = (char *)rbuf + r_shstroff;
	Shdr *shd = (Shdr *)(rbuf + shoff_r);
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *r = &routs.v[i];
		strcpy(shstr + r->nameoff, r->name);
		shd[r->shidx] = (Shdr){.sh_name = r->nameoff, .sh_type = r->type, .sh_flags = r->flags,
				       .sh_offset = r->off, .sh_size = r->size, .sh_addralign = r->align,
				       .sh_entsize = r->entsize};
		if (r->nrela) {
			sprintf(shstr + r->relanameoff, ".rela%s", r->name);
			shd[r->relaidx] = (Shdr){.sh_name = r->relanameoff, .sh_type = SHT_RELA, .sh_flags = SHF_INFO_LINK,
						 .sh_offset = r->relaoff, .sh_size = r->nrela * sizeof(Rela),
						 .sh_link = r_symidx, .sh_info = r->shidx, .sh_addralign = 8,
						 .sh_entsize = sizeof(Rela)};
		}
	}
	strcpy(shstr + shs_symtab, ".symtab");
	strcpy(shstr + shs_strtab, ".strtab");
	strcpy(shstr + shs_shstrtab, ".shstrtab");
	shd[r_symidx] = (Shdr){.sh_name = shs_symtab, .sh_type = SHT_SYMTAB, .sh_offset = r_symoff,
			       .sh_size = r_nsyms * sizeof(Sym), .sh_link = r_stridx, .sh_info = r_nlocal,
			       .sh_addralign = 8, .sh_entsize = sizeof(Sym)};
	shd[r_stridx] = (Shdr){.sh_name = shs_strtab, .sh_type = SHT_STRTAB, .sh_offset = r_stroff,
			       .sh_size = strsz, .sh_addralign = 1};
	shd[r_shstridx] = (Shdr){.sh_name = shs_shstrtab, .sh_type = SHT_STRTAB, .sh_offset = r_shstroff,
				 .sh_size = r_shstrsize, .sh_addralign = 1};
	Ehdr *eh = (Ehdr *)rbuf;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = 183;
	eh->e_version = 1;
	eh->e_shoff = shoff_r;
	eh->e_ehsize = sizeof(Ehdr);
	eh->e_shentsize = sizeof(Shdr);
	eh->e_shnum = r_nsh;
	eh->e_shstrndx = r_shstridx;
	if (atomic_load(&nerrors)) {
		munmap(rbuf, fsize);
		return 1;
	}
	out = rbuf;
	filesize = fsize;
	int rc = write_output();
	phase("-r: write");
	munmap(rbuf, fsize);
	out = NULL;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			munmap((void *)objs.v[i]->buf, objs.v[i]->size);
	gtable_free();
	memset(&routs, 0, sizeof routs);
	return rc || atomic_load(&nerrors) ? 1 : 0;
}

static pthread_mutex_t ldlock = PTHREAD_MUTEX_INITIALIZER;

int ld_main(int argc, char **argv, int dirfd, FILE *err)
{
	pthread_mutex_lock(&ldlock);
	errf = err ? err : stderr;
	dirfd_base = dirfd;
	int rc = run(argc, argv);
	free_arenas();
	pthread_mutex_unlock(&ldlock);
	return rc;
}

#ifdef VLD_MAIN
int main(int argc, char **argv)
{
	return ld_main(argc, argv, AT_FDCWD, stderr);
}
#endif
