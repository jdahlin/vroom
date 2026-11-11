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
#include "../lib/buf.h"
#include "../lib/defer.h"
#include "../lib/ckd.h"
#include "../lib/vec.h"
#include "../lib/ldhash.h"
#include "../lib/vperf.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <time.h>
#include <unistd.h>

#define NONE 0xffffffffu

#include "ldelf.h"

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
	static bool prev_multi;	/* lld: a blank line after a multi-line diagnostic */
	va_list aq;
	char small[512], *m = small;
	int n;

	va_copy(aq, ap);
	n = vsnprintf(small, sizeof small, fmt, ap);
	if (n >= (int)sizeof small && (m = malloc((size_t)n + 1)))
		vsnprintf(m, (size_t)n + 1, fmt, aq);
	va_end(aq);
	if (!m)
		m = small;
	{
		VLOCK(errlock);
		if (prev_multi)
			fputc('\n', errf);
		prev_multi = strchr(m, '\n') != NULL;
		fprintf(errf, "%s: %s: %s\n", argv0, kind, m);
	}
	if (m != small)
		free(m);
}
static void error(const char *fmt, ...)
{
	int n = atomic_fetch_add(&nerrors, 1);
	if (error_limit && n >= error_limit) {
		if (n == error_limit) {
			VLOCK(errlock);
			fprintf(errf, "%s: error: too many errors emitted, stopping now\n", argv0);
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
static vperf p_last;			/* main thread's counters at the last phase */
static long flt_last;			/* process minor faults at the last phase */
static long minflt(void)
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}
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
	vperf p = vperf_now(), d = vperf_sub(p, p_last);
	long f = minflt();
	fprintf(errf, "  %-28s %7.2f ms %8.1f M ins (main thread) %6ld faults\n", name, (t - t_last) * 1e3,
		d.ins / 1e6, f - flt_last);
	t_last = t;
	p_last = p;
	flt_last = f;
}

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
		{
			VLOCK(plock);
			while (job.gen == seen)
				pthread_cond_wait(&pcv, &plock);
			seen = job.gen;
		}
		run_job();
		{
			VLOCK(plock);
			if (--job.active == 0)
				pthread_cond_signal(&pdone);
		}
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
	{
		VLOCK(plock);
		job.fn = fn;
		job.arg = arg;
		job.n = n;
		atomic_store(&job.next, 0);
		job.active = pool_started - 1;
		job.gen++;
		pthread_cond_broadcast(&pcv);
	}
	run_job();
	VLOCK(plock);
	while (job.active)
		pthread_cond_wait(&pdone, &plock);
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
	const char *entry;
	int orphan;			/* --orphan-handling: 0 place, 1 warn, 2 error */		/* -e/--entry: over the script's ENTRY() */
	const char *comment;		/* .comment linker string */
	const char *build_id;		/* NULL, "sha1", "md5", "fast" */
	const char *map;
	bool relocatable, shared, bsymbolic, notext, no_apply_dyn, no_undefined;
	bool discard_locals, discard_all, strip_debug, pic_veneer, nmagic;
	bool stubs;			/* define unresolved symbols as -O0 stubs */
	bool relax;
	uint64_t max_page;
	VVEC(InputArg) inputs;
	VVEC(const char *) libdirs;
	bool bstatic;
	bool gc, print_gc;		/* --gc-sections, --print-gc-sections */
	bool norelro;			/* -z norelro: no PT_GNU_RELRO */
	bool eh_frame_hdr;		/* --eh-frame-hdr: .eh_frame_hdr and PT_GNU_EH_FRAME */
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
	_Atomic uint8_t *lgot;		/* local k needs a .got slot (only when there is a .got) */
	uint32_t *lgotidx;		/* its slot */
	uint32_t idx;
	/* symbol table output */
	uint32_t nloc, loc0;		/* kept locals, first output index */
	uint32_t (*kept)[2];		/* kept locals: symbol index, name length */
	uint64_t locstr;		/* strtab bytes of kept locals */
	uint64_t str0;
	bool mapped;
} Obj;

static VVEC(Obj *) objs;

enum { K_NONE, K_NORMAL, K_MERGE_IN, K_MERGE, K_THUNK, K_RELADYN, K_BUILDID,
       K_SYMTAB, K_STRTAB, K_SHSTRTAB, K_COMMENT, K_GOT, K_EHHDR };

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
	bool thunked;			/* some relocation has a tr_put entry */
	bool dropped;			/* member of a COMDAT group seen before */
	uint32_t gnext;			/* next member of its section group + 1, 0: none */
	uint32_t ehcie;			/* .eh_frame FDE piece: its CIE piece + 1 (the CIE pointer is written) */
} ISec;

#define OS_DISCARD 0xfffffffeu
static VVEC(ISec) isecs;

/* SHF_MERGE input split into pieces */
typedef struct {
	uint32_t isec;
	uint32_t syn;			/* merge synthetic ISec id */
	uint32_t npieces;
	uint32_t *inoff;		/* piece input offsets */
	uint32_t *hash;			/* 31-bit hash */
	uint32_t *outoff;		/* offset in the synthetic section */
} MergeIn;
static VVEC(MergeIn) mergeins;

typedef struct {
	uint32_t isec;			/* the synthetic ISec */
	uint32_t osec;
	uint64_t flags;
	uint32_t entsize, align;
	VVEC(uint32_t) members;		/* MergeIn ids in order */
	uint64_t shardoff[32], shardsize[32];
	/* piece lists per shard for writing: (mergein, piece) of unique pieces */
	VVEC(uint64_t) uniq[32];
} Merge;
static VVEC(Merge) merges;

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
	_Atomic uint64_t def;		/* best definition: rank<<62 | obj<<32 | symidx (rank: 0 strong, 1 common, 2 weak) */
	_Atomic uint64_t first;		/* first occurrence, for symbol table order */
	_Atomic uint8_t visrank;	/* min visibility (rank, see visrank()) */
	uint8_t state, type, bind, vis, hidden_script;
	uint8_t demoted, provide_live, scriptdef, dup, common;
	_Atomic uint8_t strongref;	/* some non-weak undefined reference */
	uint32_t obj;			/* defining object, NONE for script */
	uint32_t symidx;
	uint32_t sec;			/* GS_OBJ: ISec id or NONE (absolute) */
	uint64_t val, size;
	EV ev;				/* GS_SCRIPT: section + offset */
	uint32_t order;			/* index among globals in insertion order */
	uint32_t outidx;		/* index in the output .symtab */
	uint32_t thunk;
	uint64_t fva;			/* after layout: gsym_va() */
	bool fabs, fundef;		/* gsym_isabs(); undefined or demoted */
	_Atomic uint8_t needgot;	/* some GOT relocation names it */
	uint32_t gotidx;		/* its .got slot */
} G;

static _Atomic(const char *) *hnames;	/* per slot */
static _Atomic uint64_t *hword;		/* per slot: hash << 32 | id + 1, 0 until published */
static const char **gnames;		/* per id */
static G *gsyms;			/* per id */
static _Atomic uint32_t ngsyms;
static uint32_t hmask;
static VVEC(uint32_t) gorder;		/* ids in symbol table order */

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

/*
 * Id of name, inserting if needed (lock-free).  The open-addressing slots
 * hold only the name and a published word (hash << 32 | id + 1); symbol
 * state lives in a dense array indexed by id, so the ~12k globals touch
 * ~1 MB instead of a sparse 100-byte-per-slot table.  Ids depend on thread
 * timing; nothing orders by them.
 */
static uint32_t gintern(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = strhash(name, n);
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
static uint32_t gfind(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = strhash(name, n);
	for (uint32_t i = h & hmask;; i = (i + 1) & hmask) {
		const char *cur = atomic_load(&hnames[i]);
		if (!cur)
			return NONE;
		uint64_t w = atomic_load(&hword[i]);
		if ((uint32_t)(w >> 32) == h && !strcmp(cur, name))
			return (uint32_t)w - 1;
	}
}
static inline const char *gname(uint32_t g) { return gnames[g]; }

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
	VVEC(uint32_t) secs;		/* ISec ids, in final order */
	uint32_t thunksec;		/* ISec of this ISD's thunk section, or NONE */
	uint64_t thunk_off;		/* pass-0 isecLimit */
	struct Call *calls;		/* CALL26/JUMP26 relocations, built by thunk pass 0 */
	uint32_t ncalls;
} ISD;
static VVEC(ISD) isds;

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
	VVEC(Cmd) cmds;
	bool nonalloc, noload, discard, used_in_expr, has_input, live, type_set;
	bool uses_syms;			/* expressionsUseSymbols */
	uint32_t type, addralign, entsize;
	uint64_t flags;
	uint64_t va, size, offset;
	uint32_t shidx, nameoff;
	int ptload;			/* index into phdrs, -1 */
	uint32_t link, info;
} OSec;
static VVEC(OSec) osecs;
static VVEC(Cmd) topcmds;
static const char *entry_name;

/* names referenced by expressions, in parse order (lld referencedSymbols) */
static VVEC(uint32_t) refsyms;
typedef struct { uint32_t sym; VVEC(uint32_t) refs; } Provide;
static VVEC(Provide) provides;

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
	if (is->kind == K_MERGE_IN && is->osec != OS_DISCARD)	/* a discarded one has no MergeIn */
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

static VVEC(const char *) assert_fail;	/* recorded failures of the current pass */

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
			VVEC_PUSH(assert_fail, e->msg, alloc);
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

/* character classes of the script lexer (NUL behaves as strchr() made it) */
enum { CT_NAMEEND = 1, CT_PATEND = 2, CT_IDENT = 4, CT_WORD = 8 };
#define CT_AN (CT_IDENT | CT_WORD)
static const uint8_t ctab[256] = {
	[0] = CT_NAMEEND | CT_PATEND | CT_IDENT,
	[' '] = CT_NAMEEND | CT_PATEND, ['\t'] = CT_NAMEEND | CT_PATEND,
	['\r'] = CT_NAMEEND | CT_PATEND, ['\n'] = CT_NAMEEND | CT_PATEND,
	['('] = CT_NAMEEND | CT_PATEND, [')'] = CT_NAMEEND | CT_PATEND,
	['{'] = CT_NAMEEND, ['}'] = CT_NAMEEND, [':'] = CT_NAMEEND, [';'] = CT_NAMEEND,
	[','] = CT_NAMEEND, ['='] = CT_NAMEEND,
	['0' ... '9'] = CT_AN, ['A' ... 'Z'] = CT_AN, ['a' ... 'z'] = CT_AN,
	['_'] = CT_AN, ['.'] = CT_AN, ['$'] = CT_IDENT,
};

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
	while (sp < sp_end && !(ctab[(unsigned char)*sp] & CT_NAMEEND))
		sp++;
	return sdup(b, sp);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
static char *spatname(void)
{
	sskip();
	const char *b = sp;
	while (sp < sp_end && !(ctab[(unsigned char)*sp] & CT_PATEND))
		sp++;
	return sdup(b, sp);
}
static inline __attribute__((always_inline)) bool sword(const char *w)
{
	sskip();
	size_t n = strlen(w);
	if ((size_t)(sp_end - sp) >= n && !memcmp(sp, w, n) &&
	    (sp + n == sp_end || !(ctab[(unsigned char)sp[n]] & CT_WORD))) {
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
		VVEC_PUSH(cur_provide->refs, g, alloc);
	else
		VVEC_PUSH(refsyms, g, alloc);
}

static int prec(const char *op)
{
	char c = op[0], c1 = c ? op[1] : 0;
	if (c1 && op[2])
		return -1;
	switch (c) {
	case '*': case '/': case '%': return c1 ? -1 : 11;
	case '+': case '-': return c1 ? -1 : 10;
	case '<': case '>': return c1 == c ? 9 : !c1 || c1 == '=' ? 8 : -1;
	case '=': case '!': return c1 == '=' ? 7 : -1;
	case '&': return !c1 ? 6 : c1 == '&' ? 3 : -1;
	case '^': return c1 ? -1 : 5;
	case '|': return !c1 ? 4 : c1 == '|' ? 2 : -1;
	case '?': return c1 ? -1 : 1;
	default: return -1;
	}
}
/* peek an operator token (longest match of << >> <= >= == != && || op= and
 * the single-character operators); returns length or 0 */
static int peekop(char out[4])
{
	sskip();
	size_t left = (size_t)(sp_end - sp);
	if (!left)
		return 0;
	char c = sp[0], c1 = left > 1 ? sp[1] : 0, c2 = left > 2 ? sp[2] : 0;
	int n;
	switch (c) {
	case '<': case '>': n = c1 == c ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1; break;
	case '=': n = c1 == '=' ? 2 : 1; break;
	case '!': n = c1 == '=' ? 2 : 0; break;
	case '&': case '|': n = c1 == c || c1 == '=' ? 2 : 1; break;
	case '+': case '-': case '*': case '/': case '^': n = c1 == '=' ? 2 : 1; break;
	case '%': case '?': case ':': n = 1; break;
	default: n = 0;
	}
	memcpy(out, sp, (size_t)n);
	out[n] = 0;
	return n;
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
	while (sp < sp_end && (ctab[(unsigned char)*sp] & CT_IDENT))
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
	VVEC_PUSH(osecs, o, alloc);
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
	VVEC_PUSH(provides, ((Provide){.sym = gintern(name)}), alloc);
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
	VVEC(Pat) pats = {0};
	sexpect('(');
	while (!satend() && !speek(')')) {
		Pat p = {0};
		VVEC(const char *) g = {0};
		const char *save = sp;
		char *w = spatname();
		if ((!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_ALIGNMENT")) && speek('(')) {
			p.sort = w[5] == 'B' && w[8] == 'A' ? SORT_ALIGN : SORT_NAME;
			sexpect('(');
			while (!satend() && !speek(')'))
				VVEC_PUSH(g, spatname(), alloc);
			sexpect(')');
		} else if (!strcmp(w, "EXCLUDE_FILE")) {
			error("linker script: EXCLUDE_FILE not supported");
			sp = save;
			return;
		} else {
			VVEC_PUSH(g, w, alloc);
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
				VVEC_PUSH(g, w2, alloc);
			}
		}
		p.globs = g.v;
		p.nglobs = g.n;
		VVEC_PUSH(pats, p, alloc);
	}
	sexpect(')');
	d.pats = pats.v;
	d.npats = pats.n;
	VVEC_PUSH(isds, d, alloc);
	Cmd c = {.kind = C_ISD, .idx = isds.n - 1};
	VVEC_PUSH(os->cmds, c, alloc);
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
			VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
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
				VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
			}
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			Cmd c = {.kind = C_ASSIGN, .as = a};
			VVEC_PUSH(osecs.v[osi].cmds, c, alloc);
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
	VVEC_PUSH(topcmds, c, alloc);
}

static void psections(void)
{
	sexpect('{');
	while (!satend() && !speek('}')) {
		if (sconsume(';'))
			continue;
		if (sword("ASSERT")) {
			Assign *a = passert();
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
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
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sconsume(';');
			continue;
		}
		if (sword("PROVIDE_HIDDEN") || sword("HIDDEN")) {
			Assign *a = pprovide(true);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		if (sword("PROVIDE")) {
			Assign *a = pprovide(false);
			if (a) VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
			sexpect(';');
			continue;
		}
		const char *save = sp;
		char *w = sname();
		Assign *a = passign_rest(w);
		if (a) {
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_ASSIGN, .as = a}), alloc);
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
typedef VVEC(Pending) PendVec;
static PendVec pending;

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
		if (memcmp(h + 58, "`\n", 2)) {
			error("%s: invalid archive member header at offset %zu", path, off - 60);
			return;
		}
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
			if (size > n - off) {
				error("%s: archive member // is truncated", path);
				return;
			}
			longn = (const char *)b + off;
			longlen = size;
			off += size + (size & 1);
			continue;
		}
		if (nm[0] == '/' && longn) {
			size_t i = strtoull(nm + 1, NULL, 10);
			if (i >= longlen) {
				error("%s: invalid long name offset %s", path, nm);
				return;
			}
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
			VVEC_PUSH(pending, ((Pending){.path = full}), alloc);
		} else {
			if (size > n - off) {
				error("%s(%s): archive member goes past the end of the file", path, name);
				return;
			}
			char *full = alloc(strlen(path) + strlen(name) + 3);
			sprintf(full, "%s(%s)", path, name);
			VVEC_PUSH(pending, ((Pending){.path = full, .buf = b + off, .size = size}), alloc);
			off += size + (size & 1);
		}
	}
}

/* the bytes of a relocation's field (0: none, 4: an instruction) */
static uint32_t rel_width(uint32_t type)
{
	switch (type) {
	case R_AARCH64_NONE: return 0;
	case R_AARCH64_ABS64: case R_AARCH64_PREL64: return 8;
	case R_AARCH64_ABS16: case R_AARCH64_PREL16: return 2;
	default: return 4;
	}
}

/* a string table: in the file, NUL-terminated */
static bool strtab_ok(const Obj *o, uint32_t k)
{
	const Shdr *h = &o->sh[k];

	return h->sh_type == SHT_STRTAB && h->sh_size && o->buf[h->sh_offset + h->sh_size - 1] == 0;
}

static bool obj_quiet;	/* the sequential lazy-archive pass: no messages */

/* every offset, index and size the later passes use, checked once (as lld's
 * ObjFile: the message names what is wrong; the object is then dropped) */
static bool obj_valid(Obj *o, const Ehdr *eh)
{
#define BAD(...) do { if (!obj_quiet) error(__VA_ARGS__); return false; } while (0)
	uint64_t n = o->size;

	if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Shdr) || eh->e_shoff % 8 ||
	    eh->e_shoff > n || n - eh->e_shoff < sizeof(Shdr))
		BAD("%s: invalid section header table offset", o->path);
	o->sh = (const Shdr *)(o->buf + eh->e_shoff);
	/* more than 0xff00 sections: the count in sh[0].sh_size, the index in sh_link */
	uint64_t nsh = eh->e_shnum ? eh->e_shnum : o->sh[0].sh_size;
	uint32_t shstrndx = eh->e_shstrndx == SHN_XINDEX ? o->sh[0].sh_link : eh->e_shstrndx;

	if (nsh == 0 || nsh > (n - eh->e_shoff) / sizeof(Shdr))
		BAD("%s: section header table goes past the end of the file", o->path);
	o->nsh = nsh;
	for (uint32_t s = 0; s < o->nsh; s++) {	/* (0 too: shstrndx and sh_link may name it) */
		const Shdr *h = &o->sh[s];

		if (h->sh_type != SHT_NOBITS && h->sh_type != SHT_NULL && !vckd_range(h->sh_offset, h->sh_size, n))
			BAD("%s: section %u goes past the end of the file", o->path, s);
		if (h->sh_addralign & (h->sh_addralign - 1))
			BAD("%s: section %u: sh_addralign is not a power of 2", o->path, s);
	}
	if (shstrndx >= o->nsh || !strtab_ok(o, shstrndx))
		BAD("%s: invalid section name string table", o->path);
	o->shstr = (const char *)o->buf + o->sh[shstrndx].sh_offset;
	uint64_t shstrsz = o->sh[shstrndx].sh_size;
	const Shdr *symtab = NULL;

	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];

		if (h->sh_name >= shstrsz)
			BAD("%s: section %u: invalid sh_name", o->path, s);
		if (h->sh_type == SHT_SYMTAB) {
			if (symtab)
				BAD("%s: more than one SHT_SYMTAB", o->path);
			symtab = h;
		}
	}
	uint64_t nsym = 0, strsz = 0;

	if (symtab) {
		if (symtab->sh_offset % 8 || symtab->sh_size % sizeof(Sym))
			BAD("%s: invalid SHT_SYMTAB", o->path);
		if (symtab->sh_link >= o->nsh || !strtab_ok(o, symtab->sh_link))
			BAD("%s: invalid symbol string table", o->path);
		nsym = symtab->sh_size / sizeof(Sym);
		if (nsym > UINT32_MAX || symtab->sh_info > nsym || (nsym && symtab->sh_info == 0))
			BAD("%s: invalid sh_info in symbol table", o->path);
		strsz = o->sh[symtab->sh_link].sh_size;
		const Sym *y = (const Sym *)(o->buf + symtab->sh_offset);

		for (uint64_t k = 0; k < nsym; k++) {
			if (y[k].st_name >= strsz)
				BAD("%s: invalid symbol name offset", o->path);
			uint16_t x = y[k].st_shndx;

			if (x == SHN_XINDEX)
				BAD("%s: SHN_XINDEX symbols are not supported", o->path);
			if (x != SHN_UNDEF && x < 0xff00 && x >= o->nsh)
				BAD("%s: invalid section index: %u", o->path, x);
		}
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];

		if (h->sh_type == SHT_GROUP) {
			if (h->sh_offset % 4 || h->sh_size % 4 || h->sh_size < 4 || !symtab ||
			    h->sh_link >= o->nsh || &o->sh[h->sh_link] != symtab || h->sh_info >= nsym)
				BAD("%s: invalid SHT_GROUP section %u", o->path, s);
			const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);

			for (uint64_t k = 1; k < h->sh_size / 4; k++)
				if (w[k] == 0 || w[k] >= o->nsh)
					BAD("%s: invalid section index in group section %u", o->path, s);
			continue;
		}
		if (h->sh_type != SHT_RELA)
			continue;
		if (h->sh_offset % 8 || h->sh_size % sizeof(Rela) || h->sh_info >= o->nsh)
			BAD("%s: invalid relocation section %u", o->path, s);
		const Shdr *t = &o->sh[h->sh_info];
		const Rela *r = (const Rela *)(o->buf + h->sh_offset);

		for (uint64_t k = 0; k < h->sh_size / sizeof(Rela); k++) {
			if ((r[k].r_info >> 32) >= nsym && (r[k].r_info >> 32))
				BAD("%s: invalid symbol index in relocation section %u", o->path, s);
			uint32_t w = rel_width((uint32_t)r[k].r_info);

			if (t->sh_type != SHT_NOBITS && (r[k].r_offset > t->sh_size || t->sh_size - r[k].r_offset < w))
				BAD("%s: relocation at 0x%llx is past the end of section %u", o->path,
				    (unsigned long long)r[k].r_offset, h->sh_info);
		}
	}
	return true;
#undef BAD
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
	if (((uintptr_t)o->buf & 7) && !o->mapped) {
		/* a member of a regular archive (at 4 mod 8, after its header): the headers need 8-byte alignment */
		uint8_t *c = alloc(o->size);

		memcpy(c, o->buf, o->size);
		o->buf = c;
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
	if (!obj_valid(o, eh)) {
		o->buf = NULL;
		return;
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		if (o->sh[s].sh_type == SHT_SYMTAB) {
			o->sym = (const Sym *)(o->buf + o->sh[s].sh_offset);
			o->nsym = o->sh[s].sh_size / sizeof(Sym);
			o->firstg = o->sh[s].sh_info;
			o->str = (const char *)o->buf + o->sh[o->sh[s].sh_link].sh_offset;
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

/* lld: the first COMDAT group of a signature (in file order) is kept; a later
 * one's members are dropped.  Their definitions still take part in resolution
 * (lld postParse); one that wins is demoted (demote_symbols), and relocations
 * against it fail as lld's "refers to a symbol in a discarded section" */
static void dedup_comdats(void)
{
	struct { const char *name; uint32_t h; } *tab = NULL;
	uint32_t mask = 0, n = 0;

	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];

		for (uint32_t s = 0; o->buf && s < o->nsh; s++) {
			const Shdr *h = &o->sh[s];
			const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);

			if (h->sh_type != SHT_GROUP)
				continue;
			/* members in a ring, for --gc-sections (lld nextInSectionGroup) */
			for (uint64_t k = 1; k < h->sh_size / 4; k++)
				isecs.v[o->isec0 + w[k]].gnext = o->isec0 + w[k + 1 < h->sh_size / 4 ? k + 1 : 1] + 1;
			if (!(w[0] & 1))	/* GRP_COMDAT */
				continue;
			const char *name = o->str + o->sym[h->sh_info].st_name;
			uint32_t len = strlen(name), hh = strhash(name, len);

			if (!tab || (n + 1) * 2 > mask) {
				uint32_t m = tab ? (mask + 1) * 2 : 1024;
				__typeof__(tab) nt = calloc(m, sizeof *tab);

				for (uint32_t k = 0; tab && k <= mask; k++)
					if (tab[k].name) {
						uint32_t j = tab[k].h & (m - 1);

						while (nt[j].name)
							j = (j + 1) & (m - 1);
						nt[j] = tab[k];
					}
				free(tab);
				tab = nt;
				mask = m - 1;
			}
			uint32_t j = hh & mask;
			bool seen = false;

			for (; tab[j].name; j = (j + 1) & mask)
				if (tab[j].h == hh && !strcmp(tab[j].name, name)) {
					seen = true;
					break;
				}
			if (!seen) {
				tab[j].name = name;
				tab[j].h = hh;
				n++;
				continue;
			}
			for (uint64_t k = 1; k < h->sh_size / 4; k++) {
				ISec *is = &isecs.v[o->isec0 + w[k]];

				is->kind = K_NONE;
				is->dropped = true;
			}
		}
	}
	free(tab);
}

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
		if (y->st_shndx == SHN_COMMON && opt.relocatable)
			error("%s: common symbol %s not supported with -r", o->path, o->str + y->st_name);
		else if (y->st_shndx == SHN_UNDEF) {
			if (bind != STB_WEAK && !atomic_load_explicit(&gs->strongref, memory_order_relaxed))
				atomic_store_explicit(&gs->strongref, 1, memory_order_relaxed);
		} else
			/* lld Symbol::compare: a strong definition beats a common one, which beats a weak one */
			atomic_min64(&gs->def, (uint64_t)(bind == STB_WEAK ? 2 : y->st_shndx == SHN_COMMON) << 62 | k);
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
		if (y->st_shndx == SHN_UNDEF || y->st_shndx == SHN_COMMON || (y->st_info >> 4) == STB_WEAK)
			continue;
		G *g = &gsyms[o->gs[s - o->firstg]];
		uint64_t d = kget(&g->def);
		if ((d >> 62) == 0 && d != symkey(o->idx, s)) {
			Obj *first = objs.v[(d >> 32) & 0x3fffffff];
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
	uint32_t oi = (d >> 32) & 0x3fffffff, si = (uint32_t)d;
	Obj *o = objs.v[oi];
	const Sym *y = &o->sym[si];
	s->state = GS_OBJ;
	s->obj = oi;
	s->symidx = si;
	s->bind = y->st_info >> 4;
	s->type = y->st_info & 15;
	s->size = y->st_size;
	s->val = y->st_value;
	s->sec = y->st_shndx == SHN_ABS || y->st_shndx == SHN_COMMON ? NONE : o->isec0 + y->st_shndx;
	s->common = y->st_shndx == SHN_COMMON;
	if (s->common)
		s->type = STT_OBJECT;
	else if (y->st_shndx >= SHN_ABS && y->st_shndx != SHN_ABS)
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

/*
 * Final addresses, once layout is done: reloc_target() for the copy pass
 * from per-symbol and per-section values computed once (fin_*), falling
 * back to reloc_target() for undefined, discarded and merge-piece targets.
 */
static uint64_t *isec_fva;		/* isec_va(s, 0), or UINT64_MAX: take reloc_target */

static void fin_isec(void *arg, size_t i)
{
	(void)arg;
	ISec *is = &isecs.v[i];
	uint32_t os = isec_osec(i);
	isec_fva[i] = is->kind == K_NONE || is->kind == K_MERGE_IN || os == NONE || os == OS_DISCARD
			      ? UINT64_MAX : osecs.v[os].va + is->off;
}
static void fin_gsym(void *arg, size_t g)
{
	(void)arg;
	G *s = &gsyms[g];
	s->fundef = s->state == GS_UNDEF || s->demoted;
	if (!s->fundef) {
		s->fabs = gsym_isabs((uint32_t)g);
		s->fva = gsym_va((uint32_t)g);
	}
}
static void final_addresses(void)
{
	isec_fva = alloc((size_t)isecs.n * sizeof(uint64_t));
	pfor(isecs.n, fin_isec, NULL);
	pfor(atomic_load(&ngsyms), fin_gsym, NULL);
}

static inline void reloc_target_final(const Obj *o, uint32_t symidx, int64_t addend, Target *t)
{
	if (symidx >= o->firstg) {
		uint32_t g = o->gs[symidx - o->firstg];
		const G *s = &gsyms[g];
		if (!s->fundef) {
			t->undef = t->discarded = false;
			t->key = g;
			t->global = true;
			t->abs = s->fabs;
			t->s = s->fva;
			t->va = s->fva + addend;
			return;
		}
	} else {
		const Sym *y = &o->sym[symidx];
		if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
			uint64_t b = isec_fva[o->isec0 + y->st_shndx];
			if (b != UINT64_MAX) {
				t->undef = t->discarded = false;
				t->key = symidx;
				t->global = false;
				t->abs = false;
				t->s = b + ((y->st_info & 15) == STT_SECTION ? 0 : y->st_value);
				t->va = t->s + addend;
				return;
			}
		}
	}
	reloc_target(o, symidx, addend, t);
}

/* ---------------- output state ---------------- */

static uint8_t *out;			/* output file image */
static uint64_t filesize, shoff;
static VVEC(Phdr) phdrs;
typedef struct { int first, last; bool lma_set; } PhdrSecs;
static VVEC(PhdrSecs) phsecs;		/* first/last osec of each phdr */
static VVEC(uint32_t) outsecs;		/* live output sections in header order */
static uint64_t ndyn;			/* RELATIVE relocations */
static uint32_t reladyn_isec = NONE, buildid_isec = NONE, comment_isec = NONE, got_isec = NONE;
static uint32_t ehhdr_isec = NONE;	/* --eh-frame-hdr */
static VVEC(uint32_t) ehfdes;		/* the FDE pieces in the output, for .eh_frame_hdr */
static VVEC(uint32_t) gotsyms;		/* .got slot -> G id (the globals, first) */
typedef struct { uint32_t obj, sym; } GotLoc;
static VVEC(GotLoc) gotlocs;		/* then the locals: slot gotsyms.n + i */
static uint32_t symtab_isec = NONE, strtab_isec = NONE, shstrtab_isec = NONE;
static uint32_t nobj_real;		/* objects excluding synthetic */

static uint32_t new_isec(const char *name, uint8_t kind, uint32_t type, uint64_t flags, uint32_t align)
{
	ISec is = {.name = name, .kind = kind, .type = type, .flags = flags, .align = align,
		   .obj = NONE, .osec = NONE, .isd = NONE, .aux = NONE, .shndx = NONE};
	VVEC_PUSH(isecs, is, alloc);
	return isecs.n - 1;
}

/* ---------------- .eh_frame (lld's EhFrameSection) ---------------- */

/* an input .eh_frame split into CIE and FDE records (lld EhInputSection::split) */
typedef struct { uint32_t off, size, r0, r1, cieoff; bool cie; } EhRec;

static bool eh_input(const ISec *is)
{
	return is->kind == K_NORMAL && is->obj != NONE && (is->flags & SHF_ALLOC) && !strcmp(is->name, ".eh_frame") &&
	       !opt.relocatable;
}

/* the records of input section si, relocations given to the record holding them; NULL on error */
static EhRec *eh_split(uint32_t si, uint32_t *nrec)
{
	const ISec *is = &isecs.v[si];
	const char *path = objs.v[is->obj]->path;
	EhRec *v = NULL;
	uint32_t n = 0, cap = 0, r = 0;

	for (uint64_t off = 0; off + 4 <= is->size;) {
		uint32_t len = rd32(is->data + off);

		if (len == 0xffffffffu) {
			error("%s:(.eh_frame+0x%llx): 64-bit DWARF records are not supported", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		if (len == 0)	/* a terminator */
			break;
		if (len < 4 || off + 4 + len > is->size) {
			error("%s:(.eh_frame+0x%llx): CIE/FDE too small", path, (unsigned long long)off);
			free(v);
			return NULL;
		}
		uint32_t id = rd32(is->data + off + 4);
		EhRec e = {.off = (uint32_t)off, .size = len + 4, .cie = id == 0};

		if (!e.cie) {
			if (id > off + 4) {
				error("%s:(.eh_frame+0x%llx): invalid CIE reference", path, (unsigned long long)off);
				free(v);
				return NULL;
			}
			e.cieoff = (uint32_t)(off + 4 - id);
		}
		while (r < is->nrela && is->rela[r].r_offset < off)
			r++;
		e.r0 = r;
		while (r < is->nrela && is->rela[r].r_offset < off + e.size)
			r++;
		e.r1 = r;
		if (n == cap) {
			cap = cap ? 2 * cap : 16;
			v = realloc(v, cap * sizeof *v);
		}
		v[n++] = e;
		off += e.size;
	}
	for (uint32_t k = 1; k < is->nrela; k++)
		if (is->rela[k].r_offset < is->rela[k - 1].r_offset) {
			error("%s:(.eh_frame): relocations are not sorted", path);
			free(v);
			return NULL;
		}
	*nrec = n;
	return v;
}

/* the input section relocation symbol y of object o is in, or NONE */
static uint32_t rel_isec(const Obj *o, uint32_t y)
{
	if (y >= o->firstg) {
		const G *g = &gsyms[o->gs[y - o->firstg]];

		return g->state == GS_OBJ ? g->sec : NONE;
	}
	return o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00 ? o->isec0 + o->sym[y].st_shndx : NONE;
}

static bool eh_live(uint32_t s)
{
	return s != NONE && isecs.v[s].kind != K_NONE && !isecs.v[s].dropped && isecs.v[s].osec != OS_DISCARD;
}

/* a CIE's identity: its bytes and its relocations (type, place, addend, target) */
static bool cie_same(uint32_t sa, const EhRec *a, uint32_t sb, const EhRec *b)
{
	const ISec *x = &isecs.v[sa], *y = &isecs.v[sb];
	const Obj *ox = objs.v[x->obj], *oy = objs.v[y->obj];

	if (a->size != b->size || a->r1 - a->r0 != b->r1 - b->r0 || memcmp(x->data + a->off, y->data + b->off, a->size))
		return false;
	for (uint32_t k = 0; k < a->r1 - a->r0; k++) {
		const Rela *p = &x->rela[a->r0 + k], *q = &y->rela[b->r0 + k];
		uint32_t py = p->r_info >> 32, qy = q->r_info >> 32;

		if ((uint32_t)p->r_info != (uint32_t)q->r_info || p->r_addend != q->r_addend ||
		    p->r_offset - a->off != q->r_offset - b->off)
			return false;
		bool pg = py >= ox->firstg, qg = qy >= oy->firstg;

		if (pg != qg)
			return false;
		if (pg ? ox->gs[py - ox->firstg] != oy->gs[qy - oy->firstg] : (x->obj != y->obj || py != qy))
			return false;
	}
	return true;
}

/* a record as an input section of its own */
static uint32_t eh_piece(uint32_t si, const EhRec *e)
{
	ISec src = isecs.v[si];
	uint32_t p = new_isec(src.name, K_NORMAL, src.type, src.flags, 1);
	ISec *is = &isecs.v[p];
	Rela *rl = e->r1 > e->r0 ? alloc((e->r1 - e->r0) * sizeof(Rela)) : NULL;

	for (uint32_t k = e->r0; k < e->r1; k++) {
		rl[k - e->r0] = src.rela[k];
		rl[k - e->r0].r_offset -= e->off;
	}
	is->obj = src.obj;
	is->shndx = src.shndx;
	is->isd = src.isd;
	is->data = src.data + e->off;
	is->size = e->size;
	is->rela = rl;
	is->nrela = e->r1 - e->r0;
	return p;
}

/*
 * The .eh_frame inputs of each input section description become records,
 * placed where the first of them was: a CIE once per identity, in the order
 * of first use by a live FDE, each followed by its FDEs; an FDE is kept when
 * the section its first relocation names is live (lld's isFdeLive), and a
 * CIE without FDEs is dropped (lld's EhFrameSection::addRecords/finalize).
 */
static void eh_frames(void)
{
	for (uint32_t d = 0; d < isds.n; d++) {
		ISD *id = &isds.v[d];
		uint32_t first = NONE;

		for (uint32_t k = 0; k < id->secs.n && first == NONE; k++)
			if (eh_input(&isecs.v[id->secs.v[k]]))
				first = k;
		if (first == NONE)
			continue;
		/* CIE records: (section, record, its FDE pieces) */
		struct Cie { uint32_t sec; EhRec rec; VVEC(uint32_t) fdes; } *cies = NULL;
		uint32_t ncie = 0, capc = 0;
		VVEC(uint32_t) rest = {0};

		for (uint32_t k = 0; k < id->secs.n; k++) {
			uint32_t si = id->secs.v[k];

			if (!eh_input(&isecs.v[si])) {
				VVEC_PUSH(rest, si, alloc);
				continue;
			}
			uint32_t nrec;
			EhRec *rec = eh_split(si, &nrec);

			if (!rec) {
				isecs.v[si].kind = K_NONE;
				continue;
			}
			const Obj *o = objs.v[isecs.v[si].obj];

			for (uint32_t r = 0; r < nrec; r++) {
				if (rec[r].cie)
					continue;
				const ISec *is = &isecs.v[si];

				if (rec[r].r0 == rec[r].r1 || !eh_live(rel_isec(o, is->rela[rec[r].r0].r_info >> 32)))
					continue;	/* lld: an FDE without a live target */
				const EhRec *ce = NULL;

				for (uint32_t q = 0; q < nrec; q++)
					if (rec[q].cie && rec[q].off == rec[r].cieoff)
						ce = &rec[q];
				if (!ce) {
					error("%s:(.eh_frame+0x%x): invalid CIE reference", o->path, rec[r].off);
					continue;
				}
				uint32_t c = 0;

				while (c < ncie && !cie_same(cies[c].sec, &cies[c].rec, si, ce))
					c++;
				if (c == ncie) {
					if (ncie == capc) {
						capc = capc ? 2 * capc : 8;
						cies = realloc(cies, capc * sizeof *cies);
					}
					cies[ncie++] = (struct Cie){ .sec = si, .rec = *ce };
				}
				uint32_t p = eh_piece(si, &rec[r]);

				VVEC_PUSH(cies[c].fdes, p, alloc);
			}
			free(rec);
			isecs.v[si].kind = K_NONE;	/* replaced by its records */
		}
		/* the records in place of the first .eh_frame input */
		VVEC(uint32_t) out = {0};

		for (uint32_t k = 0; k < first; k++)
			VVEC_PUSH(out, rest.v[k], alloc);
		uint32_t out0 = out.n;

		for (uint32_t c = 0; c < ncie; c++) {
			uint32_t cp = eh_piece(cies[c].sec, &cies[c].rec);

			VVEC_PUSH(out, cp, alloc);
			for (uint32_t f = 0; f < cies[c].fdes.n; f++) {
				isecs.v[cies[c].fdes.v[f]].ehcie = cp + 1;
				VVEC_PUSH(out, cies[c].fdes.v[f], alloc);
				VVEC_PUSH(ehfdes, cies[c].fdes.v[f], alloc);
			}
		}
		{
			/* lld: a zero-length CIE as the terminator glibc expects; the section word aligned */
			static const uint8_t zero4[4];
			EhRec t = {.size = 4};
			uint32_t tp = eh_piece(id->secs.v[first], &t);

			isecs.v[tp].data = zero4;
			isecs.v[tp].nrela = 0;
			VVEC_PUSH(out, tp, alloc);
			isecs.v[out.v[out0]].align = 8;
		}
		for (uint32_t k = first; k < rest.n; k++)
			VVEC_PUSH(out, rest.v[k], alloc);
		id = &isds.v[d];
		id->secs.v = out.v;
		id->secs.n = out.n;
		id->secs.cap = out.cap;
		free(cies);
	}
}

#include "../arch/a64/ld/thunks.inc"

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

static VVEC(uint32_t) orphans;

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

/* lld's getSectionRank bits that order allocated sections: read-only, code,
 * then writable with RELRO (TLS first) before the rest and bss last; -1 for
 * non-allocated.  Orphans go next to the section whose rank shares the most
 * leading bits (lld getRankProximity) */
enum { RF_WRITE = 1 << 14, RF_EXEC = 1 << 12, RF_RODATA = 1 << 11, RF_NOT_RELRO = 1 << 9, RF_NOT_TLS = 1 << 8, RF_BSS = 1 << 7 };
static int orphan_rank(uint64_t flags, uint32_t type, const char *name)
{
	int r;

	if (!(flags & SHF_ALLOC))
		return -1;
	r = (flags & SHF_WRITE) ? RF_WRITE : (flags & SHF_EXECINSTR) ? RF_EXEC : RF_RODATA;
	bool relro = (flags & SHF_WRITE) &&
		     ((flags & SHF_TLS) || type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_PREINIT_ARRAY ||
		      !strcmp(name, ".got") || !strcmp(name, ".data.rel.ro") || !strcmp(name, ".bss.rel.ro") ||
		      !strcmp(name, ".ctors") || !strcmp(name, ".dtors") || !strcmp(name, ".jcr") || !strcmp(name, ".eh_frame"));
	if (!relro)
		r |= RF_NOT_RELRO;
	if (!(flags & SHF_TLS))
		r |= RF_NOT_TLS;
	if (type == SHT_NOBITS)
		r |= RF_BSS;
	return r;
}

/* orphans sort by rank; .eh_frame_hdr and .eh_frame are lld synthetics, after
 * the input orphans of their rank, the header first */
static uint64_t orphan_key(const ISec *is)
{
	unsigned r = (unsigned)orphan_rank(is->flags, is->type, is->name);
	unsigned syn = !strcmp(is->name, ".eh_frame_hdr") ? 1 : !strcmp(is->name, ".eh_frame") ? 2 : 0;

	return (uint64_t)r << 2 | syn;
}

static int rank_prox(int a, int b) { return a == b ? 32 : __builtin_clz((unsigned)(a ^ b)); }

/* the rank of a script output section, from its first input section */
static int osec_rank(uint32_t oi)
{
	OSec *os = &osecs.v[oi];
	for (uint32_t k = 0; k < os->cmds.n; k++)
		if (os->cmds.v[k].kind == C_ISD && isds.v[os->cmds.v[k].idx].secs.n) {
			ISec *is = &isecs.v[isds.v[os->cmds.v[k].idx].secs.v[0]];
			return orphan_rank(is->flags, is->type, is->name);
		}
	return -1;
}

static void ncache_init(void)
{
	if (ncache)
		return;
	uint32_t ncap = 1024;
	while (ncap < isecs.n / 4)
		ncap *= 2;
	ncache = alloc(ncap * sizeof *ncache);
	ncmask = ncap - 1;
}

/* __start_NAME / __stop_NAME with NAME a C identifier: the NAME, else NULL */
static const char *start_stop_name(const char *n)
{
	const char *s = !strncmp(n, "__start_", 8) ? n + 8 : !strncmp(n, "__stop_", 7) ? n + 7 : NULL;

	if (!s || !*s || isdigit((unsigned char)*s))
		return NULL;
	for (const char *p = s; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '_')
			return NULL;
	return s;
}

/* --gc-sections as lld's MarkLive: roots are the entry symbol, KEEP, SHF_GNU_RETAIN,
 * non-allocated sections, init/fini/preinit arrays, notes outside groups,
 * .ctors/.dtors/.init/.fini/.jcr, and under -shared the exported definitions;
 * relocations, SHF_LINK_ORDER and section groups propagate liveness; a
 * __start_X/__stop_X reference keeps nothing (lld's default -z start-stop-gc) */
static void gc_sections(const char *entry)
{
	uint8_t *live = calloc(isecs.n, 1);
	uint32_t *work = malloc(isecs.n * sizeof(uint32_t)), nw = 0;
#define MARK(x) do { uint32_t m_ = (x); if (m_ < isecs.n && !live[m_] && isecs.v[m_].kind != K_NONE) { live[m_] = 1; work[nw++] = m_; } } while (0)

	ncache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];

		if (is->kind == K_NONE || is->obj == NONE)
			continue;
		if (eh_input(is)) {
			/* lld MarkLive::scanEhFrameSection: kept, but its relocations do not
			 * keep the functions; a CIE's (the personality) keep their targets, an
			 * FDE's only outside code and section groups (an LSDA) */
			uint32_t nrec;
			EhRec *rec = eh_split(i, &nrec);
			const Obj *o = objs.v[is->obj];

			live[i] = 1;
			for (uint32_t r = 0; rec && r < nrec; r++)
				for (uint32_t k = rec[r].r0; k < rec[r].r1; k++) {
					uint32_t t = rel_isec(o, isecs.v[i].rela[k].r_info >> 32);

					if (t == NONE)
						continue;
					if (!rec[r].cie && ((isecs.v[t].flags & SHF_EXECINSTR) || isecs.v[t].gnext))
						continue;
					MARK(t);
				}
			free(rec);
			continue;
		}
		const char *n = is->name;
		uint32_t d = match_isd(n);
		bool root = !(is->flags & SHF_ALLOC) || (is->flags & 0x200000) /* SHF_GNU_RETAIN */ ||
			    is->type == SHT_INIT_ARRAY || is->type == SHT_FINI_ARRAY || is->type == SHT_PREINIT_ARRAY ||
			    (is->type == SHT_NOTE && !is->gnext) ||
			    !strncmp(n, ".ctors", 6) || !strncmp(n, ".dtors", 6) || !strncmp(n, ".init", 5) ||
			    !strncmp(n, ".fini", 5) || !strncmp(n, ".jcr", 4) || (d != NONE && isds.v[d].keep);

		if (root)
			MARK(i);
	}
	uint32_t eg = gfind(entry);

	if (eg != NONE && gsyms[eg].state == GS_OBJ)
		MARK(gsyms[eg].sec);
	for (uint32_t g = 0; opt.shared && g < atomic_load(&ngsyms); g++)
		if (gsyms[g].state == GS_OBJ && (gsyms[g].vis == STV_DEFAULT || gsyms[g].vis == STV_PROTECTED))
			MARK(gsyms[g].sec);
	for (bool again = true; again;) {
		while (nw) {
			ISec *is = &isecs.v[work[--nw]];
			Obj *o = objs.v[is->obj];

			for (uint32_t r = 0; r < is->nrela; r++) {
				uint32_t y = is->rela[r].r_info >> 32;

				if (!y)
					continue;
				if (y >= o->firstg) {
					G *gs = &gsyms[o->gs[y - o->firstg]];

					if (gs->state == GS_OBJ)
						MARK(gs->sec);
				} else if (o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00)
					MARK(o->isec0 + o->sym[y].st_shndx);
			}
			if (is->gnext)
				MARK(is->gnext - 1);
		}
		/* SHF_LINK_ORDER: live when the section it links to is */
		again = false;
		for (uint32_t i = 0; i < isecs.n; i++) {
			ISec *is = &isecs.v[i];

			if (live[i] || is->kind == K_NONE || is->obj == NONE || !(is->flags & 0x80))
				continue;
			const Obj *o = objs.v[is->obj];
			uint32_t l = o->sh[is->shndx].sh_link;

			if (l && l < o->nsh && live[o->isec0 + l]) {
				MARK(i);
				again = true;
			}
		}
	}
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];

		if (live[i] || is->kind == K_NONE || is->obj == NONE)
			continue;
		if (opt.print_gc)
			printf("removing unused section %s:(%s)\n", objs.v[is->obj]->path, is->name);	/* lld: stdout */
		is->kind = K_NONE;
	}
#undef MARK
	free(live);
	free(work);
}

static void assign_sections(void)
{
	ncache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ISec *is = &isecs.v[i];
		if (is->kind == K_NONE)
			continue;
		uint32_t d = match_isd(is->name);
		if (d == NONE) {
			VVEC_PUSH(orphans, i, alloc);
			continue;
		}
		ISD *isd = &isds.v[d];
		if (osecs.v[isd->osec].discard) {
			is->osec = OS_DISCARD;
			continue;
		}
		is->isd = d;
		VVEC_PUSH(isd->secs, i, alloc);
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
		else if (!opt.orphan || is->kind == K_GOT)
			;	/* lld: placed silently by default */
		else
			(opt.orphan == 2 ? error : warn)("%s:(%s) is being placed in '%s'", is->obj != NONE ? objs.v[is->obj]->path : "<internal>",
			     is->name, is->name);
	}
	/* lld sortSections: orphans stable-sorted by rank (non-allocated last), then
	 * placed one by one, each seeing those placed before it */
	{
		uint32_t n = orphans.n, *tmp = alloc(n * sizeof(uint32_t) + 1), k = 0;

		/* stable: by rank, non-allocated last */
		for (uint32_t i = 0; i < n; i++) {
			const ISec *is = &isecs.v[orphans.v[i]];
			uint64_t q = orphan_key(is);
			uint32_t at = k++;

			while (at > 0) {
				const ISec *p = &isecs.v[tmp[at - 1]];

				if (orphan_key(p) <= q)
					break;
				tmp[at] = tmp[at - 1];
				at--;
			}
			tmp[at] = orphans.v[i];
		}
		memcpy(orphans.v, tmp, n * sizeof(uint32_t));
	}
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
			VVEC_PUSH(topcmds, ((Cmd){.kind = C_OSEC, .idx = found}), alloc);
			/* lld findOrphanPos, on the coarse ranks: the first output section of
			 * the closest rank (sections without input sections do not count),
			 * then past those as close whose rank is not above the orphan's */
			int r = orphan_rank(is->flags, is->type, is->name);
			if (r >= 0) {
				uint32_t at = NONE;
				int best = -1;
				for (uint32_t c = 0; c + 1 < topcmds.n; c++)
					if (topcmds.v[c].kind == C_OSEC) {
						int q = osec_rank(topcmds.v[c].idx);
						if (q >= 0 && rank_prox(q, r) > best) {
							best = rank_prox(q, r);
							at = c;
						}
					}
				if (at != NONE) {
					uint32_t c = at;
					for (; c + 1 < topcmds.n; c++) {
						if (topcmds.v[c].kind != C_OSEC)
							continue;
						int q = osec_rank(topcmds.v[c].idx);
						if (q < 0)
							continue;
						if (rank_prox(q, r) != best || r < q)
							break;
					}
					/* lld: back to just past the last output section with input
					 * sections, then past symbol assignments other than '.' */
					if (c + 1 < topcmds.n) {
						uint32_t j = c;
						while (j > 0 && !(topcmds.v[j - 1].kind == C_OSEC && osec_rank(topcmds.v[j - 1].idx) >= 0))
							j--;
						if (j == 0)	/* none before: not ahead of the commands before the first section */
							while (j < c && topcmds.v[j].kind != C_OSEC)
								j++;
						while (j < c && topcmds.v[j].kind == C_ASSIGN && topcmds.v[j].as->sym != NONE)
							j++;
						c = j;
					}
					/* lld: with no section with input sections after, the orphan goes at the very end */
					if (c + 1 < topcmds.n) {
						Cmd cm = topcmds.v[topcmds.n - 1];
						memmove(&topcmds.v[c + 1], &topcmds.v[c], (topcmds.n - 1 - c) * sizeof(Cmd));
						topcmds.v[c] = cm;
					}
				}
			}
		}
		OSec *os = &osecs.v[found];
		uint32_t di = NONE;
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ISD)
				di = os->cmds.v[k].idx;
		if (di == NONE) {
			ISD d = {.osec = found, .thunksec = NONE};
			VVEC_PUSH(isds, d, alloc);
			di = isds.n - 1;
			VVEC_PUSH(osecs.v[found].cmds, ((Cmd){.kind = C_ISD, .idx = di}), alloc);
		}
		is->isd = di;
		VVEC_PUSH(isds.v[di].secs, orphans.v[i], alloc);
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
					VVEC_PUSH(merges, mg, alloc);
					d->secs.v[w++] = syn;
				}
				Merge *mg = &merges.v[m];
				if (is->align > mg->align)
					mg->align = is->align;
				MergeIn mi = {.isec = si, .syn = mg->isec};
				VVEC_PUSH(mergeins, mi, alloc);
				is->aux = mergeins.n - 1;
				VVEC_PUSH(mg->members, mergeins.n - 1, alloc);
			}
			d->secs.n = w;
		}
		for (uint32_t m = first_merge; m < merges.n; m++)
			isecs.v[merges.v[m].isec].align = merges.v[m].align;
	}
	eh_frames();
	if (ehhdr_isec != NONE)	/* lld EhFrameHeader::getSize: 12 + 8 per FDE */
		isecs.v[ehhdr_isec].size = 12 + 8 * (uint64_t)ehfdes.n;
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
					VVEC_PUSH(mg->uniq[shard], tab[x], alloc);
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

#include "../arch/a64/ld/scan.inc"

#include "../arch/a64/ld/thunkpass.inc"

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
	VVEC_PUSH(phdrs, p, alloc);
	VVEC_PUSH(phsecs, ((PhdrSecs){.first = -1, .last = -1}), alloc);
	return phdrs.n - 1;
}

/* arm64 TLS variant 1: tp = the PT_TLS start - alignup(16, p_align); tprel = va - tls_tp */
static uint64_t tls_tp;
static bool has_tls;

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
		if (p->p_type == PT_TLS) {
			uint64_t a = p->p_align ? p->p_align : 1;
			tls_tp = p->p_vaddr - alignup(16, a);
			has_tls = true;
		}
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

/* which locals go to .symtab, remembered (index, name length) for write_locals */
static void count_locals(void *arg, size_t i)
{
	(void)arg;
	static __thread uint32_t (*scratch)[2];
	static __thread uint32_t scap;
	Obj *o = objs.v[i];
	uint32_t n = 0, end = o->firstg < o->nsym ? o->firstg : o->nsym;
	uint64_t str = 0;
	if (o->buf && end > 1) {
		if (scap < end) {
			free(scratch);
			scap = end < 4096 ? 4096 : end;
			scratch = malloc((size_t)scap * sizeof *scratch);
		}
		for (uint32_t s = 1; s < end; s++)
			if (keep_local(o, &o->sym[s])) {
				uint32_t l = (uint32_t)strlen(o->str + o->sym[s].st_name);
				scratch[n][0] = s;
				scratch[n][1] = l;
				n++;
				str += l + 1;
			}
		o->kept = alloc((size_t)n * sizeof *scratch + 1);
		memcpy(o->kept, scratch, (size_t)n * sizeof *scratch);
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
/* ---------------- writing sections ---------------- */

#include "../arch/a64/ld/relocate.inc"

static VVEC(uint32_t) wjobs;		/* ISecs to write */

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
		if (is->ehcie)	/* an FDE's CIE pointer: from the field to its CIE */
			wr32(dst + 4, (uint32_t)(isec_va(si, 4) - isec_va(is->ehcie - 1, 0)));
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
	case K_GOT: {
		Rela *dyn = reladyn_buf ? reladyn_buf + is->dyn0 : NULL;
		uint32_t nd = 0;
		for (uint32_t k = 0; k < gotsyms.n; k++) {
			uint32_t g = gotsyms.v[k];
			bool undef = gsyms[g].state == GS_UNDEF || gsyms[g].demoted;
			uint64_t v = undef ? 0 : gsym_va(g);
			wr64(dst + 8 * k, v);
			if (pic && !undef && !gsym_isabs(g)) {
				dyn[nd].r_offset = isec_va(si, 8 * k);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		for (uint32_t k = 0; k < gotlocs.n; k++) {
			const Obj *o = objs.v[gotlocs.v[k].obj];
			const Sym *y = &o->sym[gotlocs.v[k].sym];
			bool abs = y->st_shndx == SHN_ABS;
			uint64_t v = abs ? y->st_value : isec_va(o->isec0 + y->st_shndx, y->st_value);
			uint64_t at = 8 * ((uint64_t)gotsyms.n + k);

			wr64(dst + at, v);
			if (pic && !abs) {
				dyn[nd].r_offset = isec_va(si, at);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
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

/* .eh_frame_hdr (lld EhFrameHeader::write): version 1, eh_frame_ptr pcrel
 * sdata4, fde_count udata4, a table datarel sdata4 of (initial location,
 * FDE) sorted by location, duplicate locations dropped; read from the
 * relocated FDEs */
static int cmp_u64pair(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] < y[1] ? -1 : x[1] > y[1];	/* lld stable_sort: output order */
}
static void write_ehhdr(void)
{
	if (ehhdr_isec == NONE)
		return;
	uint32_t ho = isec_osec(ehhdr_isec);

	if (ho == NONE || ho == OS_DISCARD)
		return;
	uint64_t hva = isec_va(ehhdr_isec, 0);
	uint8_t *h = out + osecs.v[ho].offset + isecs.v[ehhdr_isec].off;
	uint64_t (*t)[2] = alloc((ehfdes.n + 1) * sizeof *t);
	uint32_t n = 0;
	uint64_t ehva = 0;

	for (uint32_t k = 0; k < ehfdes.n; k++) {
		uint32_t f = ehfdes.v[k], fo = isec_osec(f);
		const uint8_t *b = out + osecs.v[fo].offset + isecs.v[f].off;
		uint64_t fva = isec_va(f, 0);

		if (!k)
			ehva = osecs.v[fo].va;
		t[n][0] = fva + 8 + (uint64_t)(int64_t)(int32_t)rd32(b + 8);	/* pc_begin: pcrel sdata4 (vcc, clang) */
		t[n][1] = fva;
		n++;
	}
	if (!ehfdes.n && isecs.n)
		for (uint32_t i = 0; i < isecs.n; i++)
			if (!strcmp(isecs.v[i].name, ".eh_frame") && isecs.v[i].kind == K_NORMAL && isec_osec(i) < osecs.n) {
				ehva = osecs.v[isec_osec(i)].va;
				break;
			}
	qsort(t, n, sizeof *t, cmp_u64pair);
	uint32_t m = 0;
	for (uint32_t k = 0; k < n; k++)
		if (!m || t[k][0] != t[m - 1][0]) {
			t[m][0] = t[k][0];
			t[m][1] = t[k][1];
			m++;
		}
	h[0] = 1;
	h[1] = 0x1b;	/* DW_EH_PE_pcrel | sdata4 */
	h[2] = 0x03;	/* udata4 */
	h[3] = 0x3b;	/* DW_EH_PE_datarel | sdata4 */
	wr32(h + 4, (uint32_t)(ehva - (hva + 4)));
	wr32(h + 8, m);
	for (uint32_t k = 0; k < m; k++) {
		wr32(h + 12 + 8 * k, (uint32_t)(t[k][0] - hva));
		wr32(h + 16 + 8 * k, (uint32_t)(t[k][1] - hva));
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

/* -lNAME: libNAME.a in the -L directories in order (only static archives: no
 * dynamic linking yet, so as lld under -Bstatic); -l:FILE takes FILE as is */
static const char *find_lib(const char *name)
{
	for (uint32_t k = 0; k < opt.libdirs.n; k++) {
		const char *d = opt.libdirs.v[k];
		size_t n = strlen(d) + strlen(name) + 8;
		char *p = alloc(n);

		if (name[0] == ':')
			snprintf(p, n, "%s/%s", d, name + 1);
		else
			snprintf(p, n, "%s/lib%s.a", d, name);
		if (!faccessat(dirfd_base, p, R_OK, 0))
			return p;
	}
	return NULL;
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
			VVEC_PUSH(opt.inputs, ((InputArg){.path = a, .whole = whole}), alloc);
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
		if (ARG("--eh-frame-hdr")) { opt.eh_frame_hdr = true; continue; }
		if (ARG("--no-eh-frame-hdr")) { opt.eh_frame_hdr = false; continue; }
		if (ARG("-EL") || ARG("--no-undefined-version")) continue;
		if (ARG("--no-undefined") || ARG("-zdefs")) { opt.no_undefined = true; continue; }
		if (ARG("-X") || ARG("--discard-locals")) { opt.discard_locals = true; continue; }
		if (ARG("-x") || ARG("--discard-all")) { opt.discard_all = true; continue; }
		if (ARG("-S") || ARG("--strip-debug")) { opt.strip_debug = true; continue; }
		if (ARG("-n") || ARG("--nmagic")) { opt.nmagic = true; continue; }
		if (ARG("--no-apply-dynamic-relocs")) { opt.no_apply_dyn = true; continue; }
		if (ARG("--apply-dynamic-relocs")) { opt.no_apply_dyn = false; continue; }
		if (ARG("--no-relax")) { opt.relax = false; continue; }
		if (ARG("--gc-sections")) { opt.gc = true; continue; }
		if (ARG("--no-gc-sections")) { opt.gc = false; continue; }
		if (ARG("--print-gc-sections")) { opt.print_gc = true; continue; }
		if (ARG("--no-print-gc-sections")) { opt.print_gc = false; continue; }
		if (ARG("--vroom-stubs")) { opt.stubs = true; continue; }
		if (ARG("--emit-relocs") || ARG("-q")) { error("--emit-relocs is not supported"); continue; }
		if ((v = VAL("-m")) || (!strncmp(a, "-m", 2) && a[2] && (v = a + 2))) {
			/* lld's AArch64 little-endian emulations */
			if (strcmp(v, "aarch64elf") && strcmp(v, "aarch64linux") && strcmp(v, "aarch64_elf64_le_vec"))
				error("unknown emulation: %s", v);
			continue;
		}
		if (ARG("--version") || ARG("-v") || ARG("-V")) {
			/* scripts/ld-version.sh takes the word after "LLD" */
			printf("vld (vroom) LLD 23.1.4 (compatible with GNU linkers)\n");
			if (argc == 2)
				exit(0);
			continue;
		}
		if (ARG("-static") || ARG("-Bstatic") || ARG("--Bstatic") || ARG("-dn") || ARG("-non_shared")) {
			opt.bstatic = true;
			continue;
		}
		if (ARG("-Bdynamic") || ARG("--Bdynamic") || ARG("-dy") || ARG("-call_shared")) { opt.bstatic = false; continue; }
		if (ARG("-no-pie") || ARG("--no-pie")) continue;
		if (ARG("-pie") || ARG("--pie")) { error("-pie is not supported (no dynamic linking yet)"); continue; }
		if ((v = VAL("--dynamic-linker")) || (v = VAL("-dynamic-linker")) || (v = VAL("-I"))) {
			error("--dynamic-linker is not supported (no dynamic linking yet)");
			continue;
		}
		if ((v = VAL("--library-path")) || (v = VAL("-L")) || (!strncmp(a, "-L", 2) && a[2] && (v = a + 2))) {
			VVEC_PUSH(opt.libdirs, v, alloc);
			continue;
		}
		if ((v = VAL("--library")) || (v = VAL("-l")) || (!strncmp(a, "-l", 2) && a[2] && (v = a + 2))) {
			const char *f = find_lib(v);

			if (f)
				VVEC_PUSH(opt.inputs, ((InputArg){.path = f, .whole = whole}), alloc);
			else
				error("unable to find library -l%s", v);
			continue;
		}
		if ((v = VAL("--script")) || (v = VAL("-T"))) { opt.script = v; continue; }
		if (!strncmp(a, "-T", 2) && a[2]) { opt.script = a + 2; continue; }
		if ((v = VAL("--entry")) || (v = VAL("-e"))) { opt.entry = v; continue; }
		if (!strncmp(a, "-e", 2) && a[2]) { opt.entry = a + 2; continue; }
		if ((v = VAL("-soname")) || (v = VAL("--soname")) || (v = VAL("-h"))) { opt.soname = v; continue; }
		if ((v = VAL("--build-id"))) { opt.build_id = strcmp(v, "none") ? v : NULL; continue; }
		if (ARG("--build-id")) { opt.build_id = "fast"; continue; }
		if ((v = VAL("--orphan-handling"))) {
			if (!strcmp(v, "place")) opt.orphan = 0;
			else if (!strcmp(v, "warn")) opt.orphan = 1;
			else if (!strcmp(v, "error")) opt.orphan = 2;
			else error("unknown --orphan-handling mode: %s", v);
			continue;
		}
		if ((v = VAL("--error-limit"))) { error_limit = atoi(v); continue; }
		if ((v = VAL("--threads"))) { nthreads = atoi(v) > 0 ? atoi(v) : 1; continue; }
		if ((v = VAL("-Map")) || (v = VAL("--Map"))) { opt.map = v; continue; }
		if ((v = VAL("--vroom-comment"))) { opt.comment = v; continue; }
		if ((v = VAL("-z")) || (!strncmp(a, "-z", 2) && a[2] && (v = a + 2))) {
			if (!strcmp(v, "notext")) opt.notext = true;
			else if (!strcmp(v, "norelro")) opt.norelro = true;
			else if (!strcmp(v, "relro")) opt.norelro = false;
			else if (!strcmp(v, "max-page-size") || !strncmp(v, "max-page-size=", 14))
				opt.max_page = strtoull(v + 14, NULL, 0);
			else if (strcmp(v, "norelro") && strcmp(v, "noexecstack") && strcmp(v, "relro") &&
				 strcmp(v, "text") && strcmp(v, "now") && strcmp(v, "lazy") && strcmp(v, "nodefaultlib"))
				warn("ignoring -z %s", v);
			continue;
		}
		usage_error(a);
	}
	if (!opt.out)
		opt.out = "a.out";
	return true;
}

/* ---- lazy archives: which members, in which order (as lld) ----
 * lld reads the inputs in order; an object's undefined (non-weak) reference to
 * a symbol a lazy member defines extracts that member at once (depth first,
 * the member is added to the files before its own symbols are read), and a
 * lazy archive's member is extracted when one of its definitions meets an
 * undefined non-weak reference already seen.  Only links with a non-whole
 * archive run this sequential pass. */
enum { LS_NONE, LS_UNDEF, LS_WEAKREF, LS_DEF, LS_LAZY };
typedef struct { const char *name; uint32_t len, h; uint8_t st; uint32_t cand; } LSym;
static struct { LSym *v; uint32_t mask, n; } lsyms;
static PendVec lcand, lout;
static VVEC(uint8_t) ltaken;

static LSym *lsym(const char *name)
{
	uint32_t len = strlen(name), h = strhash(name, len);

	if (!lsyms.v || (lsyms.n + 1) * 2 > lsyms.mask) {
		uint32_t m = lsyms.v ? (lsyms.mask + 1) * 2 : 4096;
		LSym *nv = calloc(m, sizeof(LSym));

		for (uint32_t k = 0; lsyms.v && k <= lsyms.mask; k++)
			if (lsyms.v[k].name) {
				uint32_t j = lsyms.v[k].h & (m - 1);

				while (nv[j].name)
					j = (j + 1) & (m - 1);
				nv[j] = lsyms.v[k];
			}
		free(lsyms.v);
		lsyms.v = nv;
		lsyms.mask = m - 1;
	}
	uint32_t j = h & lsyms.mask;

	for (; lsyms.v[j].name; j = (j + 1) & lsyms.mask)
		if (lsyms.v[j].h == h && lsyms.v[j].len == len && !memcmp(lsyms.v[j].name, name, len))
			return &lsyms.v[j];
	lsyms.n++;
	lsyms.v[j] = (LSym){ .name = name, .len = len, .h = h };
	return &lsyms.v[j];
}

/* the global symbols of a pending object (mapped and aligned here; NULL: not
 * a valid object, left to parse_obj to report) */
static const Sym *lglobals(Pending *p, uint32_t *n, const char **str)
{
	if (!p->buf) {
		p->buf = map_file(p->path, &p->size);
		if (!p->buf)
			return NULL;
	}
	if ((uintptr_t)p->buf & 7) {
		uint8_t *c = alloc(p->size);

		memcpy(c, p->buf, p->size);
		p->buf = c;
	}
	const Ehdr *eh = (const Ehdr *)p->buf;

	if (p->size < sizeof(Ehdr) || memcmp(eh->e_ident, "\177ELF\2\1", 6))
		return NULL;
	Obj o = { .path = p->path, .buf = p->buf, .size = p->size };
	obj_quiet = true;	/* parse_obj reports */
	bool ok = obj_valid(&o, eh);

	obj_quiet = false;
	if (!ok)
		return NULL;
	for (uint32_t s = 0; s < o.nsh; s++)
		if (o.sh[s].sh_type == SHT_SYMTAB) {
			const Sym *y = (const Sym *)(o.buf + o.sh[s].sh_offset);

			*n = o.sh[s].sh_size / sizeof(Sym) - o.sh[s].sh_info;
			*str = (const char *)o.buf + o.sh[o.sh[s].sh_link].sh_offset;
			return y + o.sh[s].sh_info;
		}
	*n = 0;
	return NULL;
}

static void lextract(uint32_t c);

static void lobject(Pending p)
{
	uint32_t n = 0;
	const char *str;
	VVEC_PUSH(lout, p, alloc);
	Pending *q = &lout.v[lout.n - 1];
	const Sym *y = lglobals(q, &n, &str);

	for (uint32_t k = 0; y && k < n; k++) {
		LSym *l = lsym(str + y[k].st_name);
		bool weak = y[k].st_info >> 4 == STB_WEAK;

		if (y[k].st_shndx != SHN_UNDEF)
			l->st = LS_DEF;
		else if (l->st == LS_LAZY) {
			if (!weak)
				lextract(l->cand);
		} else if (l->st == LS_NONE || l->st == LS_WEAKREF)
			l->st = weak ? (l->st == LS_NONE ? LS_WEAKREF : l->st) : LS_UNDEF;
	}
}

static void lextract(uint32_t c)
{
	if (ltaken.v[c])
		return;
	ltaken.v[c] = 1;
	lobject(lcand.v[c]);
}

static void larchive(uint32_t c0, uint32_t c1)
{
	for (uint32_t c = c0; c < c1; c++) {
		uint32_t n = 0;
		const char *str;
		const Sym *y = lglobals(&lcand.v[c], &n, &str);

		for (uint32_t k = 0; y && k < n && !ltaken.v[c]; k++) {
			if (y[k].st_shndx == SHN_UNDEF)
				continue;
			LSym *l = lsym(str + y[k].st_name);

			if (l->st == LS_UNDEF)
				lextract(c);
			else if (l->st == LS_NONE || l->st == LS_WEAKREF) {
				l->st = LS_LAZY;
				l->cand = c;
			}
		}
	}
}

static void load_inputs(void)
{
	bool lazy = false;

	for (uint32_t i = 0; i < opt.inputs.n && !opt.relocatable; i++)
		if (!opt.inputs.v[i].whole) {
			const char *p = opt.inputs.v[i].path;
			size_t l = strlen(p);

			lazy |= l > 2 && !strcmp(p + l - 2, ".a");
		}
	for (uint32_t i = 0; i < opt.inputs.n; i++) {
		const char *p = opt.inputs.v[i].path;
		size_t n;
		uint8_t *b = map_file(p, &n);
		if (!b)
			continue;
		if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8))) {
			if (lazy && !opt.inputs.v[i].whole && !opt.relocatable) {
				uint32_t before = pending.n, c0 = lcand.n;

				add_archive(p, b, n);
				for (uint32_t k = before; k < pending.n; k++) {
					VVEC_PUSH(lcand, pending.v[k], alloc);
					VVEC_PUSH(ltaken, 0, alloc);
				}
				pending.n = before;
				/* an input marker: the archive's members are read here */
				VVEC_PUSH(pending, ((Pending){.path = NULL, .size = c0, .buf = (const uint8_t *)(uintptr_t)lcand.n}), alloc);
				continue;
			}
			add_archive(p, b, n);
			continue;
		}
		VVEC_PUSH(pending, ((Pending){.path = p, .buf = b, .size = n}), alloc);
	}
	if (lazy) {
		for (uint32_t i = 0; i < pending.n; i++)
			if (pending.v[i].path)
				lobject(pending.v[i]);
			else
				larchive(pending.v[i].size, (uint32_t)(uintptr_t)pending.v[i].buf);
		pending = lout;
	}
	for (uint32_t i = 0; i < pending.n; i++) {
		Obj *o = alloc(sizeof *o);
		o->path = pending.v[i].path;
		o->buf = pending.v[i].buf;
		o->size = pending.v[i].size;
		o->idx = objs.n;
		VVEC_PUSH(objs, o, alloc);
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
	hword = bigalloc((size_t)cap * sizeof(*hword));
	gnames = bigalloc((size_t)cap * sizeof(*gnames));
	gsyms = bigalloc((size_t)cap * sizeof(G));
	atomic_store(&ngsyms, 0);
}

static void gtable_free(void)
{
	unmap((void *)hnames, (size_t)(hmask + 1) * sizeof(*hnames));
	unmap((void *)hword, (size_t)(hmask + 1) * sizeof(*hword));
	unmap(gnames, (size_t)(hmask + 1) * sizeof(*gnames));
	unmap(gsyms, (size_t)(hmask + 1) * sizeof(G));
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

static VVEC(const char *) recorded;	/* errors of the current address pass */

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
			VVEC_PUSH(recorded, m, alloc);
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

/* lld: .tbss takes no address space; consecutive ones stack from here */
static uint64_t tbss_off;

/* assignOffsets(); returns true if the section address changed */
static bool assign_offsets(uint32_t oi, bool *symchg)
{
	OSec *os = &osecs.v[oi];
	uint64_t saved = dot;
	bool tbss = (os->flags & (SHF_ALLOC | SHF_TLS)) == (SHF_ALLOC | SHF_TLS) && os->type == SHT_NOBITS;
	if (tbss)
		dot += tbss_off;
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
	else if (tbss) {
		tbss_off = dot - saved;
		dot = saved;
	}
	return changed;
}

static bool assign_addresses(void)
{
	bool changed = false, symchg = false;
	dot = 0;
	tbss_off = 0;
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

/* lld isRelroSection */
static bool is_relro(uint32_t oi)
{
	const OSec *os = &osecs.v[oi];
	const char *n = os->name;

	if (!(os->flags & SHF_ALLOC) || !(os->flags & SHF_WRITE))
		return false;
	if ((os->flags & SHF_TLS) || os->type == SHT_INIT_ARRAY || os->type == SHT_FINI_ARRAY ||
	    os->type == SHT_PREINIT_ARRAY)
		return true;
	if (got_isec != NONE && isec_osec(got_isec) == oi)
		return true;
	return !strcmp(n, ".data.rel.ro") || !strcmp(n, ".bss.rel.ro") || !strcmp(n, ".ctors") ||
	       !strcmp(n, ".dtors") || !strcmp(n, ".jcr") || !strcmp(n, ".eh_frame") ||
	       !strcmp(n, ".fini_array") || !strcmp(n, ".init_array") || !strcmp(n, ".preinit_array");
}

static void create_phdrs(void)
{
	phdrs.n = phsecs.n = 0;
	phnew(PT_PHDR, PF_R);
	int hdrload = phnew(PT_LOAD, PF_R);
	int load = hdrload;
	uint32_t flags = PF_R;
	/* lld relroEnd: the first section after the relro run starts a PT_LOAD */
	uint32_t relro_end = NONE;

	for (uint32_t i = 0, in = 0; i < outsecs.n && !opt.norelro && !opt.relocatable && relro_end == NONE; i++) {
		if (!sec_in_load(&osecs.v[outsecs.v[i]]))
			continue;
		if (is_relro(outsecs.v[i]))
			in = 1;
		else if (in)
			relro_end = outsecs.v[i];
	}
	for (uint32_t i = 0; i < outsecs.n; i++) {
		uint32_t oi = outsecs.v[i];
		OSec *os = &osecs.v[oi];
		os->ptload = -1;
		if (!sec_in_load(os))
			continue;
		uint32_t nf = phflags(os);
		if ((flags ^ nf) || oi == relro_end)
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
	/* lld: one PT_TLS over the SHF_TLS sections, after the PT_LOADs */
	{
		int tls = -1;
		for (uint32_t i = 0; i < outsecs.n; i++) {
			uint32_t oi = outsecs.v[i];
			if ((osecs.v[oi].flags & (SHF_ALLOC | SHF_TLS)) != (SHF_ALLOC | SHF_TLS))
				continue;
			if (tls < 0)
				tls = phnew(PT_TLS, PF_R);
			phadd(tls, oi);
		}
	}
	/* lld: one PT_GNU_RELRO over the contiguous relro sections */
	if (!opt.norelro && !opt.relocatable) {
		int rr = -1;
		bool in = false, ended = false;

		for (uint32_t i = 0; i < outsecs.n; i++) {
			uint32_t oi = outsecs.v[i];

			if (!sec_in_load(&osecs.v[oi]))
				continue;
			if (is_relro(oi)) {
				in = true;
				if (ended)
					error("section: %s is not contiguous with other relro sections", osecs.v[oi].name);
				else {
					if (rr < 0)
						rr = phnew(PT_GNU_RELRO, PF_R);
					phadd(rr, oi);
				}
			} else if (in) {
				in = false;
				ended = true;
			}
		}
		if (rr >= 0)
			phdrs.v[rr].p_align = 1;
	}
	if (ehhdr_isec != NONE && isec_osec(ehhdr_isec) != NONE && isec_osec(ehhdr_isec) != OS_DISCARD)
		phadd(phnew(PT_GNU_EH_FRAME, PF_R), isec_osec(ehhdr_isec));
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
static uint8_t *symtab_buf, *strtab_buf;	/* in the output image (fill_symtab) */
static uint64_t thunk_sym0, thunk_str0;		/* first thunk symbol and its string */
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
	for (uint32_t k = 0; k < o->nloc; k++) {
		const Sym *y = &o->sym[o->kept[k][0]];
		uint32_t l = o->kept[k][1];
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
		memcpy(strtab_buf + so, o->str + y->st_name, l + 1);
		d->st_name = (uint32_t)so;
		so += l + 1;
		d->st_info = y->st_info;
		d->st_other = y->st_other;
		d->st_shndx = shndx;
		d->st_value = val;
		d->st_size = y->st_size;
		d++;
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
	thunk_sym0 = thunk0;
	thunk_str0 = gstr;
	(void)nhid;
}

/* .symtab and .strtab are written in place in the output image */
static void fill_symtab(void)
{
	symtab_buf = out + osecs.v[isec_osec(symtab_isec)].offset + isecs.v[symtab_isec].off;
	strtab_buf = out + osecs.v[isec_osec(strtab_isec)].offset + isecs.v[strtab_isec].off;
	uint64_t so = thunk_str0;
	for (uint32_t t = 0; t < thunks.n; t++) {
		Thunk *th = &thunks.v[t];
		Sym *d = (Sym *)(void *)symtab_buf + thunk_sym0 + 2 * t;
		uint16_t shndx = sym_shndx_isec(th->tsec);
		put_sym(d, strtab_buf, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, thunk_va(t), th->size);
		put_sym(d + 1, strtab_buf, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, thunk_va(t), 0);
	}
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
	/* lld: -e, else ENTRY(), else _start (silently absent) */
	const char *en = opt.entry ? opt.entry : entry_name ? entry_name : "_start";
	uint32_t eg = gfind(en);
	if (!opt.relocatable && (opt.entry || entry_name) && (eg == NONE || gsyms[eg].state == GS_UNDEF))
		warn("cannot find entry symbol %s; not setting start address", en);
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
	/* .rela.dyn, .symtab and .strtab were written in place */
	ISec *is = &isecs.v[shstrtab_isec];
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

static void reset_state(void);

static _Atomic bool got_seen;
static void find_got(void *arg, size_t i)
{
	(void)arg;
	const ISec *is = &isecs.v[i];
	if (is->kind == K_NONE || atomic_load_explicit(&got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->nrela; r++) {
		uint32_t t = (uint32_t)is->rela[r].r_info;
		if (t == R_AARCH64_ADR_GOT_PAGE || t == R_AARCH64_LD64_GOT_LO12_NC) {
			atomic_store(&got_seen, true);
			return;
		}
	}
}
static bool any_got(void)
{
	atomic_store(&got_seen, false);
	if (!opt.relocatable)
		pfor(isecs.n, find_got, NULL);
	return atomic_load(&got_seen);
}

/* lld replaceCommonSymbols: each common symbol one NOBITS "COMMON" section
 * (after every file's sections), of the largest size and alignment among its
 * commons, in the first file with that size; placed in the order of the files
 * that have commons, each file's globals in order, at the first mention */
static void make_commons(void)
{
	bool any = false;

	for (uint32_t g = 0; g < atomic_load(&ngsyms) && !any; g++)
		any = gsyms[g].common && gsyms[g].state == GS_OBJ;
	if (!any)
		return;
	for (uint32_t g = 0; g < atomic_load(&ngsyms); g++)
		if (gsyms[g].common && gsyms[g].state == GS_OBJ)
			gsyms[g].size = 0, gsyms[g].val = 1, gsyms[g].obj = NONE;
	for (uint32_t i = 0; i < nobj_real; i++) {
		Obj *o = objs.v[i];

		for (uint32_t k = o->buf ? o->firstg : 0; o->buf && k < o->nsym; k++) {
			const Sym *y = &o->sym[k];
			G *s = &gsyms[o->gs[k - o->firstg]];

			if (y->st_shndx != SHN_COMMON || !s->common || s->state != GS_OBJ)
				continue;
			if (y->st_value > s->val)
				s->val = y->st_value;	/* the alignment, until placed */
			if (s->obj == NONE || y->st_size > s->size)
				s->obj = i, s->symidx = k, s->size = y->st_size;
		}
	}
	for (uint32_t i = 0; i < nobj_real; i++) {
		Obj *o = objs.v[i];
		bool has = false;

		for (uint32_t k = o->buf ? o->firstg : 0; o->buf && k < o->nsym && !has; k++)
			has = o->sym[k].st_shndx == SHN_COMMON;
		/* every global the file names, references too, in its order */
		for (uint32_t k = o->firstg; has && k < o->nsym; k++) {
			G *s = &gsyms[o->gs[k - o->firstg]];

			if (!s->common || s->state != GS_OBJ || s->sec != NONE)
				continue;
			uint32_t is = new_isec("COMMON", K_NORMAL, SHT_NOBITS, SHF_ALLOC | SHF_WRITE,
					       s->val ? (uint32_t)s->val : 1);

			isecs.v[is].size = s->size;
			isecs.v[is].obj = s->obj;
			s->sec = is;
			s->val = 0;
		}
	}
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
	/* lld: .eh_frame_hdr when there is an .eh_frame (sized by eh_frames) */
	for (uint32_t i = 0; opt.eh_frame_hdr && !opt.relocatable && ehhdr_isec == NONE && i < isecs.n; i++)
		if (eh_input(&isecs.v[i]))
			ehhdr_isec = new_isec(".eh_frame_hdr", K_EHHDR, SHT_PROGBITS, SHF_ALLOC, 4);
	/* only when some GOT relocation needs it (lld drops an empty .got); sized after the scan */
	if (any_got())
		got_isec = new_isec(".got", K_GOT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
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
static VVEC(uint32_t) script_new;	/* slots first inserted by the script, in order */

static void script_insert(uint32_t g)
{
	if (!g_referenced(g))
		VVEC_PUSH(script_new, g, alloc);
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
	VVEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < provides.n; i++) {
		uint32_t g = provides.v[i].sym;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = provides.v[k].sym == g;
		if (!dup && g_referenced(g) && !g_defined_obj(g)) {
			added[i] = 1;
			VVEC_PUSH(stack, i, alloc);
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
					VVEC_PUSH(stack, j, alloc);
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
	VVEC(const char *) names = {0};
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
			VVEC_PUSH(names, gname(g), alloc);
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
				VVEC_PUSH(gorder, g, alloc);
		}
	}
	for (uint32_t i = 0; i < script_new.n; i++)
		VVEC_PUSH(gorder, script_new.v[i], alloc);
}

/* lld's getAlternativeSpelling: a defined global (or a local of the referencing object) one edit
   away, tried in lld's order: substitute, transpose, insert, delete (characters '0'..'z') */
static const char *suggest_one(const char *cand, Obj *ref, const char **where)
{
	uint32_t g = gfind(cand);

	if (g != NONE && gsyms[g].state == GS_OBJ && gsyms[g].obj != NONE) {
		*where = objs.v[gsyms[g].obj]->path;
		return gname(g);
	}
	(void)ref;	/* (lld suggested no local of the referencing object in our tests) */
	return NULL;
}

static const char *suggest(const char *name, Obj *ref, const char **where)
{
	size_t n = strlen(name);
	char *b = malloc(n + 2);
	const char *r = NULL;

	if (!b)
		return NULL;
	memcpy(b, name, n + 1);
	for (size_t i = 0; i < n && !r; i++) {
		char old = b[i];

		for (char c = '0'; c <= 'z' && !r; c++) {
			b[i] = c;
			r = suggest_one(b, ref, where);
		}
		b[i] = old;
	}
	for (size_t i = 0; i + 1 < n && !r; i++) {
		char t = b[i];

		b[i] = b[i + 1], b[i + 1] = t;
		r = suggest_one(b, ref, where);
		b[i + 1] = b[i], b[i] = t;
	}
	b[0] = '0';
	memcpy(b + 1, name, n + 1);
	for (size_t i = 0; i <= n && !r; i++) {
		for (char c = '0'; c <= 'z' && !r; c++) {
			b[i] = c;
			r = suggest_one(b, ref, where);
		}
		if (i < n) {	/* (lld: std::swap(newName[i], newName[i + 1])) */
			char t = b[i];

			b[i] = b[i + 1];
			b[i + 1] = t;
		}
	}
	for (size_t i = 0; i < n && !r; i++) {
		memcpy(b, name, i);
		memcpy(b + i, name + i + 1, n - i);
		r = suggest_one(b, ref, where);
	}
	free(b);
	return r;
}

/* lld's location of a reference: "SRC.c\n>>>               OBJ:(FUNC)" (the object's STT_FILE, the
   defined symbol around the offset, else SECTION+0xOFF; an archive member "M.o:(F) in archive A") */
static void ref_loc(vbuf *b, Obj *o, uint32_t sec, uint64_t off)
{
	const char *src = NULL, *path = o->path, *lp = strrchr(path, '('), *fn = NULL;
	char obj[4096];

	for (uint32_t k = 1; k < o->firstg && !src; k++)
		if ((o->sym[k].st_info & 15) == STT_FILE)
			src = o->str + o->sym[k].st_name;
	for (uint32_t k = 1; k < o->nsym && !fn; k++) {
		const Sym *y = &o->sym[k];
		uint8_t t = y->st_info & 15;

		if (y->st_shndx == sec && t != STT_SECTION && t != STT_FILE && o->str[y->st_name] &&
		    o->str[y->st_name] != '$' && y->st_value <= off && off < y->st_value + y->st_size)
			fn = o->str + y->st_name;
	}
	if (lp && path[strlen(path) - 1] == ')')	/* "A(M)" */
		snprintf(obj, sizeof obj, "%.*s", (int)(strlen(lp + 1) - 1), lp + 1);
	else
		snprintf(obj, sizeof obj, "%s", path);
	if (src)
		vbuf_f(b, "%s\n>>>               ", src);
	if (fn)
		vbuf_f(b, "%s:(%s)", obj, fn);
	else
		vbuf_f(b, "%s:(%s+0x%llx)", obj, o->shstr + o->sh[sec].sh_name, (unsigned long long)off);
	if (lp && path[strlen(path) - 1] == ')')
		vbuf_f(b, " in archive %.*s", (int)(lp - path), path);
}

/* "undefined symbol: X" and every reference to it (lld: the first 3, then "referenced N more times") */
static void undef_refs(uint32_t gid, Obj *first, bool spell)
{
	vbuf b = {0};
	uint32_t nref = 0;
	Obj *reffile = NULL;

	vbuf_f(&b, "undefined symbol: %s", gname(gid));
	for (uint32_t oi = 0; oi < objs.n; oi++) {
		Obj *o = objs.v[oi];

		for (uint32_t k = 1; k < o->nsh; k++) {
			const Shdr *h = &o->sh[k];

			if (h->sh_type != SHT_RELA || h->sh_info >= o->nsh || !(o->sh[h->sh_info].sh_flags & SHF_ALLOC))
				continue;
			const Rela *r = (const Rela *)(o->buf + h->sh_offset);
			for (uint64_t j = 0; j < h->sh_size / sizeof(Rela); j++) {
				uint32_t si = (uint32_t)(r[j].r_info >> 32);

				if (si < o->firstg || si >= o->nsym || o->gs[si - o->firstg] != gid)
					continue;
				if (!reffile)
					reffile = o;
				if (nref++ < 3) {
					vbuf_f(&b, "\n>>> referenced by ");
					ref_loc(&b, o, h->sh_info, r[j].r_offset);
				}
			}
		}
	}
	if (!nref)
		vbuf_f(&b, "\n>>> referenced by %s", first->path);
	else if (nref > 3)
		vbuf_f(&b, "\n>>> referenced %u more times", nref - 3);
	{
		const char *where = NULL, *alt = spell ? suggest(gname(gid), reffile, &where) : NULL;

		if (alt)
			vbuf_f(&b, "\n>>> did you mean: %s\n>>> defined in: %s", alt, where);
	}
	vbuf_put(&b, "", 1);
	error("%s", (const char *)b.p);
	free(b.p);
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
			undef_refs(gorder.v[i], objs.v[kget(&s->first) >> 32], n < 2);	/* (lld: spelling for the first two) */
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

static VVEC(uint32_t) stop_syms;	/* __stop_X: the section size, after layout */

/* lld addStartStopSymbols: a referenced, undefined __start_X/__stop_X is the
 * start/end of output section X (X a C identifier), protected, global */
static void start_stop_symbols(void)
{
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		const char *n = gname(gorder.v[i]), *x;

		if (s->state != GS_UNDEF || !(x = start_stop_name(n)))
			continue;
		for (uint32_t k = 0; k < topcmds.n; k++) {
			if (topcmds.v[k].kind != C_OSEC || strcmp(osecs.v[topcmds.v[k].idx].name, x) ||
			    osecs.v[topcmds.v[k].idx].discard)
				continue;
			s->state = GS_SCRIPT;
			s->ev = (EV){.kind = EV_OSEC, .idx = topcmds.v[k].idx, .align = 1};
			s->vis = STV_PROTECTED;
			s->bind = STB_GLOBAL;
			s->type = STT_NOTYPE;
			if (n[2] == 's' && n[3] == 't' && n[4] == 'o')
				VVEC_PUSH(stop_syms, gorder.v[i], alloc);
			break;
		}
	}
}

static int ld_relocatable(void);

static int run(int argc, char **argv)
{
	reset_state();
	{
		int n = 0;
		size_t len = sizeof n;
#ifdef __APPLE__
		if (!sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, NULL, 0) && n > 0)
			nthreads = n;
#else
		(void)len;
		if ((n = (int)sysconf(_SC_NPROCESSORS_ONLN)) > 0)
			nthreads = n;
#endif
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
	p_last = vperf_now();
	flt_last = timing ? minflt() : 0;
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
	dedup_comdats();
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
			VVEC_PUSH(objs, o, alloc);
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

	if (opt.gc && !opt.relocatable)
		gc_sections(opt.entry ? opt.entry : entry_name ? entry_name : "_start");
	make_commons();
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
	start_stop_symbols();
	pfor(gorder.n, demote_symbols, NULL);
	report_undefined();
	phase("assign sections");
	finalize_merges();
	phase("merge sections");
	if (atomic_load(&nerrors))
		return 1;

	process_symbol_assignments();
	adjust_output_sections();
	if (got_isec != NONE)
		for (uint32_t i = 0; i < objs.n; i++)
			if (objs.v[i]->buf && objs.v[i]->firstg)
				objs.v[i]->lgot = calloc(objs.v[i]->firstg, 1);
	pfor(isecs.n, scan_relocs, NULL);
	/* .got: one slot per symbol, globals in symbol id order, then the locals by
	 * file (lld postScanRelocations) */
	for (uint32_t g = 0, ng = atomic_load(&ngsyms); g < ng; g++)
		if (atomic_load(&gsyms[g].needgot)) {
			gsyms[g].gotidx = gotsyms.n;
			VVEC_PUSH(gotsyms, g, alloc);
			if (pic && got_isec != NONE && !gsym_isabs(g) && gsyms[g].state != GS_UNDEF && !gsyms[g].demoted)
				isecs.v[got_isec].ndyn++;
		}
	for (uint32_t i = 0; got_isec != NONE && i < objs.n; i++) {
		Obj *o = objs.v[i];

		for (uint32_t k = 0; o->lgot && k < o->firstg; k++)
			if (o->lgot[k]) {
				if (!o->lgotidx)
					o->lgotidx = alloc(o->firstg * sizeof(uint32_t));
				o->lgotidx[k] = gotsyms.n + gotlocs.n;
				VVEC_PUSH(gotlocs, ((GotLoc){i, k}), alloc);
				if (pic && o->sym[k].st_shndx != SHN_ABS)
					isecs.v[got_isec].ndyn++;
			}
	}
	if (got_isec != NONE)
		isecs.v[got_isec].size = (uint64_t)(gotsyms.n + gotlocs.n) * 8;
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
			VVEC_PUSH(outsecs, topcmds.v[c].idx, alloc);
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
	for (uint32_t i = 0; i < stop_syms.n; i++)
		gsyms[stop_syms.v[i]].ev.val = osecs.v[gsyms[stop_syms.v[i]].ev.idx].size;
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
		VVEC(uint32_t) dl = {0};
		for (uint32_t i = 0; i < isecs.n; i++)
			if (isecs.v[i].ndyn)
				VVEC_PUSH(dl, i, alloc);
		qsort(dl.v, dl.n, sizeof(uint32_t), cmp_dyn);
		uint32_t at = 0;
		for (uint32_t i = 0; i < dl.n; i++) {
			isecs.v[dl.v[i]].dyn0 = at;
			at += isecs.v[dl.v[i]].ndyn;
		}
		/* built in place in the output image */
		reladyn_buf = (Rela *)(void *)(out + osecs.v[isec_osec(reladyn_isec)].offset +
					       isecs.v[reladyn_isec].off);
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
					VVEC_PUSH(wjobs, d->secs.v[s], alloc);
			}
	}
	final_addresses();
	pfor(wjobs.n, write_isec, NULL);
	write_ehhdr();
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
	unmap(out, filesize);
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			unmap(objs.v[i]->buf, objs.v[i]->size);
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
	VVEC(uint32_t) secs;
	uint64_t nrela, relaoff;
	uint32_t shidx, relaidx, nameoff, relanameoff;
	uint32_t secsym;
	uint32_t grp;			/* a .group: its input SHT_GROUP ISec + 1 */
	bool member;			/* a group member: its own output section, SHF_GROUP */
} ROut;
static VVEC(ROut) routs;
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
	VVEC_PUSH(routs, r, alloc);
	rmap[j] = routs.n - 1;
	return routs.n - 1;
}

/* an output section of its own (groups and their members, as lld -r) */
static uint32_t rout_new(const char *name)
{
	ROut r = {.name = name, .h = strhash(name, strlen(name)), .align = 1};
	VVEC_PUSH(routs, r, alloc);
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
			} else if (isecs.v[s->sec].kind == K_NONE) {
				size = 0;	/* in a dropped group member: undefined (lld) */
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
		const Obj *io = is->obj != NONE ? objs.v[is->obj] : NULL;
		const Shdr *ih = io && is->shndx < io->nsh ? &io->sh[is->shndx] : NULL;

		if (ih && ih->sh_type == SHT_GROUP) {
			/* a group whose members are kept (a later COMDAT copy is dropped) */
			const uint32_t *w = (const uint32_t *)(io->buf + ih->sh_offset);
			uint32_t nw = (uint32_t)(ih->sh_size / 4);

			if (nw < 2 || isecs.v[io->isec0 + w[1]].dropped)
				continue;
			uint32_t gi = rout_new(".group");
			ROut *g = &routs.v[gi];

			g->type = SHT_GROUP;
			g->align = 4;
			g->entsize = 4;
			g->grp = i + 1;
			g->size = 4 * (uint64_t)nw;
			is->osec = gi;
			continue;
		}
		if (is->kind != K_NORMAL)
			continue;
		bool member = ih && (ih->sh_flags & SHF_GROUP);
		uint32_t ri = member ? rout_new(is->name) : rout_get(is->name);
		ROut *r = &routs.v[ri];

		if (member) {
			r->member = true;
			is->flags |= SHF_GROUP;
		}
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
		VVEC_PUSH(r->secs, i, alloc);
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
			VVEC_PUSH(wjobs, routs.v[i].secs.v[k], alloc);
	/* local maps must exist before relocations are rewritten */
	pfor(objs.n, r_write_locals, NULL);
	pfor(wjobs.n, r_write_isec, NULL);
	pfor((gorder.n + 4095) / 4096, r_write_globals, NULL);
	/* groups: the flag word, then the members' output indices (a .rela member: its section's) */
	for (uint32_t i = 0; i < routs.n; i++) {
		ROut *g = &routs.v[i];

		if (!g->grp)
			continue;
		const ISec *gs = &isecs.v[g->grp - 1];
		const Obj *o = objs.v[gs->obj];
		const Shdr *h = &o->sh[gs->shndx];
		const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);
		uint32_t *d = (uint32_t *)(void *)(rbuf + g->off), n = 0;

		d[n++] = w[0];
		for (uint64_t k = 1; k < h->sh_size / 4; k++) {
			const Shdr *m = &o->sh[w[k]];
			const ISec *mi = &isecs.v[o->isec0 + (m->sh_type == SHT_RELA ? m->sh_info : w[k])];

			if (mi->kind == K_NONE || mi->osec == NONE)
				continue;
			uint32_t x = m->sh_type == SHT_RELA ? routs.v[mi->osec].relaidx : routs.v[mi->osec].shidx;

			if (x)
				d[n++] = x;
		}
		g->size = 4 * (uint64_t)n;
	}
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
		if (r->grp) {	/* sh_link the symbol table, sh_info the signature */
			const ISec *gs = &isecs.v[r->grp - 1];
			const Obj *o = objs.v[gs->obj];
			uint32_t y = o->sh[gs->shndx].sh_info;

			shd[r->shidx].sh_size = r->size;
			shd[r->shidx].sh_link = r_symidx;
			shd[r->shidx].sh_info = y >= o->firstg ? gsyms[o->gs[y - o->firstg]].outidx : lmap[gs->obj][y];
		}
		if (r->nrela) {
			sprintf(shstr + r->relanameoff, ".rela%s", r->name);
			shd[r->relaidx] = (Shdr){.sh_name = r->relanameoff, .sh_type = SHT_RELA,
						 .sh_flags = SHF_INFO_LINK | (r->member ? SHF_GROUP : 0),
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
	unmap(rbuf, fsize);
	out = NULL;
	for (uint32_t i = 0; i < objs.n; i++)
		if (objs.v[i]->mapped && objs.v[i]->buf)
			unmap(objs.v[i]->buf, objs.v[i]->size);
	gtable_free();
	memset(&routs, 0, sizeof routs);
	return rc || atomic_load(&nerrors) ? 1 : 0;
}

/* all per-link state, so ld_main can be called again (vccd, VLD_REPEAT) */
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
	memset(&script_new, 0, sizeof script_new);
	memset(&routs, 0, sizeof routs);
	stub_names = NULL;
	nstubs = 0;
	tr_keys = NULL;
	tr_vals = NULL;
	tr_mask = tr_n = 0;
	entry_name = NULL;
	reladyn_isec = buildid_isec = comment_isec = got_isec = NONE;
	gotsyms.n = 0;
	gotlocs.n = 0;
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

static pthread_mutex_t ldlock = PTHREAD_MUTEX_INITIALIZER;

int ld_main(int argc, char **argv, int dirfd, FILE *err)
{
	VLOCK(ldlock);
	errf = err ? err : stderr;
	dirfd_base = dirfd;
	int rc = run(argc, argv);
	free_arenas();
	return rc;
}

#ifdef VLD_MAIN
int wasm_ld_main(int argc, char **argv);

int main(int argc, char **argv)
{
	const char *b = strrchr(argv[0], '/');
	b = b ? b + 1 : argv[0];
	/* WebAssembly (wasmld.c): as wasm-ld, or for -mwasm64 */
	if (strstr(b, "wasm-ld"))
		return wasm_ld_main(argc, argv);
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-mwasm64"))
			return wasm_ld_main(argc, argv);
	if (strstr(b, "objcopy"))
		return objcopy_main(argc, argv, AT_FDCWD, stderr);
	if (strstr(b, "nm") && !strstr(b, "link"))
		return nm_main(argc, argv, AT_FDCWD, stdout, stderr);
	/* VLD_REPEAT=N: link N times in one process (for profilers) */
	const char *rep = getenv("VLD_REPEAT");
	for (int i = rep ? atoi(rep) : 1; i > 1; i--)
		if (ld_main(argc, argv, AT_FDCWD, stderr))
			return 1;
	keep_maps = true;
	return ld_main(argc, argv, AT_FDCWD, stderr);
}
#endif
