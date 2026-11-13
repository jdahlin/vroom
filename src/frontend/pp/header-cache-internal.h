#ifndef VCC_PP_HEADER_CACHE_INTERNAL_H
#define VCC_PP_HEADER_CACHE_INTERNAL_H
/* the header cache's types: shared by the header-cache-*.c files, included by ppimpl.h (struct pp holds some of them) */
/* ---------------- header cache: types ---------------- */

/*
 * The header cache (step 2.4; -E text only for now).  Every #include is a
 * region.  While a region runs for the first time its inputs are recorded
 * exactly:
 *
 *   - every macro table entry it reads before writing it: the value at
 *     region entry, or only whether it was defined (#ifdef, defined(),
 *     include guards).  Definitions are shared pointers (see pmacro), so
 *     "same definition" is pointer equality;
 *   - #pragma once membership of the files it would enter;
 *   - names pasted from __COUNTER__ that it looks up (must be undefined);
 *
 * and its effects: the final value of every macro it wrote, the files it
 * marked once, the files it entered (dependencies, locations), the counter
 * delta, and its output text.  Uses of __COUNTER__ are relative: digit runs
 * are tracked through paste and stringify (ptok.ctr) and rewritten on
 * replay.  A region's text is a rope: its own text with the entries of the
 * nested regions it contains spliced in, so each header's text is stored
 * once however deep it is included.
 *
 * Regions nest; recording keeps one level per open region.  A read is
 * recorded in the innermost level; a level's records are merged into its
 * parent when it ends (and a replayed entry's into the level it is
 * replayed in).  A later TU with the same search path signature reaching
 * the same #include (file, search dir, spelling, nesting level, system
 * header or not) replays an entry whose inputs all match the current state
 * instead of running the header.  Regions that print diagnostics, change
 * #line state, use push/pop_macro, __DATE__/__TIME__/__TIMESTAMP__/
 * __BASE_FILE__ or __COUNTER__ in a directive are not cached.  Entries are
 * immutable and shared by all threads.
 */
#define HDEF ((uintptr_t)1)	/* read: only "defined" was read, and it was */
#define HNONE 0xffffffffu
#define HC_MAXLEV 64

typedef struct hread {
	uint32_t id, _pad;
	uintptr_t v;		/* value at entry (enabled), or HDEF */
} hread;

typedef struct honce {
	const vlex_file *f;
	uint32_t present;
} honce;

typedef struct hfent {
	const vlex_file *f;
	const pfile *fi;
	const char *name, *spath;
	uint32_t namelen, rbase;
	uint8_t sys;
} hfent;

/* __COUNTER__ digits: in a token's spelling (cprov) or in an entry's own text (hctr) */
typedef struct cprov {
	uint32_t val;
	uint32_t off;
	uint16_t len, next;	/* next run in the same token (a chain, increasing off) */
} cprov;

typedef struct hctr {
	uint32_t off, len, rel;	/* own-text offset; value - counter at entry */
} hctr;	/* tokens: off = own-token index, len = byte offset << 16 | length */

/* a lookup of an identifier pasted from __COUNTER__ (it was undefined) */
typedef struct hcread {
	const char *s;
	uint32_t len, off, dlen, rel;
} hcread;

typedef struct hent hent;

typedef struct hkid {
	const hent *e;
	const uint32_t *ex;
	uint32_t nex;
} hkid;

/*
 * A stored token (12 bytes; ptok is 24).  T_IDENT: sp is the id and the
 * spelling the id's.  Others: sp - 1 is the spelling's offset / 16 in the
 * shared region (hc_blob: stored once).  HF_X: sp indexes the entry's
 * escapes (full ptoks: spliced identifiers, long spellings, odd ids).
 */
typedef struct htok {
	uint32_t sp;
	uint32_t loc;		/* offset from the base + 1, 0: none */
	uint8_t kind, flags;
	uint16_t len;
} htok;
#define HF_X 128

static inline void htok_get(ptok *t, const htok *h, const ptok *xtk, const char *sbase, uint32_t base)
{
	if (__builtin_expect(h->flags & HF_X, 0)) {
		*t = xtk[h->sp];
	} else {
		t->kind = h->kind;
		t->flags = h->flags;
		t->len = h->len;
		t->ctr = 0;
		if (h->kind == T_IDENT) {
			t->id = h->sp;
			t->s = vintern_ents[h->sp].s;
		} else {
			t->id = 0;
			t->s = sbase + ((size_t)(h->sp - 1) << 4);
		}
	}
	t->loc = h->loc ? base + h->loc - 1 : 0;
}

typedef struct hseg {
	uint32_t at;		/* own-text offset (tokens: own-token index) the child goes before */
	uint32_t cdelta;	/* child's counter at entry - ours */
	const hent *child;
	uint32_t lbase;		/* tokens: child's location base - ours */
} hseg;

/*
 * Arrays of reads and writes are stored as chunks deduplicated across all
 * entries (an enclosing header's arrays repeat its children's runs).  The
 * cuts are content defined (by id), so the same run of ids cuts the same
 * way wherever it occurs.
 */
typedef struct hck {
	uint64_t h;
	uint32_t n, esz;	/* elements, element size (1: a blob of n bytes) */
	_Alignas(16) char d[];
} hck;

typedef struct hvec {
	const hck *const *c;
	uint32_t nc, n;		/* chunks, elements */
} hvec;

/* for (const T *x ...) over the elements of v; break leaves only the inner loop */
#define HV_FOR(v, T, x) \
	for (uint32_t hvi_ = 0; hvi_ < (v).nc; hvi_++) \
		for (const T *x = (const T *)(const void *)(v).c[hvi_]->d, *x##_e = x + (v).c[hvi_]->n; x < x##_e; x++)

/*
 * An include lookup an entry made itself that depended on the search
 * directories (used > 0).  The entry's ndirs/psig say the directories it
 * depended on were the same; where they differ, the entry still holds when
 * each such lookup gives the same answer here (hc_looks_ok): PostgreSQL
 * puts a per-target directory first, which every lookup passes through.
 */
typedef struct hlook {
	const char *name;	/* as written */
	const char *from;	/* the includer's spath (NULL: no includer) */
	const char *path;	/* the answer's spelling (NULL: not found) */
	const vlex_file *f;
	int32_t from_sdir, sdir;
	uint32_t nlen, from_sdl;
	uint8_t angled, next, from_bi, sys;
} hlook;

struct hent {
	hent *next;
	hvec rg;		/* inputs: include guards (v: HDEF or 0); checked first */
	hvec ru;		/* inputs: ids read while undefined (uint32_t) */
	hvec rv;		/* inputs: other reads */
	hvec dl;		/* delta: (id, new value) */
	const honce *ord;
	const vlex_file *const *oadd;
	const hfent *fe;
	const char *text;	/* own text */
	const htok *tk;		/* token entries: own tokens */
	const ptok *xtk;	/* their escapes (HF_X) */
	const hctr *ctr;
	const hcread *crd;
	const hseg *seg;
	/*
	 * Child entries in order: their reads are this entry's inputs except
	 * the ids this entry wrote before the child (ex, sorted), and their
	 * deltas apply before this entry's own (dl: the ids whose last write was
	 * this entry's own).  Reads and writes are stored once, in the entry
	 * that made them.
	 */
	const struct hkid *kid;
	uint32_t nkid;
	const hlook *lk;	/* its own lookups that depended on the search directories */
	uint32_t nlk;
	size_t ntext, total;	/* own text (tokens); with the children */
	int32_t level;		/* __INCLUDE_LEVEL__ used: the include depth it ran at (-1: not used) */
	uint32_t ndirs, psig;	/* leading search directories its lookups depended on; their id */
	const uint32_t *br;	/* token entries: outermost '{' '}' pairs of the own tokens with no child
				   inside, (open, close) own-token indices, by open (pp_lazy_skip) */
	uint32_t nbr;
	uint32_t nord, noadd, nfe, nctr, ncrd, nseg, span, counter_delta;
	uint8_t has_text, first_sol, col_end, pend_out;
	uint8_t edir_end;
	uint8_t pk, pk_L, pk_pp;	/* the printer's last token at the end (pp.pk...) */
};

typedef struct hkey {
	uint64_t hash;
	const vlex_file *f;
	const char *spath;
	uint32_t sig, nfile;
	int32_t sdir;
	uint8_t sys;
	_Atomic(hent *) head;
	_Atomic uint32_t n;
} hkey;

/* recording, per TU */
enum { HT_R = 1, HT_W = 2, HT_G = 4 /* the read was an include guard check */,
       HT_O = 8 /* the last write was the level's own (not a child entry's) */ };

typedef struct htouch {	/* a level's first touch of an id */
	uint32_t id, flags;	/* HT_* */
	uintptr_t v;		/* HT_R: value at entry or HDEF */
	uint64_t prev;		/* the id's stamp before (the enclosing levels') */
} htouch;

typedef struct hcpos {		/* __COUNTER__ digits in the output */
	size_t pos;
	uint32_t len, val;
} hcpos;

typedef struct hcrd {		/* hcread while recording: absolute value */
	const char *s;
	uint32_t len, off, dlen, val;
} hcrd;

typedef struct hsegr {		/* a child entry's text (tokens) in the output */
	size_t start, end;
	const hent *child;
	uint32_t counter0;
	uint32_t base0;		/* tokens: its location base */
} hsegr;

typedef struct hlev {
	hkey *k;
	uint32_t serial;
	int depth, errors0, warns0, text_started;
	uint32_t t0, o0, c0, r0, s0;	/* starts in the stacks below */
	uint32_t fe0, base0, nonce0, nlm0, npushed0;
	unsigned counter0;
	size_t text0;
	uint8_t first_sol, uses_level;
	uint32_t nfile, ndirs;
	uint32_t k0, x0;	/* starts in rec.kid, rec.kex */
	uint32_t l0;		/* start in rec.lk */
} hlev;

typedef struct hkidr {		/* a child entry of a level while recording */
	const hent *e;
	uint32_t ex0, nex;	/* its exclusions in rec.kex */
} hkidr;

#define HC_MAXT (1u << 24)

#define HC_MAXCP 65535u

#define HC_MAXCRD 65536u

/* ---------------- header cache: shared table ---------------- */

typedef struct {
	_Atomic unsigned long hit, miss, rec, uncacheable, full, bytes_replayed, bytes_rec, mem;
	_Atomic unsigned long nru, nrv, ndl, nfe, nvalid, nchecked;
	_Atomic unsigned long b_rd, b_dl, b_once, b_fe, b_text, b_seg, b_ent;	/* bytes stored, by kind */
} hstat_t;

/* ---------------- header cache: regions ---------------- */

#define HC_MAXVAR 64	/* entries per key */
#define HC_MAXORD (1u << 20)
#define HC_MAXSEG (1u << 20)

#define HC_MAXTK (1u << 24)
#define HC_MAXSB ((size_t)1 << 28)
#define HC_MAXKID (1u << 20)
#define HC_MAXKEX (1u << 22)
#define HC_MAXLK (1u << 16)

typedef struct {
	htouch *t;
	honce *ord;
	hcpos *ctr;
	hcrd *crd;
	hsegr *seg;
	hkidr *kid;
	uint32_t *kex;
	hlook *lk;
	cprov *cp;
} rec_spare_t;

#endif
