/*
 * vcc front end: declaration replay per region (RESEARCH-preprocessor.md
 * §6.9), step 2.
 *
 * A region is a header-cache hit that starts and ends at file-scope
 * declaration boundaries.  Its tokens are a function of (entry, __COUNTER__
 * at its start), so what parsing it does to the front end's state is a
 * function of those, the options, the #pragma pack state and the part of the
 * state it reads.  Recording a region (TU A) logs:
 *   - the names it looked up that were bound before it (or not bound), and
 *     the world W they reach: the records of older syms, tags, types,
 *     attributes, strings and lists, copied at first use (deep);
 *   - the types it interned or created, in order (the type log);
 *   - what it created (own records, index-addressed, in A's index space) and
 *     the writes to W (records whose post state differs).
 * Replaying it in TU B unifies W with B's state from the same names
 * (structurally, building the A -> B maps), replays the type log through B's
 * interning, appends the own records translated into B's index space and
 * applies the writes; the preprocessor then drops the region's tokens
 * (pp_skip_hit).  Anything not covered (diagnostics, #pragma weak/pack, lazy
 * bodies, labels, asm, _Complex, a reference that does not map) is not
 * published, so the region is parsed instead.
 *
 * VCC_DR=1 records and replays.  VCC_DRREC=1 records and publishes, and only
 * counts the hits that would replay.  VCC_DRCHECK=1 copies the whole state at
 * a recording's start and refuses to publish a region that wrote to an older
 * record outside W (a write the log misses); with VCC_DR=1 it also parses
 * every region that would replay and compares the result, field by field,
 * with what the replay would have written (stderr: "drcheck:").
 * VCC_DR_MAX_MB caps the entries' memory (default 512).
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#include "ccimpl.h"

#define VEC(T) struct { T *v; uint32_t n, cap; }
#define VGROW(x, k)                                                                                 \
	do {                                                                                        \
		if ((x).n + (k) > (x).cap) {                                                        \
			(x).cap = ((x).n + (k)) * 2 + 32;                                           \
			(x).v = realloc((x).v, (size_t)(x).cap * sizeof(*(x).v));                   \
		}                                                                                   \
	} while (0)
#define VPUSH(x, val)                     \
	do {                              \
		VGROW(x, 1);              \
		(x).v[(x).n++] = (val);   \
	} while (0)

typedef VEC(char) cvec;

static void *xdup(const void *p, size_t n)
{
	void *q = malloc(n ? n : 1);

	if (n)
		memcpy(q, p, n);
	return q;
}

/* ---------------- record schemas ---------------- */

/* what a field holds */
enum {
	K_IMM,		/* a value */
	K_LOC,		/* a location (ext records: not compared) */
	K_SHADOW,	/* sym.shadow (not compared; unmappable: 0, rebound by sym_bind) */
	K_SYM, K_TAG,
	K_TYPE,		/* qty */
	K_TIDX,		/* type index */
	K_SELF,		/* ctype.canon of a type that is not an aligned variant: itself */
	K_NODE,		/* (ext records: compared as zero / non-zero) */
	K_LIST,		/* list index */
	K_LIST1,	/* list index + 1, 0: none */
	K_LISTO,	/* a list index if the region's, else kept */
	K_IPOOL, K_FPOOL,
	K_STR,		/* string index */
	K_STR1,		/* string index + 1, 0: none */
	K_ATTR,
	K_PARAMS,	/* function type's aux (by ty_func) */
	K_BAD,		/* not supported */
};

#define LK_IMM 0xff	/* (list element marks: a value, explicitly) */
#define LK_DEAD 0xfe	/* a list nothing in the state refers to (declarator scratch): copied as is */

typedef struct fd { uint16_t off, size; uint8_t kind; } fd;
#define FD(T, f, k) ((fd){ offsetof(T, f), sizeof(((T *)0)->f), (k) })
#define FDR(T, f0, f1, k) ((fd){ offsetof(T, f0), offsetof(T, f1) + sizeof(((T *)0)->f1) - offsetof(T, f0), (k) })

static int sym_fd(const sym *s, fd *o)
{
	int n = 0;

	o[n++] = FD(sym, name, K_IMM);
	o[n++] = FD(sym, loc, K_LOC);
	o[n++] = FDR(sym, kind, sc, K_IMM);
	o[n++] = FD(sym, flags, K_IMM);
	o[n++] = FD(sym, ty, K_TYPE);
	o[n++] = FD(sym, align, K_IMM);
	o[n++] = FD(sym, val, s->kind == S_TAG ? K_TAG : K_IMM);
	o[n++] = FD(sym, body, K_NODE);
	o[n++] = FD(sym, asmname, K_IMM);
	o[n++] = FD(sym, attrs, K_ATTR);
	o[n++] = FD(sym, shadow, K_SHADOW);
	o[n++] = FD(sym, depth, K_IMM);
	o[n++] = FD(sym, next, K_SYM);
	o[n++] = FD(sym, ovl, K_SYM);
	o[n++] = FD(sym, plb, K_LOC);
	o[n++] = FD(sym, ple, K_LOC);
	o[n++] = FD(sym, psyms, K_LIST1);
	return n;
}

/* (f: separately) */
static int tag_fd(const tag *t, fd *o)
{
	int n = 0;

	o[n++] = FD(tag, name, K_IMM);
	o[n++] = FD(tag, loc, K_LOC);
	o[n++] = FD(tag, ty, K_TIDX);
	o[n++] = FDR(tag, flags, nfield, K_IMM);
	o[n++] = FD(tag, size, K_IMM);
	o[n++] = FDR(tag, align, maxalign, K_IMM);
	o[n++] = FD(tag, typedef_name, K_IMM);
	o[n++] = FD(tag, enums, t->nenum ? K_LIST : K_IMM);
	o[n++] = FD(tag, nenum, K_IMM);
	return n;
}

static int field_fd(fd *o)
{
	int n = 0;

	o[n++] = FD(field, name, K_IMM);
	o[n++] = FD(field, ty, K_TYPE);
	o[n++] = FD(field, off, K_IMM);
	o[n++] = FDR(field, width, bitfield, K_IMM);
	o[n++] = FD(field, loc, K_LOC);
	o[n++] = FD(field, counted_by, K_IMM);
	return n;
}

static bool is_tagkind(int k) { return k == TY_STRUCT || k == TY_UNION || k == TY_ENUM; }

static int ty_fd(const ctype *t, fd *o)
{
	int n = 0;

	o[n++] = FDR(ctype, kind, flags, K_IMM);
	o[n++] = FD(ctype, align, K_IMM);
	o[n++] = FD(ctype, size, K_IMM);
	o[n++] = FD(ctype, base, K_TYPE);
	o[n++] = FD(ctype, n, t->flags & TF_VLA ? K_NODE : t->flags & TF_ATOMIC ? K_TIDX : K_IMM);
	o[n++] = FD(ctype, aux, is_tagkind(t->kind) ? K_TAG : t->kind == TY_FUNC ? K_PARAMS : K_IMM);
	o[n++] = FD(ctype, canon, t->flags & TF_ALIGNED ? K_TIDX : K_SELF);
	return n;
}

static int attr_fd(fd *o)
{
	int n = 0;

	o[n++] = FD(attrs, aligned, K_IMM);
	o[n++] = FD(attrs, section, K_STR1);
	o[n++] = FD(attrs, alias, K_STR1);
	o[n++] = FD(attrs, asmname, K_STR1);
	o[n++] = FD(attrs, cleanup, K_SYM);
	o[n++] = FDR(attrs, mode, vecsize, K_IMM);
	o[n++] = FD(attrs, counted_by, K_IMM);
	o[n++] = FD(attrs, visibility, K_STR1);
	o[n++] = FD(attrs, flags, K_IMM);
	o[n++] = FDR(attrs, packed, destructor, K_IMM);
	o[n++] = FDR(attrs, nonstring, error_fn, K_IMM);
	o[n++] = FDR(attrs, overloadable, noinstr, K_IMM);
	return n;
}

/* a node's operands */
static void node_kinds(const node *x, uint8_t k[3])
{
	k[0] = k[1] = k[2] = K_IMM;
	switch (x->op) {
	case N_NONE: case N_ZERO: case N_BREAK: case N_CONTINUE: break;
	case N_NUM: k[0] = K_IPOOL; break;
	case N_FNUM: k[0] = K_FPOOL; break;
	case N_STR: k[0] = K_STR; break;
	case N_VAR: k[0] = K_SYM; break;
	case N_MEMBER: k[0] = K_NODE, k[1] = K_TAG; break;
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_ASSIGN: case N_COMMA: case N_CPLX:
	case N_WHILE: case N_DO: case N_SWITCH:
		k[0] = k[1] = K_NODE;
		break;
	case N_COND: case N_IF: k[0] = k[1] = k[2] = K_NODE; break;
	case N_CALL: k[0] = K_NODE, k[1] = K_LIST; break;
	case N_BUILTIN: case N_INIT: k[1] = K_LIST; break;
	case N_CAST: case N_UNARY: case N_ADDR: case N_DEREF: case N_PREINC: case N_PREDEC: case N_POSTINC:
	case N_POSTDEC: case N_SIZEOF_VLA: case N_STMTEXPR: case N_VAARG: case N_EXPR: case N_GOTO_IND: case N_RETURN:
		k[0] = K_NODE;
		break;
	case N_COMPLIT: k[0] = K_NODE, k[1] = K_SYM; break;
	case N_BLOCK: k[0] = x->aux ? K_LIST : K_LISTO, k[1] = K_LIST; break;
	case N_DECL: k[0] = K_SYM, k[1] = K_NODE; break;
	case N_FOR: k[0] = k[1] = K_NODE, k[2] = K_LIST; break;
	case N_CASE: k[0] = k[1] = K_IPOOL, k[2] = K_NODE; break;
	case N_DEFAULT: k[2] = K_NODE; break;
	default: k[0] = K_BAD; break;	/* labels, asm, designators */
	}
}

static int node_fd(const node *x, fd *o)
{
	uint8_t k[3];
	int n = 0;

	node_kinds(x, k);
	o[n++] = FDR(node, op, aux, K_IMM);
	o[n++] = FD(node, ty, K_TYPE);
	o[n++] = FD(node, loc, K_LOC);
	o[n++] = FD(node, a, k[0]);
	o[n++] = FD(node, b, k[1]);
	o[n++] = FD(node, c, k[2]);
	return n;
}

/* a field of up to 8 bytes, little-endian (schema_check: no wider ones) */
static inline uint64_t rdz(const char *p, unsigned size)
{
	uint64_t v = 0;

	switch (size) {
	case 1: return *(const uint8_t *)p;
	case 2: { uint16_t x; memcpy(&x, p, 2); return x; }
	case 4: { uint32_t x; memcpy(&x, p, 4); return x; }
	case 8: memcpy(&v, p, 8); return v;
	case 3: memcpy(&v, p, 3); return v;
	case 5: memcpy(&v, p, 5); return v;
	case 6: memcpy(&v, p, 6); return v;
	case 7: memcpy(&v, p, 7); return v;
	}
	return 0;
}

static inline void wrz(char *p, unsigned size, uint64_t v)
{
	switch (size) {
	case 1: *(uint8_t *)p = (uint8_t)v; break;
	case 2: { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); break; }
	case 4: { uint32_t x = (uint32_t)v; memcpy(p, &x, 4); break; }
	case 8: memcpy(p, &v, 8); break;
	case 3: memcpy(p, &v, 3); break;
	case 5: memcpy(p, &v, 5); break;
	case 6: memcpy(p, &v, 6); break;
	case 7: memcpy(p, &v, 7); break;
	}
}

static inline uint64_t rd(const void *r, fd f) { return rdz((const char *)r + f.off, f.size); }
static inline void wr(void *r, fd f, uint64_t v) { wrz((char *)r + f.off, f.size, v); }

/* ---------------- entries ---------------- */

enum { W_SYM, W_TAG, W_TY, W_ATTR, W_STR, W_LIST, W_N };

typedef struct wsym { uint32_t a; sym s; } wsym;
/* fields at wf[fo]; k: its values interned (tc_intern), set at publish */
typedef struct wtag { uint32_t a, fo; const void *k; tag t; } wtag;
typedef struct wty { uint32_t a, po; uint8_t intern; ctype t; } wty;	/* params at wp[po]; intern: only as an
									   interning result, not compared */
typedef struct wattr { uint32_t a; attrs r; } wattr;
typedef struct wstr { uint32_t a, bo; cstr s; } wstr;			/* bytes at wb[bo] */
typedef struct wlist { uint32_t a, n, vo; } wlist;			/* syms at wv[vo] */

enum { OP_INTERN, OP_NEW, OP_FUNC };
typedef struct drop { uint8_t op; uint32_t a, po; ctype t; } drop;	/* FUNC: params at opar[po], t.n of them */
typedef struct drread { uint32_t id, s, flags; uint8_t ns, kind; } drread;	/* kind, flags: of s in A */
typedef struct xrec { uint32_t w, mask; union { sym s; tag t; attrs r; ctype y; } u; } xrec;	/* a write: W position,
											   changed fields */
#define XF_F (1u << 31)	/* tag.f changed */

/*
 * Own records are kept in A's indices; apply copies them and fixes only the
 * fields that change, by a relocation list built at publish (rel_build): an
 * offset into the array, an op, and for a W reference its W position or for
 * an own type its index in the region.
 */
enum { RA_SYM, RA_TAG, RA_NODE, RA_SPAN, RA_LIST, RA_ATTR, RA_HEAP, RA_N };
enum { RO_DS, RO_DT, RO_DN, RO_DL, RO_DI, RO_DF, RO_DR, RO_DA,	/* own: + (B start - A start) */
       RO_ZERO, RO_W, RO_W1, RO_TYOWN, RO_TYW, RO_TIOWN, RO_TIW };
typedef struct drrel { uint32_t off; uint8_t op, size, wk, _pad; uint32_t arg; } drrel;

typedef struct drent {
	struct drent *_Atomic next;
	const void *he;
	unsigned ctr;
	uint32_t pack;
	uint64_t sig;
	uint32_t nsym_bi, nty_bi, ntag_bi;
	uint32_t base, span;
	/* A's counts at the start and the end */
	uint32_t s0, s1, t0, t1, y0, y1, n0, n1, l0, l1, i0, i1, f0, f1, r0, r1, a0, a1;
	drread *rd;
	uint32_t nrd;
	/* W, per kind: (A index << 32 | position) sorted, and the records by position */
	uint64_t *wk[W_N];	/* per kind, open addressing: (A index + 1) << 32 | position; 0: empty */
	uint32_t wmask[W_N];
	uint32_t nw[W_N];
	wsym *ws;
	wtag *wt;
	wty *wy;
	wattr *wa;
	wstr *wst;
	wlist *wl;
	field *wf;
	qty *wp;
	char *wb;
	uint32_t *wv;
	uint32_t *wvp;		/* per wv element: its W position (W_SYM), UINT32_MAX: none */
	/* the type log */
	drop *ops;
	uint32_t nops;
	qty *opar;
	ctype *fin;		/* per op: the final content (NEW) */
	/* own records, A's indices; heap pointers as offset + 1 into heap */
	sym *syms;
	tag *tags;
	node *nodes;
	pp_span *spans;
	uint32_t *lists;
	uint8_t *lk;		/* per list element: K_* */
	i128 *ip;
	double *fp;
	cstr *strs;
	attrs *attr;
	char *heap;
	size_t nheap;
	uint32_t *fpc, nfpc;	/* field pieces in heap: (offset, count) */
	struct drrel *rel[RA_N];	/* the own records' references, per array (rel_build) */
	uint32_t nrel[RA_N];
	uint32_t *bind, nbind;	/* syms bound at file scope, in order */
	uint32_t *tl, ntl;	/* toplevel */
	xrec *xs, *xt, *xa, *xy;
	uint32_t nxs, nxt, nxa, nxy;
	uint32_t prevloc;
	uint8_t prevflags;
	uint64_t ntok;
	size_t bytes;
} drent;

#define DR_NB 65536
#define DR_VARIANTS 8
static drent *_Atomic dr_tab[DR_NB];
static _Atomic uint64_t dr_mem, dr_max;
static int dr_mode = -1;	/* 0 off, 1 record + replay (VCC_DR), 2 record only (VCC_DRREC) */
static int dr_check;

static uint32_t key_hash(const void *he, unsigned ctr, uint32_t pack, uint64_t sig)
{
	uint64_t h = (uintptr_t)he * 0x9E3779B97F4A7C15ull ^ (ctr + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;

	h ^= (pack + sig * 31) * 0x165667B19E3779F9ull;
	return (uint32_t)(h >> 40) & (DR_NB - 1);
}

/* the #pragma pack state a region sees: its depth (lazy bodies) and the alignment in effect */
static uint32_t pack_key(const cc *c)
{
	return c->npack ? (uint32_t)c->npack << 24 | c->pack[c->npack - 1] : 0;
}

/*
 * Regions that did not publish: after NEVER_AFTER failures (or abandoned
 * recordings) a key is not recorded again, as what stops it (a lazy body, a
 * pragma, asm ...) is mostly the region's own.
 */
#define NEVER_N 65536
#define NEVER_AFTER 2
static struct never { _Atomic uint64_t key; _Atomic uint32_t n; } never_tab[NEVER_N];

static uint64_t never_key(const void *he, unsigned ctr, uint32_t pack, uint64_t sig)
{
	uint64_t h = (uintptr_t)he * 0x9E3779B97F4A7C15ull ^ (ctr + 1) * 0xC2B2AE3D27D4EB4Full ^
		     (pack + 7 + sig * 131) * 0x165667B19E3779F9ull;

	return h | 1;
}

static struct never *never_slot(uint64_t k, bool make)
{
	for (uint32_t i = 0, j = (uint32_t)(k >> 40) & (NEVER_N - 1); i < 16; i++, j = (j + 1) & (NEVER_N - 1)) {
		uint64_t x = atomic_load_explicit(&never_tab[j].key, memory_order_acquire), z = 0;

		if (x == k)
			return &never_tab[j];
		if (x)
			continue;
		if (!make)
			return NULL;
		if (atomic_compare_exchange_strong(&never_tab[j].key, &z, k) || z == k)
			return &never_tab[j];
	}
	return NULL;
}

static void never_note(const void *he, unsigned ctr, uint32_t pack, uint64_t sig, uint32_t n)
{
	struct never *v = never_slot(never_key(he, ctr, pack, sig), true);

	if (v)
		atomic_fetch_add_explicit(&v->n, n, memory_order_relaxed);
}

static bool never_record(const void *he, unsigned ctr, uint32_t pack, uint64_t sig)
{
	struct never *v = never_slot(never_key(he, ctr, pack, sig), false);

	return v && atomic_load_explicit(&v->n, memory_order_relaxed) >= NEVER_AFTER;
}

/* ---------------- per-TU state ---------------- */

enum { F_DIAG, F_PRAGMA, F_LAZY, F_LABEL, F_ASM, F_CPLX, F_WEAK, F_BODY, F_TYWRITE, F_LISTWRITE, F_UNHOOKED,
       F_EXTBIND, F_UNMAPPED, F_TYMUT, F_OPS, F_HEAP, F_BUDGET, F_VARIANTS, F_N };
static const char *const fname[F_N] = { "diag", "pragma", "lazy", "label", "asm", "complex", "pweak", "ext-var-body",
	"type-write", "list-write", "unhooked-write", "ext-bind", "unmapped", "type-mutated", "type-log", "heap",
	"budget", "variants" };

typedef struct wmap { uint64_t *k; uint32_t *v; uint32_t cap, n; } wmap;	/* (kind << 32 | A index) -> position */

struct drrec {
	bool open;
	uint64_t b;
	const void *he;
	unsigned ctr;
	uint32_t base, span, pack;
	uint32_t nsym_bi, nty_bi, ntag_bi;
	uint32_t s0, t0, y0, n0, l0, i0, f0, r0, a0, st0, tl0, tla0, lb0, as0, lzf0, lzt0, lzs0, pw0;
	int npack0, errors0, pperr0;
	uint32_t ndiag0;
	uint32_t cplx0[TY_NKINDS];
	uint32_t fail;		/* F_* bits */
	size_t heap0;		/* heap used at the start */
	/* reads */
	uint64_t *hs;
	uint32_t hcap, hn;
	VEC(drread) rds;
	/* W */
	wmap wm;
	VEC(wsym) ws;
	VEC(wtag) wt;
	VEC(wty) wy;
	VEC(wattr) wa;
	VEC(wstr) wst;
	VEC(wlist) wl;
	VEC(field) wf;
	VEC(qty) wp;
	VEC(char) wb;
	VEC(uint32_t) wv;
	VEC(uint64_t) work;	/* (kind << 32 | position) */
	/* the type log */
	VEC(drop) ops;
	VEC(qty) opar;
	uint32_t *rt, rcap, rn;		/* the region's ty_intern table */
	uint32_t *real, realcap;
	uint32_t *rf, rfcap, rfn;	/* the region's ty_func table */
	uint32_t *freal, frealcap, fnreal;
	/* VCC_DRCHECK: the state at the start */
	sym *ssym;
	tag *stag;
	ctype *sty;
	attrs *sattr;
	uint32_t csym, ctag, cty, cattr;
	/* replay scratch */
	uint32_t *slot[W_N], cslot[W_N];
	uint32_t *stamp[3], cstamp[3], gen;
	uint32_t *tymap, ctymap;
	VEC(uint64_t) uw;
	VEC(qty) tpar;
	/* per tag: the interned values (wtag.k) a successful validation compared it with, NULL: none or written
	   since (tag_memo) */
	const void **tk;
	uint32_t ctk;
	VEC(uint64_t) tdone;	/* this try's fully compared tags: (W position << 32 | B index) */
	/* VCC_DR + VCC_DRCHECK: the region is parsed and compared with what this entry's replay would do */
	const struct drent *chk;
	uint32_t chk_base;
};

/* stats */
enum { S_OPEN, S_PUB, S_NOPUB, S_ABANDON, S_TRY, S_HIT, S_MISS, S_NOENT, S_NOSKIP, S_NEVER, S_N };
static _Atomic uint64_t st_n[S_N], st_tok[S_N], st_fail[F_N], st_failtok[F_N], st_bytes, st_ents;

#define ADD(x, v) atomic_fetch_add_explicit(&(x), (v), memory_order_relaxed)

static _Atomic uint64_t tk_val, tk_apply, tk_close, tk_w;
static inline uint64_t ticks(void)
{
	uint64_t v;

#if defined(__aarch64__)
	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
#else
	v = 0;
#endif
	return v;
}

static void count(int k, uint64_t tok)
{
	ADD(st_n[k], 1);
	ADD(st_tok[k], tok);
}

/* ---------------- tag memo ---------------- */

/*
 * The values of a tag (everything but references and locations: the tag,
 * its fields, its enumerators) are interned process-wide, one copy per
 * distinct content: tc_intern.  A TU remembers per tag which content a
 * successful validation compared it with (tk).  Any write to the tag since
 * clears that (DR_TAGWRITE for parsed code, apply for replayed writes, a
 * restore for all), so a tag that matches its memo has those values, and
 * validation only pairs its references (tag_known).  Enumerators are never
 * written after their enum body.
 */
typedef struct tcont { struct tcont *next; uint64_t h; uint32_t n; char b[]; } tcont;
#define TC_NB 65536
static tcont *_Atomic tc_tab[TC_NB];
static pthread_mutex_t tc_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t tc_bytes, tc_n;

static const void *tc_intern(const char *b, uint32_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	tcont *x;
	uint32_t j;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ (unsigned char)b[i]) * 0x100000001b3ull;
	j = (uint32_t)(h >> 40) & (TC_NB - 1);
	for (x = atomic_load_explicit(&tc_tab[j], memory_order_acquire); x; x = x->next)
		if (x->h == h && x->n == n && !memcmp(x->b, b, n))
			return x;
	pthread_mutex_lock(&tc_mu);
	for (x = atomic_load_explicit(&tc_tab[j], memory_order_acquire); x; x = x->next)
		if (x->h == h && x->n == n && !memcmp(x->b, b, n))
			break;
	if (!x) {
		x = malloc(sizeof(*x) + n);
		x->h = h, x->n = n;
		memcpy(x->b, b, n);
		x->next = atomic_load_explicit(&tc_tab[j], memory_order_relaxed);
		atomic_store_explicit(&tc_tab[j], x, memory_order_release);
		ADD(tc_bytes, sizeof(*x) + n);
		ADD(tc_n, 1);
	}
	pthread_mutex_unlock(&tc_mu);
	return x;
}

static void tk_set(struct drrec *r, uint32_t t, const void *k)
{
	if (t >= r->ctk) {
		uint32_t nc;

		if (!k)
			return;
		nc = t + t / 2 + 1024;
		r->tk = realloc(r->tk, nc * sizeof(*r->tk));
		memset(r->tk + r->ctk, 0, (nc - r->ctk) * sizeof(*r->tk));
		r->ctk = nc;
	}
	r->tk[t] = k;
}

/* ---------------- small hash tables ---------------- */

static uint32_t hmix(uint64_t k) { return (uint32_t)(k * 0x9E3779B97F4A7C15ull >> 32); }

static bool rs_add(struct drrec *r, uint64_t k)
{
	uint32_t m;

	if (r->hn * 2 >= r->hcap) {
		uint32_t ncap = r->hcap ? r->hcap * 2 : 1024;
		uint64_t *nh = calloc(ncap, sizeof(uint64_t));

		for (uint32_t i = 0; i < r->hcap; i++)
			if (r->hs[i]) {
				uint32_t j = hmix(r->hs[i]) & (ncap - 1);

				while (nh[j])
					j = (j + 1) & (ncap - 1);
				nh[j] = r->hs[i];
			}
		free(r->hs);
		r->hs = nh, r->hcap = ncap;
	}
	m = r->hcap - 1;
	k++;	/* (0: empty) */
	for (uint32_t j = hmix(k) & m;; j = (j + 1) & m) {
		if (!r->hs[j]) {
			r->hs[j] = k, r->hn++;
			return true;
		}
		if (r->hs[j] == k)
			return false;
	}
}

/* position of key k, or insert it with value v (returns UINT32_MAX then) */
static uint32_t wm_get(wmap *w, uint64_t k, uint32_t v)
{
	uint32_t m;

	k++;
	if (w->n * 2 >= w->cap) {
		uint32_t ncap = w->cap ? w->cap * 2 : 1024;
		uint64_t *nk = calloc(ncap, sizeof(uint64_t));
		uint32_t *nv = malloc(ncap * sizeof(uint32_t));

		for (uint32_t i = 0; i < w->cap; i++)
			if (w->k[i]) {
				uint32_t j = hmix(w->k[i]) & (ncap - 1);

				while (nk[j])
					j = (j + 1) & (ncap - 1);
				nk[j] = w->k[i], nv[j] = w->v[i];
			}
		free(w->k), free(w->v);
		w->k = nk, w->v = nv, w->cap = ncap;
	}
	m = w->cap - 1;
	for (uint32_t j = hmix(k) & m;; j = (j + 1) & m) {
		if (!w->k[j]) {
			w->k[j] = k, w->v[j] = v, w->n++;
			return UINT32_MAX;
		}
		if (w->k[j] == k)
			return w->v[j];
	}
}

static bool wm_has(const wmap *w, uint64_t k)
{
	uint32_t m = w->cap - 1;

	if (!w->cap)
		return false;
	k++;
	for (uint32_t j = hmix(k) & m;; j = (j + 1) & m) {
		if (!w->k[j])
			return false;
		if (w->k[j] == k)
			return true;
	}
}

/* position of key k, UINT32_MAX if absent */
static uint32_t wm_find(const wmap *w, uint64_t k)
{
	uint32_t m = w->cap - 1;

	if (!w->cap)
		return UINT32_MAX;
	k++;
	for (uint32_t j = hmix(k) & m;; j = (j + 1) & m) {
		if (!w->k[j])
			return UINT32_MAX;
		if (w->k[j] == k)
			return w->v[j];
	}
}

static void wm_clear(wmap *w)
{
	if (w->n)
		memset(w->k, 0, w->cap * sizeof(uint64_t));
	w->n = 0;
}

/* ---------------- recording: the world W ---------------- */

static void w_ty(cc *c, uint32_t i, bool intern);

static void w_push(struct drrec *r, int kind, uint32_t pos)
{
	VPUSH(r->work, (uint64_t)kind << 32 | pos);
}

static void w_sym(cc *c, uint32_t s)
{
	struct drrec *r = c->dr;

	if (!s || s >= r->s0 || wm_get(&r->wm, (uint64_t)W_SYM << 32 | s, r->ws.n) != UINT32_MAX)
		return;
	VPUSH(r->ws, ((wsym){ s, c->syms[s] }));
	w_push(r, W_SYM, r->ws.n - 1);
}

static void w_tag(cc *c, uint32_t t)
{
	struct drrec *r = c->dr;
	const tag *g = &c->tags[t];
	wtag w;

	if (!t || t >= r->t0 || wm_get(&r->wm, (uint64_t)W_TAG << 32 | t, r->wt.n) != UINT32_MAX)
		return;
	w.a = t, w.t = *g, w.fo = r->wf.n;
	w.k = NULL;
	if (g->f && g->nfield) {
		VGROW(r->wf, g->nfield);
		memcpy(r->wf.v + r->wf.n, g->f, g->nfield * sizeof(field));
		r->wf.n += g->nfield;
	}
	VPUSH(r->wt, w);
	w_push(r, W_TAG, r->wt.n - 1);
}

static void w_attr(cc *c, uint32_t a)
{
	struct drrec *r = c->dr;

	if (!a || a >= r->a0 || wm_get(&r->wm, (uint64_t)W_ATTR << 32 | a, r->wa.n) != UINT32_MAX)
		return;
	VPUSH(r->wa, ((wattr){ a, c->attr[a] }));
	w_push(r, W_ATTR, r->wa.n - 1);
}

static void w_str(cc *c, uint32_t s)
{
	struct drrec *r = c->dr;
	const cstr *x = &c->strs[s];
	size_t n = (size_t)x->n * x->esz;

	if (!s || s >= r->st0 || wm_get(&r->wm, (uint64_t)W_STR << 32 | s, r->wst.n) != UINT32_MAX)
		return;
	VPUSH(r->wst, ((wstr){ s, r->wb.n, *x }));
	VGROW(r->wb, n);
	memcpy(r->wb.v + r->wb.n, x->s, n);
	r->wb.n += (uint32_t)n;
}

static void w_list(cc *c, uint32_t l, uint32_t n)
{
	struct drrec *r = c->dr;

	if (l >= r->l0 || wm_get(&r->wm, (uint64_t)W_LIST << 32 | l, r->wl.n) != UINT32_MAX)
		return;
	VPUSH(r->wl, ((wlist){ l, n, r->wv.n }));
	VGROW(r->wv, n);
	memcpy(r->wv.v + r->wv.n, &c->lists[l], n * sizeof(uint32_t));
	r->wv.n += n;
	for (uint32_t i = 0; i < n; i++)
		w_sym(c, c->lists[l + i]);
}

/* the refs of an older record */
static void w_refs(cc *c, const void *rec, const fd *f, int n)
{
	for (int i = 0; i < n; i++) {
		uint64_t v = rd(rec, f[i]);

		switch (f[i].kind) {
		case K_SYM: w_sym(c, (uint32_t)v); break;
		case K_TAG: w_tag(c, (uint32_t)v); break;
		case K_TYPE: w_ty(c, QI((uint32_t)v), false); break;
		case K_TIDX: w_ty(c, (uint32_t)v, false); break;
		case K_ATTR: w_attr(c, (uint32_t)v); break;
		case K_STR: w_str(c, (uint32_t)v); break;
		case K_STR1: if (v) w_str(c, (uint32_t)v - 1); break;
		case K_BAD: c->dr->fail |= 1u << F_UNMAPPED; break;
		default: break;
		}
	}
}

static void w_ty(cc *c, uint32_t i, bool intern)
{
	struct drrec *r = c->dr;
	const ctype *t = &c->ty[i];
	uint32_t p;
	wty w;

	if (i < r->nty_bi || i >= r->y0)
		return;
	p = wm_get(&r->wm, (uint64_t)W_TY << 32 | i, r->wy.n);
	if (p != UINT32_MAX) {
		if (!intern && r->wy.v[p].intern) {
			r->wy.v[p].intern = 0;	/* reached from a read after all: compared */
			w_push(r, W_TY, p);
		}
		return;
	}
	w.a = i, w.t = *t, w.intern = intern, w.po = r->wp.n;
	if (t->kind == TY_FUNC) {
		VGROW(r->wp, t->n);
		memcpy(r->wp.v + r->wp.n, &c->params[t->aux], t->n * sizeof(qty));
		r->wp.n += t->n;
	}
	VPUSH(r->wy, w);
	if (!intern)
		w_push(r, W_TY, r->wy.n - 1);
}

static void w_drain(cc *c)
{
	struct drrec *r = c->dr;
	fd f[24];
	int n;

	while (r->work.n) {
		uint64_t x = r->work.v[--r->work.n];
		uint32_t p = (uint32_t)x;

		switch ((int)(x >> 32)) {
		case W_SYM: {
			sym s = r->ws.v[p].s;

			n = sym_fd(&s, f);
			w_refs(c, &s, f, n);
			if (s.body && s.kind == S_VAR)
				r->fail |= 1u << F_BODY;
			if (s.psyms) {
				const ctype *t = &c->ty[QI(s.ty)];

				if (t->kind != TY_FUNC)
					r->fail |= 1u << F_UNMAPPED;
				else
					w_list(c, s.psyms - 1, t->n);
			}
			break;
		}
		case W_TAG: {
			wtag w = r->wt.v[p];

			n = tag_fd(&w.t, f);
			w_refs(c, &w.t, f, n);
			if (w.t.nenum)
				w_list(c, w.t.enums, w.t.nenum);
			n = field_fd(f);
			for (uint32_t i = 0; i < w.t.nfield && w.t.f; i++) {
				field q = r->wf.v[w.fo + i];

				w_refs(c, &q, f, n);
			}
			break;
		}
		case W_TY: {
			wty w = r->wy.v[p];

			n = ty_fd(&w.t, f);
			w_refs(c, &w.t, f, n);
			if (w.t.flags & TF_VLA)
				r->fail |= 1u << F_UNMAPPED;
			if (w.t.kind == TY_FUNC)
				for (uint32_t i = 0; i < w.t.n; i++)
					w_ty(c, QI(r->wp.v[w.po + i]), false);
			break;
		}
		case W_ATTR: {
			attrs a = r->wa.v[p].r;

			n = attr_fd(f);
			w_refs(c, &a, f, n);
			break;
		}
		}
	}
}

/* a name lookup (ns 0: ordinary, 1: tag) gave s */
VPRIV __attribute__((noinline)) void dr_read(cc *c, int ns, uint32_t id, uint32_t s)
{
	struct drrec *r = c->dr;

	if (!r->open || (s && s >= r->s0))
		return;
	if (!rs_add(r, (uint64_t)ns << 31 | id))
		return;
	VPUSH(r->rds, ((drread){ id, s, s ? c->syms[s].flags : 0, (uint8_t)ns, s ? c->syms[s].kind : 0 }));
	if (s) {
		uint64_t t0 = ticks();

		w_sym(c, s);
		w_drain(c);
		ADD(tk_w, ticks() - t0);
	}
}

/* ---------------- recording: the type log ---------------- */

static void rt_insert(struct drrec *r, cc *c, uint32_t idx)
{
	uint32_t m;

	if ((r->rn + 1) * 2 > r->rcap) {
		uint32_t ncap = r->rcap * 2, *nh = calloc(ncap, sizeof(uint32_t));

		for (uint32_t j = 0; j < r->rcap; j++) {
			uint32_t x = r->rt[j], k;

			if (!x)
				continue;
			for (k = ty_hash(&c->ty[x]) & (ncap - 1); nh[k]; k = (k + 1) & (ncap - 1))
				;
			nh[k] = x;
		}
		free(r->rt);
		r->rt = nh, r->rcap = ncap;
		c->tyhash = r->rt, c->tyhcap = r->rcap;
	}
	m = r->rcap - 1;
	for (uint32_t k = ty_hash(&c->ty[idx]) & m;; k = (k + 1) & m)
		if (!r->rt[k]) {
			r->rt[k] = idx, r->rn++;
			return;
		}
}

/*
 * While a region is recorded, c->tyhash and c->fnhash are tables of its own,
 * so ty_intern and ty_func reach dr_intern / dr_func once per structural type
 * the region interns: its type log.  The TU's tables stay complete.  ty_ptr's
 * cache ignores pointer types from before the region (ptr_lo).
 */
VPRIV __attribute__((noinline)) uint32_t dr_intern(cc *c, const ctype *t)
{
	struct drrec *r = c->dr;
	uint32_t i;

	c->tyhash = r->real, c->tyhcap = r->realcap, c->dr_on = 0;
	i = ty_intern(c, t);
	r->real = c->tyhash, r->realcap = c->tyhcap, c->dr_on = 1;
	c->tyhash = r->rt, c->tyhcap = r->rcap;
	rt_insert(r, c, i);
	VPUSH(r->ops, ((drop){ OP_INTERN, i, 0, *t }));
	if (i < r->y0)
		w_ty(c, i, true);
	return i;
}

static void rf_insert(struct drrec *r, cc *c, uint32_t idx)
{
	uint32_t m;

	if ((r->rfn + 1) * 2 > r->rfcap) {
		uint32_t ncap = r->rfcap * 2, *nh = calloc(ncap, sizeof(uint32_t));

		for (uint32_t j = 0; j < r->rfcap; j++) {
			uint32_t x = r->rf[j], k;
			const ctype *f = &c->ty[x];

			if (!x)
				continue;
			for (k = fn_hash(f->base, &c->params[f->aux], f->n, f->flags) & (ncap - 1); nh[k];
			     k = (k + 1) & (ncap - 1))
				;
			nh[k] = x;
		}
		free(r->rf);
		r->rf = nh, r->rfcap = ncap;
		c->fnhash = r->rf, c->fnhcap = r->rfcap;
	}
	m = r->rfcap - 1;
	{
		const ctype *f = &c->ty[idx];

		for (uint32_t k = fn_hash(f->base, &c->params[f->aux], f->n, f->flags) & m;; k = (k + 1) & m)
			if (!r->rf[k]) {
				r->rf[k] = idx, r->rfn++;
				return;
			}
	}
}

VPRIV __attribute__((noinline)) qty dr_func(cc *c, qty ret, const qty *par, uint32_t n, int flags)
{
	struct drrec *r = c->dr;
	uint32_t nfn = c->nfn;
	qty q;
	drop o = { OP_FUNC, 0, r->opar.n, { 0 } };

	c->fnhash = r->freal, c->fnhcap = r->frealcap, c->nfn = r->fnreal, c->dr_on = 0;
	q = ty_func(c, ret, par, n, flags);
	r->freal = c->fnhash, r->frealcap = c->fnhcap, r->fnreal = c->nfn, c->dr_on = 1;
	c->fnhash = r->rf, c->fnhcap = r->rfcap, c->nfn = nfn;
	rf_insert(r, c, QI(q));
	o.a = QI(q);
	o.t.kind = TY_FUNC, o.t.flags = (uint8_t)flags, o.t.base = ret, o.t.n = n;
	VGROW(r->opar, n);
	memcpy(r->opar.v + r->opar.n, par, n * sizeof(qty));
	r->opar.n += n;
	VPUSH(r->ops, o);
	if (QI(q) < r->y0)
		w_ty(c, QI(q), true);
	return q;
}

VPRIV __attribute__((noinline)) void dr_new(cc *c, uint32_t idx)
{
	VPUSH(c->dr->ops, ((drop){ OP_NEW, idx, 0, c->ty[idx] }));
}

VPRIV __attribute__((noinline)) void dr_pragma(cc *c, int kind)
{
	struct drrec *r = c->dr;

	if (!r->open)
		return;
	r->fail |= 1u << (kind == 1 ? F_WEAK : F_PRAGMA);
}

/* DR_TAGWRITE: tag ti is written by the code being parsed */
VPRIV __attribute__((noinline)) void dr_tagwrite(cc *c, uint32_t ti)
{
	struct drrec *r = c->dr;

	tk_set(r, ti, NULL);
	if (r->open && ti < r->t0 && ti >= r->ntag_bi && !wm_has(&r->wm, (uint64_t)W_TAG << 32 | ti))
		r->fail |= 1u << F_UNHOOKED;	/* (an older tag the reads did not reach: the entry would miss the write) */
}

/* ---------------- opening and closing ---------------- */

static void *grow(void *p, uint32_t *cap, uint32_t need, size_t el)
{
	if (need <= *cap)
		return p;
	*cap = need + need / 2 + 1024;
	return realloc(p, (size_t)*cap * el);
}

/* the TU's own tables back */
static void rec_stop(cc *c)
{
	struct drrec *r = c->dr;

	if (c->tyhash == r->rt)
		c->tyhash = r->real, c->tyhcap = r->realcap;
	if (c->fnhash == r->rf)
		c->fnhash = r->freal, c->fnhcap = r->frealcap, c->nfn = r->fnreal;
	c->ptr_lo = 1;
	c->dr_on = 0;
	r->open = false;
	r->chk = NULL;
}

static void rec_open(cc *c, uint64_t b, const void *he, uint32_t base, unsigned ctr, uint32_t span)
{
	struct drrec *r = c->dr;

	r->open = true;
	r->b = b, r->he = he, r->base = base, r->ctr = ctr, r->span = span;
	r->pack = pack_key(c);
	r->s0 = c->nsym, r->t0 = c->ntag, r->y0 = c->nty, r->n0 = c->nnode, r->l0 = c->nlist;
	r->i0 = c->nipool, r->f0 = c->nfpool, r->st0 = c->nstr, r->a0 = c->nattr, r->r0 = c->nparams;
	r->as0 = c->nasm, r->lb0 = c->nlabel, r->tl0 = c->ntoplevel, r->tla0 = c->ntlasm;
	r->lzf0 = c->nlzf, r->lzt0 = c->nlzt, r->lzs0 = c->nlzs, r->pw0 = c->npweak;
	r->npack0 = c->npack, r->errors0 = c->errors, r->ndiag0 = c->ndiag, r->pperr0 = pp_errors(c->p);
	memcpy(r->cplx0, c->cplx, sizeof(r->cplx0));
	r->fail = 0;
	if (r->hn) {
		memset(r->hs, 0, r->hcap * sizeof(uint64_t));
		r->hn = 0;
	}
	r->rds.n = 0;
	wm_clear(&r->wm);
	r->ws.n = r->wt.n = r->wy.n = r->wa.n = r->wst.n = r->wl.n = 0;
	r->wf.n = r->wp.n = r->wb.n = r->wv.n = r->work.n = 0;
	r->ops.n = r->opar.n = 0;
	if (dr_check) {
		r->ssym = grow(r->ssym, &r->csym, c->nsym, sizeof(sym));
		memcpy(r->ssym, c->syms, c->nsym * sizeof(sym));
		r->stag = grow(r->stag, &r->ctag, c->ntag, sizeof(tag));
		memcpy(r->stag, c->tags, c->ntag * sizeof(tag));
		r->sty = grow(r->sty, &r->cty, c->nty, sizeof(ctype));
		memcpy(r->sty, c->ty, c->nty * sizeof(ctype));
		r->sattr = grow(r->sattr, &r->cattr, c->nattr, sizeof(attrs));
		memcpy(r->sattr, c->attr, c->nattr * sizeof(attrs));
	}
	if (!r->rt)
		r->rt = calloc(r->rcap = 1024, sizeof(uint32_t));
	else if (r->rn)
		memset(r->rt, 0, r->rcap * sizeof(uint32_t));
	r->rn = 0;
	if (!r->rf)
		r->rf = calloc(r->rfcap = 256, sizeof(uint32_t));
	else if (r->rfn)
		memset(r->rf, 0, r->rfcap * sizeof(uint32_t));
	r->rfn = 0;
	r->real = c->tyhash, r->realcap = c->tyhcap;
	c->tyhash = r->rt, c->tyhcap = r->rcap;
	r->freal = c->fnhash, r->frealcap = c->fnhcap, r->fnreal = c->nfn;
	c->fnhash = r->rf, c->fnhcap = r->rfcap;
	c->ptr_lo = c->nty;
	c->dr_on = 1;
	count(S_OPEN, 0);
}

/* ---------------- translation (A's indices to B's) ---------------- */

typedef struct trx {
	cc *c;			/* B (check: A) */
	const drent *e;
	struct drrec *r;
	bool check, bad;
	int64_t ds, dt, dn, dl, di, df, dr, da;	/* own records: B start - A start */
	uint32_t baseB;
	char *heapB;
} trx;

static int64_t wfind(const drent *e, int k, uint32_t a)
{
	const uint64_t *v = e->wk[k];
	uint32_t m = e->wmask[k];

	for (uint32_t j = hmix((uint64_t)a + 1) & m;; j = (j + 1) & m) {
		uint64_t x = v[j];

		if (!x)
			return -1;
		if ((uint32_t)(x >> 32) == a + 1)
			return (int64_t)(uint32_t)x;
	}
}

/* a W member's B index (check: present) */
static uint32_t tr_w(trx *x, int k, uint32_t a, bool *ok)
{
	int64_t p = wfind(x->e, k, a);

	if (p < 0) {
		*ok = false;
		return 0;
	}
	*ok = true;
	if (x->check)
		return a;
	return x->r->slot[k][p] - 1;
}

static uint32_t tr_ty(trx *x, uint32_t a)
{
	const drent *e = x->e;
	bool ok;
	uint32_t b;

	if (a < e->nty_bi)
		return a;
	if (a >= e->y0 && a < e->y1)
		return x->check ? a : x->r->tymap[a - e->y0];
	b = tr_w(x, W_TY, a, &ok);
	if (!ok)
		x->bad = true;
	return b;
}

static uint64_t tr(trx *x, int k, uint64_t v64)
{
	const drent *e = x->e;
	uint32_t v = (uint32_t)v64, b;
	bool ok;

	switch (k) {
	case K_IMM: case K_PARAMS: case K_SELF:
		return v64;
	case K_LOC:
		if (!v)
			return 0;
		if (v - e->base < e->span)
			return x->check ? v : x->baseB + (v - e->base);
		x->bad = true;
		return 0;
	case K_SHADOW:
	case K_SYM:
		if (!v)
			return 0;
		if (v >= e->s0 && v < e->s1)
			return x->check ? v : (uint32_t)(v + x->ds);
		b = tr_w(x, W_SYM, v, &ok);
		if (ok)
			return b;
		if (v < e->nsym_bi)
			return v;
		if (k == K_SYM)
			x->bad = true;
		return 0;
	case K_TAG:
		if (!v)
			return 0;
		if (v >= e->t0 && v < e->t1)
			return x->check ? v : (uint32_t)(v + x->dt);
		b = tr_w(x, W_TAG, v, &ok);
		if (ok)
			return b;
		if (v < e->ntag_bi)
			return v;
		x->bad = true;
		return 0;
	case K_TYPE:
		return QT(tr_ty(x, QI(v)), QQ(v));
	case K_TIDX:
		return tr_ty(x, v);
	case K_NODE:
		if (!v)
			return 0;
		if (v >= e->n0 && v < e->n1)
			return x->check ? v : (uint32_t)(v + x->dn);
		x->bad = true;
		return 0;
	case K_LIST:
		if (v >= e->l0 && v < e->l1)
			return x->check ? v : (uint32_t)(v + x->dl);
		if (!v)
			return 0;
		b = tr_w(x, W_LIST, v, &ok);
		if (!ok)
			x->bad = true;
		return b;
	case K_LIST1:
		return v ? tr(x, K_LIST, v - 1) + 1 : 0;
	case K_LISTO:
		return v >= e->l0 && v < e->l1 && !x->check ? (uint32_t)(v + x->dl) : v;
	case K_IPOOL:
		if (v >= e->i0 && v < e->i1)
			return x->check ? v : (uint32_t)(v + x->di);
		x->bad = true;
		return 0;
	case K_FPOOL:
		if (v >= e->f0 && v < e->f1)
			return x->check ? v : (uint32_t)(v + x->df);
		x->bad = true;
		return 0;
	case K_STR:
		if (!v)
			return 0;
		if (v >= e->r0 && v < e->r1)
			return x->check ? v : (uint32_t)(v + x->dr);
		b = tr_w(x, W_STR, v, &ok);
		if (!ok)
			x->bad = true;
		return b;
	case K_STR1:
		return v ? tr(x, K_STR, v - 1) + 1 : 0;
	case K_ATTR:
		if (!v)
			return 0;
		if (v >= e->a0 && v < e->a1)
			return x->check ? v : (uint32_t)(v + x->da);
		b = tr_w(x, W_ATTR, v, &ok);
		if (!ok)
			x->bad = true;
		return b;
	default:
		x->bad = true;
		return 0;
	}
}

/* a record's refs, in place */
static void tr_rec(trx *x, void *rec, const fd *f, int n, uint32_t self)
{
	for (int i = 0; i < n; i++) {
		if (f[i].kind == K_IMM)
			continue;
		if (f[i].kind == K_SELF) {
			if (!x->check)
				wr(rec, f[i], self);
			continue;
		}
		{
			uint64_t v = tr(x, f[i].kind, rd(rec, f[i]));

			if (!x->check)
				wr(rec, f[i], v);
		}
	}
}

/* ---------------- closing: publish ---------------- */

static void fail_count(uint32_t fail, uint64_t tok)
{
	for (int i = 0; i < F_N; i++)
		if (fail & (1u << i)) {
			ADD(st_fail[i], 1);
			ADD(st_failtok[i], tok);
			return;	/* (the first reason) */
		}
}

static uint64_t opt_sig(const cc_opts *o)
{
	return (uint64_t)o->target | (uint64_t)!!o->unsigned_char << 8 | (uint64_t)!!o->ms_anon << 9 |
	       (uint64_t)!!o->short_wchar << 10 | (uint64_t)o->std << 11 | (uint64_t)!!o->implicit_int << 20 |
	       (uint64_t)!!o->gnu89_inline << 21 | (uint64_t)!!o->instrument << 22 |
	       (uint64_t)!!o->general_regs_only << 23 | (uint64_t)!!o->no_builtin << 24 |
	       (uint64_t)!!o->implicit_fn << 25 | (uint64_t)!!o->emit << 26;
}

/* copy n bytes of the A heap at p into the entry heap: its offset + 1; 0 if p is not the region's */
static uint32_t heap_piece(cvec *h, const cc *c, size_t heap0, const void *p, size_t n, bool *bad)
{
	uint32_t at;

	if (!p)
		return 0;
	if ((const char *)p < c->heap + heap0 || (const char *)p + n > c->hp) {
		*bad = true;
		return 0;
	}
	at = (h->n + 15) & ~15u;
	VGROW(*h, at - h->n + n);
	memset(h->v + h->n, 0, at - h->n);
	memcpy(h->v + at, p, n);
	h->n = at + (uint32_t)n;
	return at + 1;
}

typedef VEC(drrel) relvec;

/* the relocation of field f (value v64) of an own record at byte offset off; false: unmapped */
static int64_t rel_w(const struct drrec *r, int k, uint32_t a)
{
	uint32_t p = wm_find(&r->wm, (uint64_t)k << 32 | a);

	return p == UINT32_MAX ? -1 : (int64_t)p;
}

static bool rel_one(const drent *e, const struct drrec *r, relvec *rl, uint32_t off, fd f, uint64_t v64)
{
	uint32_t v = (uint32_t)v64, ix;
	int64_t p;
	drrel x = { off + f.off, 0, (uint8_t)f.size, 0, 0, 0 };
	int k = f.kind;

	switch (k) {
	case K_IMM: case K_PARAMS:
		return true;
	case K_LOC:	/* (rel_locs: every one in the region) */
		return !v || v - e->base < e->span;
	case K_SHADOW: case K_SYM:
		if (!v)
			return true;
		if (v >= e->s0 && v < e->s1) {
			x.op = RO_DS;
			break;
		}
		if ((p = rel_w(r, W_SYM, v)) >= 0) {
			x.op = RO_W, x.wk = W_SYM, x.arg = (uint32_t)p;
			break;
		}
		if (v < e->nsym_bi)
			return true;
		if (k == K_SYM)
			return false;
		x.op = RO_ZERO;
		break;
	case K_TAG:
		if (!v)
			return true;
		if (v >= e->t0 && v < e->t1) {
			x.op = RO_DT;
			break;
		}
		if ((p = rel_w(r, W_TAG, v)) >= 0) {
			x.op = RO_W, x.wk = W_TAG, x.arg = (uint32_t)p;
			break;
		}
		return v < e->ntag_bi;
	case K_TYPE: case K_TIDX:
		ix = k == K_TYPE ? QI(v) : v;
		if (ix < e->nty_bi)
			return true;
		if (ix >= e->y0 && ix < e->y1) {
			x.op = k == K_TYPE ? RO_TYOWN : RO_TIOWN, x.arg = ix - e->y0;
			break;
		}
		if ((p = rel_w(r, W_TY, ix)) < 0)
			return false;
		x.op = k == K_TYPE ? RO_TYW : RO_TIW, x.arg = (uint32_t)p;
		break;
	case K_NODE:
		if (!v)
			return true;
		if (v < e->n0 || v >= e->n1)
			return false;
		x.op = RO_DN;
		break;
	case K_LIST: case K_LIST1: case K_LISTO:
		if (k == K_LIST1) {
			if (!v)
				return true;
			v--;
		}
		if (v >= e->l0 && v < e->l1) {
			x.op = RO_DL;
			break;
		}
		if (!v || k == K_LISTO)
			return true;
		if ((p = rel_w(r, W_LIST, v)) < 0)
			return false;
		x.op = k == K_LIST1 ? RO_W1 : RO_W, x.wk = W_LIST, x.arg = (uint32_t)p;
		break;
	case K_IPOOL: case K_FPOOL:
		if (k == K_IPOOL ? v < e->i0 || v >= e->i1 : v < e->f0 || v >= e->f1)
			return false;
		x.op = k == K_IPOOL ? RO_DI : RO_DF;
		break;
	case K_STR: case K_STR1:
		if (!v)
			return true;
		if (k == K_STR1 && !--v)
			return true;
		if (v >= e->r0 && v < e->r1) {
			x.op = RO_DR;
			break;
		}
		if ((p = rel_w(r, W_STR, v)) < 0)
			return false;
		x.op = k == K_STR1 ? RO_W1 : RO_W, x.wk = W_STR, x.arg = (uint32_t)p;
		break;
	case K_ATTR:
		if (!v)
			return true;
		if (v >= e->a0 && v < e->a1) {
			x.op = RO_DA;
			break;
		}
		if ((p = rel_w(r, W_ATTR, v)) < 0)
			return false;
		x.op = RO_W, x.wk = W_ATTR, x.arg = (uint32_t)p;
		break;
	default:
		return false;
	}
	VPUSH(*rl, x);
	return true;
}

static bool rel_rec(const drent *e, const struct drrec *r, relvec *rl, uint32_t off, const void *rec, const fd *f, int n)
{
	bool ok = true;

	for (int i = 0; i < n; i++)
		ok &= rel_one(e, r, rl, off, f[i], rd(rec, f[i]));
	return ok;
}

/* the relocation lists of e's own records (e->syms ... and the field pieces of heap h) */
static bool rel_build(drent *e, const struct drrec *r, const char *h)
{
	relvec rl[RA_N] = { 0 };
	fd f[24], sf[2] = { FD(pp_span, b, K_LOC), FD(pp_span, e, K_LOC) };
	int n;
	bool ok = true;

	for (uint32_t i = 0; i < e->s1 - e->s0; i++) {
		n = sym_fd(&e->syms[i], f);
		ok &= rel_rec(e, r, &rl[RA_SYM], i * (uint32_t)sizeof(sym), &e->syms[i], f, n);
	}
	for (uint32_t i = 0; i < e->t1 - e->t0; i++) {
		n = tag_fd(&e->tags[i], f);
		ok &= rel_rec(e, r, &rl[RA_TAG], i * (uint32_t)sizeof(tag), &e->tags[i], f, n);
	}
	for (uint32_t i = 0; i < e->n1 - e->n0; i++) {
		n = node_fd(&e->nodes[i], f);
		ok &= rel_rec(e, r, &rl[RA_NODE], i * (uint32_t)sizeof(node), &e->nodes[i], f, n);
		ok &= rel_rec(e, r, &rl[RA_SPAN], i * (uint32_t)sizeof(pp_span), &e->spans[i], sf, 2);
	}
	for (uint32_t i = 0; i < e->l1 - e->l0; i++)
		if (e->lk[i] != K_IMM && e->lk[i] != LK_DEAD)
			ok &= rel_one(e, r, &rl[RA_LIST], i * 4, (fd){ 0, 4, e->lk[i] }, e->lists[i]);
	n = attr_fd(f);
	for (uint32_t i = 0; i < e->a1 - e->a0; i++)
		ok &= rel_rec(e, r, &rl[RA_ATTR], i * (uint32_t)sizeof(attrs), &e->attr[i], f, n);
	n = field_fd(f);
	for (uint32_t i = 0; i < e->nfpc; i += 2)
		for (uint32_t k = 0; k + 1 < e->fpc[i + 1]; k++) {
			uint32_t off = e->fpc[i] + k * (uint32_t)sizeof(field);

			ok &= rel_rec(e, r, &rl[RA_HEAP], off, h + off, f, n);
		}
	for (int a = 0; a < RA_N; a++)
		e->rel[a] = rl[a].v, e->nrel[a] = rl[a].n;
	return ok;
}

/* apply relocations rl[0, n) to the copied array at base */
static void rel_apply(const trx *x, char *base, const drrel *rl, uint32_t n)
{
	const struct drrec *r = x->r;
	const int64_t d[8] = { x->ds, x->dt, x->dn, x->dl, x->di, x->df, x->dr, x->da };

	for (uint32_t i = 0; i < n; i++) {
		const drrel *q = &rl[i];
		char *p = base + q->off;
		uint64_t v = rdz(p, q->size);

		switch (q->op) {
		case RO_ZERO: v = 0; break;
		case RO_W: v = r->slot[q->wk][q->arg] - 1; break;
		case RO_W1: v = r->slot[q->wk][q->arg]; break;
		case RO_TYOWN: v = QT(r->tymap[q->arg], QQ((uint32_t)v)); break;
		case RO_TYW: v = QT(r->slot[W_TY][q->arg] - 1, QQ((uint32_t)v)); break;
		case RO_TIOWN: v = r->tymap[q->arg]; break;
		case RO_TIW: v = r->slot[W_TY][q->arg] - 1; break;
		default: v = (uint32_t)(v + d[q->op]); break;
		}
		wrz(p, q->size, v);
	}
}

/* the locations of copied own records: + (B base - A base), 0 stays */
#define REL_LOC(x_, lb_) ((x_) = (x_) ? (x_) + (lb_) : 0)

static void drcheck(cc *c);

static void rec_close(cc *c, uint64_t endpos)
{
	struct drrec *r = c->dr;
	uint64_t tok = endpos - r->b;
	uint32_t fail = r->fail;
	drent *e;
	trx x;
	fd f[24];
	int n;
	cvec hp = { 0 };
	size_t heap0 = r->heap0;
	VEC(uint32_t) fpc = { 0 };
	VEC(xrec) xs = { 0 }, xt = { 0 }, xa = { 0 }, xy = { 0 };
	uint32_t ownty = 0;

	{
		const drent *chk = r->chk;

		rec_stop(c);
		if (chk) {
			r->chk = chk;
			drcheck(c);
			r->chk = NULL;
			return;
		}
	}
	if (c->errors != r->errors0 || c->ndiag != r->ndiag0 || pp_errors(c->p) != r->pperr0)
		fail |= 1u << F_DIAG;
	if (c->npack != r->npack0)
		fail |= 1u << F_PRAGMA;
	if (c->nlzf != r->lzf0 || c->nlzt != r->lzt0 || c->nlzs != r->lzs0)
		fail |= 1u << F_LAZY;
	if (c->nlabel != r->lb0)
		fail |= 1u << F_LABEL;
	if (c->nasm != r->as0 || c->ntlasm != r->tla0)
		fail |= 1u << F_ASM;
	if (memcmp(r->cplx0, c->cplx, sizeof(r->cplx0)))
		fail |= 1u << F_CPLX;
	if (c->npweak != r->pw0)
		fail |= 1u << F_WEAK;
	if (fail)
		goto nopub;

	/* writes to W (and, with VCC_DRCHECK, to anything else older) */
	for (uint32_t i = 0; i < r->ws.n; i++) {
		const sym *a = &r->ws.v[i].s, *b = &c->syms[r->ws.v[i].a];
		xrec w = { i, 0, { .s = *b } };

		if (!memcmp(a, b, sizeof(sym)))
			continue;
		n = sym_fd(a, f);
		for (int k = 0; k < n; k++)
			if (rd(a, f[k]) != rd(b, f[k]))
				w.mask |= 1u << k;
		if (w.mask)
			VPUSH(xs, w);
	}
	for (uint32_t i = 0; i < r->wt.n; i++) {
		const wtag *a = &r->wt.v[i];
		const tag *b = &c->tags[a->a];
		xrec w = { i, 0, { .t = *b } };

		n = tag_fd(&a->t, f);
		for (int k = 0; k < n; k++)
			if (rd(&a->t, f[k]) != rd(b, f[k]))
				w.mask |= 1u << k;
		if (a->t.f != b->f)
			w.mask |= XF_F;
		else if (b->f && a->t.nfield && memcmp(&r->wf.v[a->fo], b->f, a->t.nfield * sizeof(field)))
			fail |= 1u << F_TYWRITE;
		if (w.mask)
			VPUSH(xt, w);
	}
	for (uint32_t i = 0; i < r->wy.n; i++) {
		const ctype *a = &r->wy.v[i].t, *b = &c->ty[r->wy.v[i].a];
		xrec w = { i, 0, { .y = *b } };

		if (!memcmp(a, b, sizeof(ctype)))
			continue;
		if (r->wy.v[i].intern || !is_tagkind(a->kind) || (a->flags & (TF_ALIGNED | TF_ATOMIC))) {
			fail |= 1u << F_TYWRITE;	/* (an interned type: its hash would change) */
			continue;
		}
		n = ty_fd(a, f);
		for (int k = 0; k < n; k++)
			if (rd(a, f[k]) != rd(b, f[k]))
				w.mask |= 1u << k;
		VPUSH(xy, w);
	}
	for (uint32_t i = 0; i < r->wa.n; i++) {
		const attrs *a = &r->wa.v[i].r, *b = &c->attr[r->wa.v[i].a];
		xrec w = { i, 0, { .r = *b } };

		n = attr_fd(f);
		for (int k = 0; k < n; k++)
			if (rd(a, f[k]) != rd(b, f[k]))
				w.mask |= 1u << k;
		if (w.mask)
			VPUSH(xa, w);
	}
	for (uint32_t i = 0; i < r->wl.n; i++)
		if (memcmp(&r->wv.v[r->wl.v[i].vo], &c->lists[r->wl.v[i].a], r->wl.v[i].n * sizeof(uint32_t)))
			fail |= 1u << F_LISTWRITE;
	if (dr_check) {
		for (uint32_t i = 1; i < r->s0; i++)
			if (memcmp(&r->ssym[i], &c->syms[i], sizeof(sym)) && !wm_has(&r->wm, (uint64_t)W_SYM << 32 | i))
				fail |= 1u << F_UNHOOKED;
		for (uint32_t i = 1; i < r->t0; i++)
			if (memcmp(&r->stag[i], &c->tags[i], sizeof(tag)) && !wm_has(&r->wm, (uint64_t)W_TAG << 32 | i))
				fail |= 1u << F_UNHOOKED;
		for (uint32_t i = 0; i < r->y0; i++)
			if (memcmp(&r->sty[i], &c->ty[i], sizeof(ctype)) && !wm_has(&r->wm, (uint64_t)W_TY << 32 | i))
				fail |= 1u << F_UNHOOKED;
		for (uint32_t i = 1; i < r->a0; i++)
			if (memcmp(&r->sattr[i], &c->attr[i], sizeof(attrs)) && !wm_has(&r->wm, (uint64_t)W_ATTR << 32 | i))
				fail |= 1u << F_UNHOOKED;
	}
	for (uint32_t i = r->tl0; i < c->ntoplevel; i++)
		if (c->toplevel[i] < r->s0)
			fail |= 1u << F_EXTBIND;
	if (fail)
		goto nopub;

	/* the entry */
	e = calloc(1, sizeof(drent));
	e->he = r->he, e->ctr = r->ctr, e->pack = r->pack, e->sig = opt_sig(c->o);
	e->nsym_bi = r->nsym_bi, e->nty_bi = r->nty_bi, e->ntag_bi = r->ntag_bi;
	e->base = r->base, e->span = r->span;
	e->s0 = r->s0, e->s1 = c->nsym, e->t0 = r->t0, e->t1 = c->ntag, e->y0 = r->y0, e->y1 = c->nty;
	e->n0 = r->n0, e->n1 = c->nnode, e->l0 = r->l0, e->l1 = c->nlist, e->i0 = r->i0, e->i1 = c->nipool;
	e->f0 = r->f0, e->f1 = c->nfpool, e->r0 = r->st0, e->r1 = c->nstr, e->a0 = r->a0, e->a1 = c->nattr;
	e->ntok = tok;
	e->prevloc = c->prevloc, e->prevflags = c->prevflags;
	e->rd = xdup(r->rds.v, r->rds.n * sizeof(drread)), e->nrd = r->rds.n;
	{
		uint32_t nw[W_N] = { r->ws.n, r->wt.n, r->wy.n, r->wa.n, r->wst.n, r->wl.n };

		for (int k = 0; k < W_N; k++) {
			uint32_t cap = 2;

			while (cap < nw[k] * 2)
				cap *= 2;
			e->nw[k] = nw[k];
			e->wmask[k] = cap - 1;
			e->wk[k] = calloc(cap, sizeof(uint64_t));
			for (uint32_t i = 0; i < nw[k]; i++) {
				uint32_t a = k == W_SYM ? r->ws.v[i].a : k == W_TAG ? r->wt.v[i].a : k == W_TY ? r->wy.v[i].a
					   : k == W_ATTR ? r->wa.v[i].a : k == W_STR ? r->wst.v[i].a : r->wl.v[i].a, j;

				for (j = hmix((uint64_t)a + 1) & (cap - 1); e->wk[k][j]; j = (j + 1) & (cap - 1))
					;
				e->wk[k][j] = ((uint64_t)a + 1) << 32 | i;
			}
		}
	}
	/* the W tags' values (tag_known): the tag, its fields, its enumerators, as A had them before the region */
	{
		cvec cb = { 0 };

		for (uint32_t i = 0; i < r->wt.n; i++) {
			wtag *w = &r->wt.v[i];
			bool ok = true;

			if (w->a < r->ctk && r->tk[w->a]) {
				w->k = r->tk[w->a];	/* (known, and not written since: also before the region) */
				continue;
			}
			cb.n = 0;
#define PUT(rec, fds, nf)                                                                         \
	for (int q_ = 0; q_ < (nf); q_++) {                                                       \
		uint64_t v_ = rd((rec), (fds)[q_]);                                               \
		int k_ = (fds)[q_].kind;                                                          \
		if (k_ == K_LOC || k_ == K_SHADOW)                                                \
			continue;                                                                 \
		if (k_ == K_TYPE || k_ == K_TIDX) {                                               \
			uint32_t ix_ = k_ == K_TYPE ? QI((uint32_t)v_) : (uint32_t)v_;            \
			if (ix_ >= r->nty_bi)                                                     \
				v_ = k_ == K_TYPE ? QQ((uint32_t)v_) | 1ull << 40 : 1ull << 40;   \
		} else if (k_ != K_IMM) {                                                         \
			v_ = v_ ? 1ull << 41 : 0;                                                 \
		}                                                                                 \
		VGROW(cb, 8);                                                                     \
		memcpy(cb.v + cb.n, &v_, 8);                                                      \
		cb.n += 8;                                                                        \
	}
			n = tag_fd(&w->t, f);
			PUT(&w->t, f, n);
			n = field_fd(f);
			for (uint32_t j = 0; j < w->t.nfield && w->t.f; j++)
				PUT(&r->wf.v[w->fo + j], f, n);
			if (w->t.nenum) {
				uint32_t lp = wm_find(&r->wm, (uint64_t)W_LIST << 32 | w->t.enums);

				if (lp == UINT32_MAX)
					ok = false;
				for (uint32_t j = 0; ok && j < r->wl.v[lp].n; j++) {
					uint32_t sp = wm_find(&r->wm, (uint64_t)W_SYM << 32 | r->wv.v[r->wl.v[lp].vo + j]);

					if (sp == UINT32_MAX) {
						ok = false;
						break;
					}
					{
						const sym *y = &r->ws.v[sp].s;

						if (y->kind != S_ENUMCONST || y->attrs || y->body || y->next || y->ovl || y->psyms) {
							ok = false;	/* (tag_known pairs only an enumerator's type) */
							break;
						}
						n = sym_fd(y, f);
						PUT(y, f, n);
					}
				}
			}
#undef PUT
			w->k = ok ? tc_intern(cb.v, cb.n) : NULL;
			if (w->k) {	/* (A's tag has these values now too unless the region wrote it) */
				bool wrote = false;

				for (uint32_t j = 0; j < xt.n && !wrote; j++)
					wrote = xt.v[j].w == i;
				if (!wrote)
					tk_set(r, w->a, w->k);
			}
		}
		free(cb.v);
	}
	e->ws = xdup(r->ws.v, r->ws.n * sizeof(wsym));
	e->wt = xdup(r->wt.v, r->wt.n * sizeof(wtag));
	e->wy = xdup(r->wy.v, r->wy.n * sizeof(wty));
	e->wa = xdup(r->wa.v, r->wa.n * sizeof(wattr));
	e->wst = xdup(r->wst.v, r->wst.n * sizeof(wstr));
	e->wl = xdup(r->wl.v, r->wl.n * sizeof(wlist));
	e->wf = xdup(r->wf.v, r->wf.n * sizeof(field));
	e->wp = xdup(r->wp.v, r->wp.n * sizeof(qty));
	e->wb = xdup(r->wb.v, r->wb.n);
	e->wv = xdup(r->wv.v, r->wv.n * sizeof(uint32_t));
	e->wvp = malloc((r->wv.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < r->wv.n; i++)
		e->wvp[i] = r->wv.v[i] ? wm_find(&r->wm, (uint64_t)W_SYM << 32 | r->wv.v[i]) : UINT32_MAX;
	e->ops = xdup(r->ops.v, r->ops.n * sizeof(drop)), e->nops = r->ops.n;
	e->opar = xdup(r->opar.v, r->opar.n * sizeof(qty));
	e->fin = malloc((r->ops.n + 1) * sizeof(ctype));
	x = (trx){ c, e, r, true, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL };

	/* the type log: every own type made by one op, content mappable */
	for (uint32_t i = 0; i < e->nops; i++) {
		drop *o = &e->ops[i];
		const ctype *now = &c->ty[o->a];

		e->fin[i] = *now;
		if (o->a >= e->y0)
			ownty++;
		if (o->op == OP_NEW) {
			if (o->a < e->y0 || now->kind == TY_FUNC)
				fail |= 1u << F_OPS;
			n = ty_fd(now, f);
			tr_rec(&x, &e->fin[i], f, n, o->a);
		} else if (o->op == OP_FUNC) {
			tr(&x, K_TYPE, o->t.base);
			for (uint32_t k = 0; k < o->t.n; k++)
				tr(&x, K_TYPE, e->opar[o->po + k]);
			if (o->a >= e->y0 && (now->kind != TY_FUNC || now->flags != o->t.flags || now->base != o->t.base ||
					      now->n != o->t.n || now->size != 1 || now->align != 4))
				fail |= 1u << F_TYMUT;
		} else {
			ctype t = o->t;

			if (t.kind == TY_FUNC)
				fail |= 1u << F_OPS;
			n = ty_fd(&t, f);
			tr_rec(&x, &t, f, n, o->a);
			if (o->a >= e->y0) {
				ctype a = o->t, b = *now;

				if (!(a.flags & TF_ALIGNED))
					a.canon = b.canon = 0;
				if (memcmp(&a, &b, sizeof(ctype)))
					fail |= 1u << F_TYMUT;
			}
		}
	}
	if (ownty != e->y1 - e->y0)
		fail |= 1u << F_OPS;

	/* own records */
	/* (rel_build checks that their references map) */
	e->syms = xdup(&c->syms[e->s0], (e->s1 - e->s0) * sizeof(sym));
	e->tags = xdup(&c->tags[e->t0], (e->t1 - e->t0) * sizeof(tag));
	for (uint32_t i = 0; i < e->t1 - e->t0; i++) {
		tag *t = &e->tags[i];

		if (t->f) {
			uint32_t at = heap_piece(&hp, c, heap0, t->f, (t->nfield + 1) * sizeof(field), &x.bad);

			VPUSH(fpc, at - 1);
			VPUSH(fpc, t->nfield + 1);
			t->f = (field *)(uintptr_t)at;
		}
	}
	e->nodes = xdup(&c->nodes[e->n0], (e->n1 - e->n0) * sizeof(node));
	e->spans = xdup(&c->span[e->n0], (e->n1 - e->n0) * sizeof(pp_span));
	e->lists = xdup(&c->lists[e->l0], (e->l1 - e->l0) * sizeof(uint32_t));
	e->lk = calloc(e->l1 - e->l0 + 1, 1);
#define MARK(l, cnt, kk, ext_ok)                                                               \
	do {                                                                                   \
		uint32_t l_ = (l), n_ = (cnt);                                                 \
		if (l_ >= e->l0 && l_ + n_ <= e->l1) {                                         \
			for (uint32_t j_ = 0; j_ < n_; j_++) {                                 \
				uint8_t want_ = (kk);                                          \
				uint8_t *p_ = &e->lk[l_ - e->l0 + j_];                         \
				if (*p_ && *p_ != want_)                                       \
					x.bad = true;                                          \
				*p_ = want_;                                                   \
			}                                                                      \
		} else if (n_ && l_ < e->l0 && !ext_ok) {                                      \
			x.bad = true;                                                          \
		}                                                                              \
	} while (0)
	for (uint32_t i = 0; i < e->n1 - e->n0; i++) {
		node *d = &e->nodes[i];
		pp_span *s = &e->spans[i];

		switch (d->op) {
		case N_CALL: case N_BUILTIN: MARK(d->b, d->c, K_NODE, false); break;
		case N_BLOCK:
			MARK(d->b, d->c, K_NODE, false);
			if (d->aux)
				MARK(d->a, d->aux, K_SYM, false);
			break;
		case N_INIT:
			if (d->aux == 1)
				MARK(d->b, d->c * 3, j_ % 3 == 2 ? K_NODE : LK_IMM, false);
			else
				MARK(d->b, d->c, K_NODE, false);
			break;
		case N_FOR: MARK(d->c, 2, K_NODE, false); break;
		}
		(void)s;
	}
	for (uint32_t i = 0; i < e->s1 - e->s0; i++) {
		const sym *s = &c->syms[e->s0 + i];

		if (s->psyms) {
			const ctype *t = &c->ty[QI(s->ty)];

			if (t->kind != TY_FUNC)
				x.bad = true;
			else
				MARK(s->psyms - 1, t->n, K_SYM, true);
		}
	}
	for (uint32_t i = 0; i < e->t1 - e->t0; i++) {
		const tag *t = &c->tags[e->t0 + i];

		if (t->nenum)
			MARK(t->enums, t->nenum, K_SYM, true);
	}
	for (uint32_t i = 0; i < r->wt.n; i++)	/* (written ext tags: their new enum lists) */
		if (c->tags[r->wt.v[i].a].nenum)
			MARK(c->tags[r->wt.v[i].a].enums, c->tags[r->wt.v[i].a].nenum, K_SYM, true);
	for (uint32_t i = 0; i < r->ws.n; i++) {
		const sym *s = &c->syms[r->ws.v[i].a];

		if (s->psyms && s->psyms - 1 >= e->l0) {
			const ctype *t = &c->ty[QI(s->ty)];

			if (t->kind != TY_FUNC)
				x.bad = true;
			else
				MARK(s->psyms - 1, t->n, K_SYM, true);
		}
	}
#undef MARK
	for (uint32_t i = 0; i < e->l1 - e->l0; i++) {
		if (e->lk[i] == LK_IMM)
			e->lk[i] = K_IMM;
		else if (!e->lk[i])
			e->lk[i] = LK_DEAD;
	}
	e->ip = xdup(&c->ipool[e->i0], (e->i1 - e->i0) * sizeof(i128));
	e->fp = xdup(&c->fpool[e->f0], (e->f1 - e->f0) * sizeof(double));
	e->strs = xdup(&c->strs[e->r0], (e->r1 - e->r0) * sizeof(cstr));
	for (uint32_t i = 0; i < e->r1 - e->r0; i++) {
		cstr *s = &e->strs[i];

		s->s = (const char *)(uintptr_t)heap_piece(&hp, c, heap0, s->s, (size_t)s->n * s->esz, &x.bad);
	}
	e->attr = xdup(&c->attr[e->a0], (e->a1 - e->a0) * sizeof(attrs));

	/* writes: the changed fields must map */
	for (uint32_t i = 0; i < xs.n; i++) {
		n = sym_fd(&xs.v[i].u.s, f);
		for (int k = 0; k < n; k++)
			if (xs.v[i].mask & (1u << k))
				tr(&x, f[k].kind == K_SHADOW ? K_BAD : f[k].kind, rd(&xs.v[i].u.s, f[k]));
	}
	for (uint32_t i = 0; i < xt.n; i++) {
		tag *t = &xt.v[i].u.t;

		n = tag_fd(t, f);
		for (int k = 0; k < n; k++)
			if (xt.v[i].mask & (1u << k))
				tr(&x, f[k].kind, rd(t, f[k]));
		if (xt.v[i].mask & XF_F) {
			fd g[8];
			int m = field_fd(g);
			uint32_t at = heap_piece(&hp, c, heap0, t->f, (t->nfield + 1) * sizeof(field), &x.bad);

			for (uint32_t k = 0; k < t->nfield && t->f; k++) {
				field q = t->f[k];

				tr_rec(&x, &q, g, m, 0);
			}
			if (at) {
				VPUSH(fpc, at - 1);
				VPUSH(fpc, t->nfield + 1);
			}
			t->f = (field *)(uintptr_t)at;
		}
	}
	for (uint32_t i = 0; i < xy.n; i++) {
		n = ty_fd(&xy.v[i].u.y, f);
		for (int k = 0; k < n; k++)
			if (xy.v[i].mask & (1u << k))
				tr(&x, f[k].kind == K_SELF ? K_BAD : f[k].kind, rd(&xy.v[i].u.y, f[k]));
	}
	for (uint32_t i = 0; i < xa.n; i++) {
		n = attr_fd(f);
		for (int k = 0; k < n; k++)
			if (xa.v[i].mask & (1u << k))
				tr(&x, f[k].kind, rd(&xa.v[i].u.r, f[k]));
	}
	/* file-scope bindings: own syms only */
	{
		VEC(uint32_t) bind = { 0 };

		for (uint32_t i = c->nsstack; i-- > 0 && c->sstack[i] >= e->s0;)
			VPUSH(bind, c->sstack[i]);
		/* (the sstack's tail of own syms: everything bound at file scope by the region) */
		e->nbind = bind.n;
		e->bind = malloc((bind.n + 1) * sizeof(uint32_t));
		for (uint32_t i = 0; i < bind.n; i++)
			e->bind[i] = bind.v[bind.n - 1 - i];
		free(bind.v);
	}
	e->ntl = c->ntoplevel - r->tl0;
	e->tl = xdup(&c->toplevel[r->tl0], e->ntl * sizeof(uint32_t));
	tr(&x, K_LOC, e->prevloc);
	e->heap = hp.v, e->nheap = hp.n;
	e->fpc = fpc.v, e->nfpc = fpc.n;
	if (!rel_build(e, r, hp.v))
		x.bad = true;
	e->xs = xs.v, e->nxs = xs.n, e->xt = xt.v, e->nxt = xt.n, e->xa = xa.v, e->nxa = xa.n, e->xy = xy.v, e->nxy = xy.n;
	xs.v = xt.v = xa.v = xy.v = NULL;
	if (x.bad)
		fail |= 1u << F_UNMAPPED;
	e->bytes = sizeof(drent) + e->nrd * sizeof(drread) + r->ws.n * sizeof(wsym) + r->wt.n * sizeof(wtag) +
		   r->wy.n * sizeof(wty) + r->wa.n * sizeof(wattr) + r->wst.n * sizeof(wstr) + r->wl.n * sizeof(wlist) +
		   r->wf.n * sizeof(field) + r->wp.n * 4 + r->wb.n + r->wv.n * 8 + e->nops * (sizeof(drop) + sizeof(ctype)) +
		   r->opar.n * 4 + (e->s1 - e->s0) * sizeof(sym) + (e->t1 - e->t0) * sizeof(tag) +
		   (e->n1 - e->n0) * (sizeof(node) + sizeof(pp_span)) + (e->l1 - e->l0) * 5 + (e->i1 - e->i0) * 16 +
		   (e->f1 - e->f0) * 8 + (e->r1 - e->r0) * sizeof(cstr) + (e->a1 - e->a0) * sizeof(attrs) + e->nheap +
		   e->nfpc * 4 + e->nbind * 4 + e->ntl * 4 + (e->nxs + e->nxt + e->nxa + e->nxy) * sizeof(xrec);
	for (int k = 0; k < RA_N; k++)
		e->bytes += e->nrel[k] * sizeof(drrel);
	for (int k = 0; k < W_N; k++)
		e->bytes += (e->wmask[k] + 1) * 8;
	if (!fail) {
		uint64_t m = atomic_fetch_add_explicit(&dr_mem, e->bytes, memory_order_relaxed);

		if (m + e->bytes > atomic_load_explicit(&dr_max, memory_order_relaxed))
			fail |= 1u << F_BUDGET;
	}
	if (!fail) {
		uint32_t h = key_hash(e->he, e->ctr, e->pack, e->sig), nv = 0;
		drent *head = atomic_load_explicit(&dr_tab[h], memory_order_acquire);

		for (drent *q = head; q; q = atomic_load_explicit(&q->next, memory_order_acquire))
			nv += q->he == e->he && q->ctr == e->ctr && q->pack == e->pack && q->sig == e->sig;
		if (nv >= DR_VARIANTS)
			fail |= 1u << F_VARIANTS;
		else {
			atomic_store_explicit(&e->next, head, memory_order_relaxed);
			while (!atomic_compare_exchange_weak_explicit(&dr_tab[h], &head, e, memory_order_release,
								      memory_order_acquire))
				atomic_store_explicit(&e->next, head, memory_order_relaxed);
			count(S_PUB, tok);
			ADD(st_bytes, e->bytes);
			ADD(st_ents, 1);
			return;
		}
	}
	/* not published */
	free(e->rd);
	for (int k = 0; k < W_N; k++)
		free(e->wk[k]);
	free(e->ws), free(e->wt), free(e->wy), free(e->wa), free(e->wst), free(e->wl), free(e->wf), free(e->wp);
	free(e->wb), free(e->wv), free(e->wvp), free(e->ops), free(e->opar), free(e->fin), free(e->syms), free(e->tags);
	free(e->nodes), free(e->spans), free(e->lists), free(e->lk), free(e->ip), free(e->fp), free(e->strs);
	for (int k = 0; k < RA_N; k++)
		free(e->rel[k]);
	free(e->attr), free(e->heap), free(e->fpc), free(e->bind), free(e->tl), free(e->xs), free(e->xt), free(e->xa), free(e->xy);
	free(e);
	hp.v = NULL, fpc.v = NULL;
nopub:
	free(hp.v), free(fpc.v), free(xs.v), free(xt.v), free(xa.v), free(xy.v);
	count(S_NOPUB, tok);
	fail_count(fail, tok);
	/* (what the region's own tokens do stops it every time: no second recording) */
	if (!(fail & (1u << F_BUDGET | 1u << F_VARIANTS)))
		never_note(r->he, r->ctr, r->pack, opt_sig(c->o),
			   fail & (1u << F_PRAGMA | 1u << F_WEAK | 1u << F_LAZY | 1u << F_LABEL | 1u << F_ASM | 1u << F_CPLX)
				   ? NEVER_AFTER : 1);
}

/* ---------------- replay: validation ---------------- */

typedef struct un {
	cc *c;
	const drent *e;
	struct drrec *r;
} un;

static uint32_t *ensure(uint32_t **a, uint32_t *cap, uint32_t n, bool zero)
{
	if (n > *cap) {
		uint32_t nc = n + n / 2 + 1024;

		*a = realloc(*a, (size_t)nc * sizeof(uint32_t));
		memset(*a + *cap, 0, (size_t)(nc - *cap) * sizeof(uint32_t));
		*cap = nc;
	}
	if (zero)
		memset(*a, 0, (size_t)n * sizeof(uint32_t));
	return *a;
}

/* map W member a (kind k) to B's b; false on a conflict */
static bool pair_wp(un *u, int k, uint32_t a, uint32_t b, bool *found, bool push);

static bool pair_w(un *u, int k, uint32_t a, uint32_t b, bool *found)
{
	return pair_wp(u, k, a, b, found, true);
}

static bool pair_wp(un *u, int k, uint32_t a, uint32_t b, bool *found, bool push)
{
	int64_t p = wfind(u->e, k, a);
	uint32_t *s;

	*found = p >= 0;
	if (p < 0)
		return true;
	s = &u->r->slot[k][p];
	if (*s)
		return *s - 1 == b;
	*s = b + 1;
	if (k <= W_TY) {
		uint32_t lim = k == W_SYM ? u->c->nsym : k == W_TAG ? u->c->ntag : u->c->nty;
		uint32_t *st;

		if (b >= lim)
			return false;
		st = ensure(&u->r->stamp[k], &u->r->cstamp[k], lim, false);
		if (st[b] == u->r->gen)
			return false;	/* (two A records for one of B's) */
		st[b] = u->r->gen;
	}
	if (push && (k != W_TY || !u->e->wy[p].intern))
		VPUSH(u->r->uw, (uint64_t)k << 32 | (uint32_t)p);
	return true;
}

static _Atomic uint64_t st_known;
static bool pair(un *u, int k, uint64_t a64, uint64_t b64);

/* pair a record's references that tc_intern did not take as values (types not builtin, refs not 0) */
static bool refs_rest(un *u, const void *a, const void *b, const fd *f, int n)
{
	for (int i = 0; i < n; i++) {
		int k = f[i].kind;
		uint64_t v;

		if (k == K_IMM || k == K_LOC || k == K_SHADOW)
			continue;
		v = rd(a, f[i]);
		if (k == K_TYPE ? QI((uint32_t)v) < u->e->nty_bi : k == K_TIDX ? v < u->e->nty_bi : !v)
			continue;
		if (!pair(u, k, v, rd(b, f[i])))
			return false;
	}
	return true;
}

/* map W member p (kind k) to B's b without comparing it (pair_wp with the position known) */
static bool slot_set(un *u, int k, uint32_t p, uint32_t b)
{
	uint32_t *s = &u->r->slot[k][p], lim, *st;

	if (*s)
		return *s - 1 == b;
	*s = b + 1;
	lim = k == W_SYM ? u->c->nsym : k == W_TAG ? u->c->ntag : u->c->nty;
	if (b >= lim)
		return false;
	st = ensure(&u->r->stamp[k], &u->r->cstamp[k], lim, false);
	if (st[b] == u->r->gen)
		return false;
	st[b] = u->r->gen;
	return true;
}

/* a W tag that B's tag b was already found equal to (tk): pair the references of it, its fields and enumerators */
static bool tag_known(un *u, const wtag *w, uint32_t b, bool *ok)
{
	struct drrec *r = u->r;
	const tag *bt = &u->c->tags[b];
	const drent *e = u->e;
	fd f[24];
	int n;
	bool found;

	if (!w->k || b >= r->ctk || r->tk[b] != w->k)
		return false;
	ADD(st_known, 1);
	*ok = false;
	if (bt->nfield != w->t.nfield || !bt->f != !w->t.f || bt->nenum != w->t.nenum)
		return true;	/* (cannot be: the same values) */
	n = tag_fd(&w->t, f);
	if (!refs_rest(u, &w->t, bt, f, n))
		return true;
	for (uint32_t i = 0; i < w->t.nfield && w->t.f; i++) {
		qty a = e->wf[w->fo + i].ty;

		if (QI(a) >= e->nty_bi && !pair(u, K_TYPE, a, bt->f[i].ty))
			return true;
	}
	if (w->t.nenum) {
		int64_t q = wfind(e, W_LIST, w->t.enums);
		const wlist *l;
		const uint32_t *bl = &u->c->lists[bt->enums];

		if (q < 0 || bt->enums + bt->nenum > u->c->nlist ||
		    !pair_wp(u, W_LIST, w->t.enums, bt->enums, &found, false))
			return true;
		l = &e->wl[q];
		for (uint32_t i = 0; i < l->n; i++) {
			uint32_t sp = e->wvp[l->vo + i];
			qty a;

			if (sp == UINT32_MAX || !slot_set(u, W_SYM, sp, bl[i]))
				return true;
			a = e->ws[sp].s.ty;	/* (the rest of an enumerator: values, or 0) */
			if (QI(a) >= e->nty_bi && !pair(u, K_TYPE, a, u->c->syms[bl[i]].ty))
				return true;
		}
	}
	*ok = true;
	return true;
}

static bool pair(un *u, int k, uint64_t a64, uint64_t b64)
{
	const drent *e = u->e;
	uint32_t a = (uint32_t)a64, b = (uint32_t)b64;
	bool found;

	switch (k) {
	case K_IMM:
		return a64 == b64;
	case K_LOC: case K_SHADOW: case K_PARAMS: case K_SELF:
		return true;
	case K_NODE:
		return !a == !b;
	case K_SYM:
		if (!a || !b)
			return a == b;
		if (!pair_w(u, W_SYM, a, b, &found))
			return false;
		return found || (a < e->nsym_bi && a == b);
	case K_TAG:
		if (!a || !b)
			return a == b;
		if (!pair_w(u, W_TAG, a, b, &found))
			return false;
		return found || (a < e->ntag_bi && a == b);
	case K_TYPE:
		if (QQ(a) != QQ(b))
			return false;
		a = QI(a), b = QI(b);
		/* fallthrough */
	case K_TIDX:
		if (a < e->nty_bi || b < e->nty_bi)
			return a == b;
		if (!pair_w(u, W_TY, a, b, &found))
			return false;
		return found;
	case K_ATTR:
		if (!a || !b)
			return a == b;
		return pair_w(u, W_ATTR, a, b, &found) && found;
	case K_STR1:
		if (!a || !b)
			return a == b;
		a--, b--;
		/* fallthrough */
	case K_STR:
		if (!a || !b)
			return a == b;
		if (!pair_w(u, W_STR, a, b, &found) || !found)
			return false;
		{
			const wstr *w = &e->wst[wfind(e, W_STR, a)];
			const cstr *s = &u->c->strs[b];

			return w->s.n == s->n && w->s.esz == s->esz && w->s.kind == s->kind &&
			       !memcmp(e->wb + w->bo, s->s, (size_t)s->n * s->esz);
		}
	case K_LIST1:
		if (!a || !b)
			return a == b;
		a--, b--;
		/* fallthrough */
	case K_LIST:
		return pair_w(u, W_LIST, a, b, &found) && found;
	default:
		return false;
	}
}

static bool un_rec(un *u, const void *a, const void *b, const fd *f, int n)
{
	for (int i = 0; i < n; i++) {
		if (!pair(u, f[i].kind, rd(a, f[i]), rd(b, f[i]))) {
			return false;
		}
	}
	return true;
}

static bool unify_one(un *u, int k, uint32_t p)
{
	const drent *e = u->e;
	cc *c = u->c;
	uint32_t b = u->r->slot[k][p] - 1;
	fd f[24];
	int n;

	switch (k) {
	case W_SYM: {
		const sym *as = &e->ws[p].s, *bs = &c->syms[b];

		n = sym_fd(as, f);
		return un_rec(u, as, bs, f, n);
	}
	case W_TAG: {
		const wtag *w = &e->wt[p];
		const tag *bt = &c->tags[b];
		bool ok;

		if (tag_known(u, w, b, &ok))
			return ok;
		if (w->k)
			VPUSH(u->r->tdone, (uint64_t)p << 32 | b);	/* (compared in full: remembered if the try succeeds) */

		n = tag_fd(&w->t, f);
		if (!un_rec(u, &w->t, bt, f, n))
			return false;
		if (!w->t.f != !bt->f)
			return false;
		n = field_fd(f);
		for (uint32_t i = 0; i < w->t.nfield && w->t.f; i++)
			if (!un_rec(u, &e->wf[w->fo + i], &bt->f[i], f, n))
				return false;
		return true;
	}
	case W_TY: {
		const wty *w = &e->wy[p];
		const ctype *bt = &c->ty[b];

		n = ty_fd(&w->t, f);
		if (!un_rec(u, &w->t, bt, f, n))
			return false;
		if (!(w->t.flags & TF_ALIGNED) && (w->t.canon != w->a || bt->canon != b))
			return false;
		if (w->t.kind == TY_FUNC)
			for (uint32_t i = 0; i < w->t.n; i++)
				if (!pair(u, K_TYPE, e->wp[w->po + i], c->params[bt->aux + i]))
					return false;
		return true;
	}
	case W_ATTR:
		n = attr_fd(f);
		return un_rec(u, &e->wa[p].r, &c->attr[b], f, n);
	case W_LIST: {
		const wlist *w = &e->wl[p];

		if (b + w->n > c->nlist)
			return false;
		for (uint32_t i = 0; i < w->n; i++)
			if (!pair(u, K_SYM, e->wv[w->vo + i], c->lists[b + i]))
				return false;
		return true;
	}
	}
	return true;
}

static bool validate(cc *c, const drent *e)
{
	struct drrec *r = c->dr;
	un u = { c, e, r };

	if (e->nsym_bi != r->nsym_bi || e->nty_bi != r->nty_bi || e->ntag_bi != r->ntag_bi)
		return false;
	if (++r->gen == 0) {
		for (int k = 0; k < 3; k++)
			if (r->stamp[k])
				memset(r->stamp[k], 0, r->cstamp[k] * sizeof(uint32_t));
		r->gen = 1;
	}
	for (int k = 0; k < W_N; k++)
		ensure(&r->slot[k], &r->cslot[k], e->nw[k] + 1, true);
	r->uw.n = 0;
	r->tdone.n = 0;
	/* first the bindings themselves (bound or not, kind, flags): most variants that fail, fail here */
	for (uint32_t i = 0; i < e->nrd; i++) {
		const drread *d = &e->rd[i];
		uint32_t b = dget(d->ns ? c->tdir : c->odir, d->id);

		if (!d->s != !b || (b && (c->syms[b].kind != d->kind || c->syms[b].flags != d->flags)))
			return false;
	}
	for (uint32_t i = 0; i < e->nrd; i++) {
		const drread *d = &e->rd[i];
		uint32_t b = dget(d->ns ? c->tdir : c->odir, d->id);

		if (!pair(&u, K_SYM, d->s, b))
			return false;
		while (r->uw.n) {
			uint64_t x = r->uw.v[--r->uw.n];

			if (!unify_one(&u, (int)(x >> 32), (uint32_t)x))
				return false;
		}
	}
	/* every member of W reached (types from interning: set by the log) */
	for (int k = 0; k < W_N; k++)
		for (uint32_t i = 0; i < e->nw[k]; i++)
			if (!r->slot[k][i] && !(k == W_TY && e->wy[i].intern))
				return false;
	for (uint32_t i = 0; i < r->tdone.n; i++)
		tk_set(r, (uint32_t)r->tdone.v[i], e->wt[r->tdone.v[i] >> 32].k);
	return true;
}

/* ---------------- replay: apply ---------------- */

static void apply(cc *c, const drent *e, uint32_t baseB)
{
	struct drrec *r = c->dr;
	trx x = { c, e, r, false, false, (int64_t)c->nsym - e->s0, (int64_t)c->ntag - e->t0, (int64_t)c->nnode - e->n0,
		  (int64_t)c->nlist - e->l0, (int64_t)c->nipool - e->i0, (int64_t)c->nfpool - e->f0,
		  (int64_t)c->nstr - e->r0, (int64_t)c->nattr - e->a0, baseB, NULL };
	uint32_t ty0 = c->nty, lb = baseB - e->base;
	fd f[24];
	int n;

	ensure(&r->tymap, &r->ctymap, e->y1 - e->y0 + 1, false);
	/* 1. the type log */
	for (uint32_t i = 0; i < e->nops; i++) {
		const drop *o = &e->ops[i];
		uint32_t b;

		if (o->op == OP_NEW) {
			b = ty_new(c, o->t.kind);
		} else if (o->op == OP_FUNC) {
			r->tpar.n = 0;
			VGROW(r->tpar, o->t.n);
			for (uint32_t k = 0; k < o->t.n; k++)
				r->tpar.v[k] = (qty)tr(&x, K_TYPE, e->opar[o->po + k]);
			b = QI(ty_func(c, (qty)tr(&x, K_TYPE, o->t.base), r->tpar.v, o->t.n, o->t.flags));
		} else {
			ctype t = o->t;

			n = ty_fd(&t, f);
			tr_rec(&x, &t, f, n, 0);
			b = ty_intern(c, &t);
		}
		if (o->a >= e->y0) {
			r->tymap[o->a - e->y0] = b;
		} else if (o->a < e->nty_bi) {
			if (b != o->a) {
				fprintf(stderr, "vcc: VCC_DR: type log gives another builtin type (bug)\n");
				abort();
			}
		} else {
			int64_t p = wfind(e, W_TY, o->a);
			uint32_t *s = &r->slot[W_TY][p];

			if (*s && *s - 1 != b) {
				fprintf(stderr, "vcc: VCC_DR: type log disagrees with W (bug)\n");
				abort();
			}
			*s = b + 1;
		}
	}
	/* 2. own records */
	{
		uint32_t ns = e->s1 - e->s0;
		sym *to = &c->syms[c->nsym];

		memcpy(to, e->syms, ns * sizeof(sym));
		rel_apply(&x, (char *)to, e->rel[RA_SYM], e->nrel[RA_SYM]);
		for (uint32_t i = 0; i < ns; i++) {
			REL_LOC(to[i].loc, lb);
			REL_LOC(to[i].plb, lb);
			REL_LOC(to[i].ple, lb);
		}
	}
	{
		uint32_t nt = e->t1 - e->t0;
		tag *to = &c->tags[c->ntag];

		if (e->nheap) {
			x.heapB = halloc(c, e->nheap + 16);
			x.heapB = (char *)(((uintptr_t)x.heapB + 15) & ~(uintptr_t)15);
			memcpy(x.heapB, e->heap, e->nheap);
			rel_apply(&x, x.heapB, e->rel[RA_HEAP], e->nrel[RA_HEAP]);
			for (uint32_t i = 0; i < e->nfpc; i += 2) {
				field *ff = (field *)(x.heapB + e->fpc[i]);

				for (uint32_t k = 0; k + 1 < e->fpc[i + 1]; k++)
					REL_LOC(ff[k].loc, lb);
			}
		}
		memcpy(to, e->tags, nt * sizeof(tag));
		rel_apply(&x, (char *)to, e->rel[RA_TAG], e->nrel[RA_TAG]);
		for (uint32_t i = 0; i < nt; i++) {
			REL_LOC(to[i].loc, lb);
			if (to[i].f)
				to[i].f = (field *)(x.heapB + ((uintptr_t)to[i].f - 1));
		}
	}
	{
		uint32_t nn = e->n1 - e->n0;
		node *to = &c->nodes[c->nnode];
		pp_span *sp = &c->span[c->nnode];

		memcpy(to, e->nodes, nn * sizeof(node));
		memcpy(sp, e->spans, nn * sizeof(pp_span));
		rel_apply(&x, (char *)to, e->rel[RA_NODE], e->nrel[RA_NODE]);
		for (uint32_t i = 0; i < nn; i++) {
			REL_LOC(to[i].loc, lb);
			REL_LOC(sp[i].b, lb);
			REL_LOC(sp[i].e, lb);
		}
	}
	{
		uint32_t nl = e->l1 - e->l0, *to = &c->lists[c->nlist];

		memcpy(to, e->lists, nl * sizeof(uint32_t));
		rel_apply(&x, (char *)to, e->rel[RA_LIST], e->nrel[RA_LIST]);
	}
	memcpy(&c->ipool[c->nipool], e->ip, (e->i1 - e->i0) * sizeof(i128));
	memcpy(&c->fpool[c->nfpool], e->fp, (e->f1 - e->f0) * sizeof(double));
	{
		uint32_t nr = e->r1 - e->r0;
		cstr *to = &c->strs[c->nstr];

		memcpy(to, e->strs, nr * sizeof(cstr));
		for (uint32_t i = 0; i < nr; i++)
			if (to[i].s)
				to[i].s = x.heapB + ((uintptr_t)to[i].s - 1);
	}
	{
		uint32_t na = e->a1 - e->a0;
		attrs *to = &c->attr[c->nattr];

		memcpy(to, e->attr, na * sizeof(attrs));
		rel_apply(&x, (char *)to, e->rel[RA_ATTR], e->nrel[RA_ATTR]);
	}
	/* 3. the final content of the region's records and enums (and its other ty_new types) */
	for (uint32_t i = 0; i < e->nops; i++)
		if (e->ops[i].op == OP_NEW) {
			uint32_t b = r->tymap[e->ops[i].a - e->y0];
			ctype t = e->fin[i];

			n = ty_fd(&t, f);
			tr_rec(&x, &t, f, n, b);
			c->ty[b] = t;
		}
	(void)ty0;
	c->nsym += e->s1 - e->s0, c->ntag += e->t1 - e->t0, c->nnode += e->n1 - e->n0, c->nlist += e->l1 - e->l0;
	c->nipool += e->i1 - e->i0, c->nfpool += e->f1 - e->f0, c->nstr += e->r1 - e->r0, c->nattr += e->a1 - e->a0;
	/* 4. writes to older records: the fields the region changed */
	for (uint32_t i = 0; i < e->nxs; i++) {
		const xrec *w = &e->xs[i];
		sym *b = &c->syms[r->slot[W_SYM][w->w] - 1];

		n = sym_fd(&w->u.s, f);
		for (int k = 0; k < n; k++)
			if (w->mask & (1u << k))
				wr(b, f[k], tr(&x, f[k].kind, rd(&w->u.s, f[k])));
	}
	for (uint32_t i = 0; i < e->nxt; i++) {
		const xrec *w = &e->xt[i];
		tag *b = &c->tags[r->slot[W_TAG][w->w] - 1];

		tk_set(r, r->slot[W_TAG][w->w] - 1, NULL);
		n = tag_fd(&w->u.t, f);
		for (int k = 0; k < n; k++)
			if (w->mask & (1u << k))
				wr(b, f[k], tr(&x, f[k].kind, rd(&w->u.t, f[k])));
		if (w->mask & XF_F)
			b->f = w->u.t.f ? (field *)(x.heapB + ((uintptr_t)w->u.t.f - 1)) : NULL;
	}
	for (uint32_t i = 0; i < e->nxy; i++) {
		const xrec *w = &e->xy[i];
		ctype *b = &c->ty[r->slot[W_TY][w->w] - 1];

		n = ty_fd(&w->u.y, f);
		for (int k = 0; k < n; k++)
			if (w->mask & (1u << k))
				wr(b, f[k], tr(&x, f[k].kind, rd(&w->u.y, f[k])));
	}
	for (uint32_t i = 0; i < e->nxa; i++) {
		const xrec *w = &e->xa[i];
		attrs *b = &c->attr[r->slot[W_ATTR][w->w] - 1];

		n = attr_fd(f);
		for (int k = 0; k < n; k++)
			if (w->mask & (1u << k))
				wr(b, f[k], tr(&x, f[k].kind, rd(&w->u.r, f[k])));
	}
	/* 5. file-scope bindings, toplevel, the last token */
	for (uint32_t i = 0; i < e->nbind; i++)
		sym_bind(c, (uint32_t)(e->bind[i] + x.ds));
	for (uint32_t i = 0; i < e->ntl; i++)
		c->toplevel[c->ntoplevel++] = (uint32_t)tr(&x, K_SYM, e->tl[i]);
	c->prevloc = (uint32_t)tr(&x, K_LOC, e->prevloc);
	c->prevflags = e->prevflags;
}

/* ---------------- VCC_DRCHECK: replay against parse ---------------- */

static _Atomic uint64_t st_chk, st_chkbad;

static void chk_report(cc *c, const char *what, uint32_t i, int field, uint64_t want, uint64_t got)
{
	const char *nm = "?";
	uint32_t nl = 1, line = 0, col = 0;

	if (atomic_fetch_add(&st_chkbad, 1) > 100000)
		return;
	pp_where(c->p, c->prevloc, &nm, &nl, &line, &col);
	fprintf(stderr, "drcheck: %.*s:%u: %s %u field %d: replay %#llx, parse %#llx\n", (int)nl, nm, line, what, i,
		field, (unsigned long long)want, (unsigned long long)got);
}

static bool chk_rec(cc *c, trx *x, const char *what, uint32_t i, const void *a, const void *b, const fd *f, int n,
		    uint32_t self)
{
	for (int k = 0; k < n; k++) {
		uint64_t want, got;

		if (f[k].kind == K_SHADOW)
			continue;
		want = f[k].kind == K_SELF ? self : tr(x, f[k].kind, rd(a, f[k]));
		got = rd(b, f[k]);
		if (x->bad) {
			char w[64];

			snprintf(w, sizeof(w), "unmapped %s.%d(kind %d)", what, k, f[k].kind);
			chk_report(c, w, i, 0, rd(a, f[k]), got);
			x->bad = false;
			return false;
		}
		if (want != got) {
			chk_report(c, what, i, k, want, got);
			return false;
		}
	}
	return true;
}

static void drcheck(cc *c)
{
	struct drrec *r = c->dr;
	const drent *e = r->chk;
	trx x = { c, e, r, false, false, (int64_t)r->s0 - e->s0, (int64_t)r->t0 - e->t0, (int64_t)r->n0 - e->n0,
		  (int64_t)r->l0 - e->l0, (int64_t)r->i0 - e->i0, (int64_t)r->f0 - e->f0, (int64_t)r->st0 - e->r0,
		  (int64_t)r->a0 - e->a0, r->chk_base, NULL };
	fd f[24];
	int n;
	bool ok = true;

	ADD(st_chk, 1);
#define CNT(what, a, b)                                                    \
	if ((uint64_t)(a) != (uint64_t)(b)) {                              \
		chk_report(c, "count " what, 0, 0, (a), (b));              \
		return;                                                    \
	}
	CNT("syms", e->s1 - e->s0, c->nsym - r->s0);
	CNT("tags", e->t1 - e->t0, c->ntag - r->t0);
	CNT("nodes", e->n1 - e->n0, c->nnode - r->n0);
	CNT("lists", e->l1 - e->l0, c->nlist - r->l0);
	CNT("ipool", e->i1 - e->i0, c->nipool - r->i0);
	CNT("fpool", e->f1 - e->f0, c->nfpool - r->f0);
	CNT("strs", e->r1 - e->r0, c->nstr - r->st0);
	CNT("attrs", e->a1 - e->a0, c->nattr - r->a0);
	CNT("ops", e->nops, r->ops.n);
	CNT("toplevel", e->ntl, c->ntoplevel - r->tl0);
#undef CNT
	ensure(&r->tymap, &r->ctymap, e->y1 - e->y0 + 1, false);
	for (uint32_t i = 0; i < e->nops; i++) {
		const drop *o = &e->ops[i];
		uint32_t b = r->ops.v[i].a;

		if (o->op != r->ops.v[i].op) {
			chk_report(c, "op kind", i, 0, o->op, r->ops.v[i].op);
			return;
		}
		if (o->a >= e->y0)
			r->tymap[o->a - e->y0] = b;
		else if (o->a < e->nty_bi) {
			if (b != o->a) {
				chk_report(c, "op builtin type", i, 0, o->a, b);
				return;
			}
		} else {
			int64_t p = wfind(e, W_TY, o->a);
			uint32_t *sl = &r->slot[W_TY][p];

			if (*sl && *sl - 1 != b) {
				chk_report(c, "op ext type", i, 0, *sl - 1, b);
				return;
			}
			*sl = b + 1;
		}
	}
	for (uint32_t i = 0; ok && i < e->s1 - e->s0; i++) {
		n = sym_fd(&e->syms[i], f);
		ok = chk_rec(c, &x, "sym", r->s0 + i, &e->syms[i], &c->syms[r->s0 + i], f, n, 0);
	}
	for (uint32_t i = 0; ok && i < e->t1 - e->t0; i++) {
		const tag *a = &e->tags[i], *b = &c->tags[r->t0 + i];

		n = tag_fd(a, f);
		ok = chk_rec(c, &x, "tag", r->t0 + i, a, b, f, n, 0);
		if (ok && !a->f != !b->f) {
			chk_report(c, "tag f", r->t0 + i, 0, !!a->f, !!b->f);
			ok = false;
		}
		if (ok && a->f) {
			const field *af = (const field *)(e->heap + ((uintptr_t)a->f - 1));

			n = field_fd(f);
			for (uint32_t k = 0; ok && k < a->nfield; k++)
				ok = chk_rec(c, &x, "field", k, &af[k], &b->f[k], f, n, 0);
		}
	}
	for (uint32_t i = 0; ok && i < e->n1 - e->n0; i++) {
		pp_span sp = e->spans[i];

		n = node_fd(&e->nodes[i], f);
		ok = chk_rec(c, &x, "node", r->n0 + i, &e->nodes[i], &c->nodes[r->n0 + i], f, n, 0);
		if (ok && x.bad) {
			chk_report(c, "unmapped node", i, 0, 0, 0);
			ok = false;
		}
		if (ok && ((uint32_t)tr(&x, K_LOC, sp.b) != c->span[r->n0 + i].b ||
			   (uint32_t)tr(&x, K_LOC, sp.e) != c->span[r->n0 + i].e)) {
			chk_report(c, "span", r->n0 + i, 0, 0, 0);
			ok = false;
		}
	}
	for (uint32_t i = 0; ok && i < e->l1 - e->l0; i++) {
		uint32_t want = e->lk[i] == K_IMM || e->lk[i] == LK_DEAD ? e->lists[i] : (uint32_t)tr(&x, e->lk[i], e->lists[i]);

		if (x.bad) {
			chk_report(c, "unmapped list elem", i, e->lk[i], e->lists[i], c->lists[r->l0 + i]);
			ok = false;
			break;
		}
		if (e->lk[i] != LK_DEAD && want != c->lists[r->l0 + i]) {
			chk_report(c, "list", r->l0 + i, e->lk[i], want, c->lists[r->l0 + i]);
			ok = false;
		}
	}
	for (uint32_t i = 0; ok && i < e->r1 - e->r0; i++) {
		const cstr *a = &e->strs[i], *b = &c->strs[r->st0 + i];

		if (a->n != b->n || a->esz != b->esz || a->kind != b->kind ||
		    memcmp(e->heap + ((uintptr_t)a->s - 1), b->s, (size_t)a->n * a->esz)) {
			chk_report(c, "str", r->st0 + i, 0, 0, 0);
			ok = false;
		}
	}
	n = attr_fd(f);
	for (uint32_t i = 0; ok && i < e->a1 - e->a0; i++)
		ok = chk_rec(c, &x, "attr", r->a0 + i, &e->attr[i], &c->attr[r->a0 + i], f, n, 0);
	for (uint32_t i = 0; ok && i < e->nops; i++)
		if (e->ops[i].op == OP_NEW) {
			uint32_t b = r->tymap[e->ops[i].a - e->y0];

			n = ty_fd(&e->fin[i], f);
			ok = chk_rec(c, &x, "type", b, &e->fin[i], &c->ty[b], f, n, b);
		}
	for (uint32_t i = 0; ok && i < e->ntl; i++)
		if ((uint32_t)tr(&x, K_SYM, e->tl[i]) != c->toplevel[r->tl0 + i]) {
			chk_report(c, "toplevel", i, 0, tr(&x, K_SYM, e->tl[i]), c->toplevel[r->tl0 + i]);
			ok = false;
		}
	for (uint32_t i = 0; ok && i < e->nxs; i++) {
		const xrec *w = &e->xs[i];
		const sym *b = &c->syms[r->slot[W_SYM][w->w] - 1];

		n = sym_fd(&w->u.s, f);
		for (int k = 0; ok && k < n; k++)
			if (w->mask & (1u << k)) {
				uint64_t want = tr(&x, f[k].kind, rd(&w->u.s, f[k]));

				if (want != rd(b, f[k])) {
					chk_report(c, "sym write", r->slot[W_SYM][w->w] - 1, k, want, rd(b, f[k]));
					ok = false;
				}
			}
	}
	for (uint32_t i = 0; ok && i < e->nxt; i++) {
		const xrec *w = &e->xt[i];
		const tag *b = &c->tags[r->slot[W_TAG][w->w] - 1];

		n = tag_fd(&w->u.t, f);
		for (int k = 0; ok && k < n; k++)
			if (w->mask & (1u << k)) {
				uint64_t want = tr(&x, f[k].kind, rd(&w->u.t, f[k]));

				if (want != rd(b, f[k])) {
					chk_report(c, "tag write", r->slot[W_TAG][w->w] - 1, k, want, rd(b, f[k]));
					ok = false;
				}
			}
	}
	for (uint32_t i = 0; ok && i < e->nxy; i++) {
		const xrec *w = &e->xy[i];
		const ctype *b = &c->ty[r->slot[W_TY][w->w] - 1];

		n = ty_fd(&w->u.y, f);
		for (int k = 0; ok && k < n; k++)
			if (w->mask & (1u << k)) {
				uint64_t want = tr(&x, f[k].kind, rd(&w->u.y, f[k]));

				if (want != rd(b, f[k])) {
					chk_report(c, "type write", r->slot[W_TY][w->w] - 1, k, want, rd(b, f[k]));
					ok = false;
				}
			}
	}
	if (ok && x.bad)
		chk_report(c, "unmapped", 0, 0, 0, 0);
}

/* ---------------- boundaries ---------------- */

/* at a file-scope declaration boundary, before the declaration cache: a recording whose region ends here */
VPRIV void dr_close(cc *c)
{
	struct drrec *r = c->dr;
	uint64_t pos, b;
	uint32_t n;
	const pp_hitreg *h;
	const void *he;

	if (!r->open)
		return;
	if (c->ntk != 1) {
		rec_stop(c);
		count(S_ABANDON, 0);
		return;
	}
	pos = pp_ntok(c->p) - 1;
	h = pp_hit_regions(c->p, &n);
	for (uint32_t i = n; i-- > 0 && h[i].b >= r->b;)
		if (h[i].b == r->b) {
			if (h[i].e == pos) {
				uint64_t t0 = ticks();

				rec_close(c, pos);
				ADD(tk_close, ticks() - t0);
			} else {
				if (!r->chk)
					never_note(r->he, r->ctr, r->pack, opt_sig(c->o), 1);
				rec_stop(c);
				count(S_ABANDON, h[i].e - r->b);
			}
			return;
		}
	if (pp_open_hit(c->p, &b, &he, NULL, NULL, NULL) && b == r->b)
		return;	/* still in it */
	rec_stop(c);
	count(S_ABANDON, 0);
}

/* after the declaration cache: replay a region starting at tk[0], or record it; true if replayed */
VPRIV bool dr_boundary(cc *c)
{
	struct drrec *r = c->dr;
	uint64_t pos, b;
	const void *he;
	uint32_t base, span, pack;
	unsigned ctr;

	if (r->open || c->ntk != 1)
		return false;
	pos = pp_ntok(c->p) - 1;
	if (!pp_open_hit(c->p, &b, &he, &base, &ctr, &span) || b != pos)
		return false;
	pack = pack_key(c);
	bool any = false;
	uint32_t nvar = 0;

	if (pp_skip_hit(c->p, 1)) {
		uint64_t sig = opt_sig(c->o);
		uint32_t h = key_hash(he, ctr, pack, sig);

		for (drent *e = atomic_load_explicit(&dr_tab[h], memory_order_acquire); e;
		     e = atomic_load_explicit(&e->next, memory_order_acquire)) {
			if (e->he != he || e->ctr != ctr || e->pack != pack || e->sig != sig)
				continue;
			any = true;
			nvar++;
			count(S_TRY, 0);
			{
				uint64_t t0 = ticks();
				bool ok = validate(c, e);

				ADD(tk_val, ticks() - t0);
				if (!ok)
					continue;
			}
			count(S_HIT, e->ntok);
			if (dr_mode == 2)
				return false;	/* (VCC_DRREC: would replay; parsed, not recorded) */
			if (dr_check) {
				r->chk = e, r->chk_base = base;
				r->heap0 = (size_t)(c->hp - c->heap);
				rec_open(c, b, he, base, ctr, span);
				return false;
			}
			{
				uint64_t t0 = ticks();

				apply(c, e, base);
				ADD(tk_apply, ticks() - t0);
			}
			pp_skip_hit(c->p, 0);
			cc_fetch(c, &c->tk[0]);
			return true;
		}
		count(any ? S_MISS : S_NOENT, 0);
	} else {
		count(S_NOSKIP, 0);	/* (and not recorded: an enclosing header being recorded changes the parse,
					   e.g. no lazy bodies from the header cache) */
		return false;
	}
	if (nvar >= DR_VARIANTS || never_record(he, ctr, pack, opt_sig(c->o))) {
		count(S_NEVER, 0);
		return false;
	}
	r->heap0 = (size_t)(c->hp - c->heap);
	rec_open(c, b, he, base, ctr, span);
	return false;
}

/* ---------------- per TU ---------------- */

/* every schema field fits rd/wr: at most 8 bytes */
static void schema_check(void)
{
	fd f[24];
	int n, bad = 0;
	sym s = { 0 };
	tag t = { .nenum = 1 };
	ctype y = { 0 };
	node x = { 0 };

	for (int pass = 0; pass < 5; pass++) {
		n = pass == 0 ? sym_fd(&s, f) : pass == 1 ? tag_fd(&t, f) : pass == 2 ? field_fd(f) : pass == 3 ? ty_fd(&y, f)
										   : attr_fd(f);
		for (int i = 0; i < n; i++)
			bad |= f[i].size > 8 || !f[i].size;
	}
	n = node_fd(&x, f);
	for (int i = 0; i < n; i++)
		bad |= f[i].size > 8 || !f[i].size;
	if (bad) {
		fprintf(stderr, "vcc: VCC_DR: a schema field is wider than 8 bytes (bug)\n");
		abort();
	}
}

static void dr_env(void)
{
	const char *m = getenv("VCC_DR_MAX_MB");

	dr_mode = getenv("VCC_DR") ? 1 : getenv("VCC_DRREC") ? 2 : 0;
	dr_check = getenv("VCC_DRCHECK") != NULL;
	if (dr_mode)
		schema_check();
	atomic_store(&dr_max, (uint64_t)(m ? atoll(m) : 512) << 20);
}

VPRIV void dr_begin(cc *c)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, dr_env);
	c->dr = NULL;
	c->dr_on = 0;
	if (!dr_mode || c->o->dump_layouts)
		return;
	c->dr = calloc(1, sizeof(struct drrec));
	c->dr->nsym_bi = c->nsym, c->dr->nty_bi = c->nty, c->dr->ntag_bi = c->ntag;
}

VPRIV void dr_end(cc *c)
{
	struct drrec *r = c->dr;

	if (!r)
		return;
	if (r->open) {
		rec_stop(c);
		count(S_ABANDON, 0);
	}
	free(r->hs), free(r->rds.v), free(r->wm.k), free(r->wm.v);
	free(r->ws.v), free(r->wt.v), free(r->wy.v), free(r->wa.v), free(r->wst.v), free(r->wl.v);
	free(r->wf.v), free(r->wp.v), free(r->wb.v), free(r->wv.v), free(r->work.v);
	free(r->ops.v), free(r->opar.v), free(r->rt), free(r->rf);
	free(r->ssym), free(r->stag), free(r->sty), free(r->sattr);
	for (int k = 0; k < W_N; k++)
		free(r->slot[k]);
	for (int k = 0; k < 3; k++)
		free(r->stamp[k]);
	free(r->tymap), free(r->uw.v), free(r->tpar.v);
	free(r->tk), free(r->tdone.v);
	free(r);
	c->dr = NULL;
	c->dr_on = 0;
}

/* a declaration-cache restore replaced the state: a recording in progress is void */
void dr_restored(cc *c)
{
	if (c->dr && c->dr->ctk)	/* (the tags are the snapshot's) */
		memset(c->dr->tk, 0, c->dr->ctk * sizeof(*c->dr->tk));
	if (c->dr && c->dr->open) {
		c->dr->open = false;	/* (the snapshot's tables are in place: no swap back) */
		c->dr->chk = NULL;
		c->ptr_lo = 1;
		c->dr_on = 0;
		count(S_ABANDON, 0);
	}
}

#define M(x) ((double)atomic_load(&(x)) / 1e6)
#define K(x) ((unsigned long long)atomic_load(&(x)))

void cc_dr_stats(char *buf, size_t n)
{
	int k;

	if (dr_mode <= 0) {
		*buf = 0;
		return;
	}
	k = snprintf(buf, n,
		     "%s: recorded %llu: published %llu (%.1f M tok), not %llu (%.1f M), abandoned %llu; %llu entries, %.1f MB\n"
		     "  at hits: no entry %llu, tried %llu variants, %s %llu (%.1f M tok), no valid variant %llu, no skip %llu,"
		     " not recorded again %llu",
		     dr_mode == 1 ? "decl replay" : "decl replay (record only)", K(st_n[S_OPEN]), K(st_n[S_PUB]),
		     M(st_tok[S_PUB]), K(st_n[S_NOPUB]), M(st_tok[S_NOPUB]), K(st_n[S_ABANDON]), K(st_ents),
		     (double)K(st_bytes) / (1 << 20), K(st_n[S_NOENT]), K(st_n[S_TRY]),
		     dr_mode == 1 ? "replayed" : "valid", K(st_n[S_HIT]), M(st_tok[S_HIT]), K(st_n[S_MISS]),
		     K(st_n[S_NOSKIP]), K(st_n[S_NEVER]));
	if (k > 0 && (size_t)k + 96 < n)
		k += snprintf(buf + k, n - (size_t)k, "\n  tags known equal %llu; %llu tag contents, %.1f MB", K(st_known), K(tc_n),
			      (double)K(tc_bytes) / (1 << 20));
	if (k > 0 && (size_t)k + 96 < n)
		k += snprintf(buf + k, n - (size_t)k, "\n  ms (24 MHz ticks, all threads): validate %.0f, apply %.0f, publish %.0f, W %.0f",
			      K(tk_val) / 24e3, K(tk_apply) / 24e3, K(tk_close) / 24e3, K(tk_w) / 24e3);
	if (dr_check && dr_mode == 1 && k > 0 && (size_t)k + 64 < n)
		k += snprintf(buf + k, n - (size_t)k, "\n  drcheck: %llu regions compared, %llu mismatches",
			      K(st_chk), K(st_chkbad));
	if (k > 0 && (size_t)k + 32 < n)
		k += snprintf(buf + k, n - (size_t)k, "\n  not published:");
	for (int i = 0; i < F_N && k > 0 && (size_t)k + 48 < n; i++)
		if (K(st_fail[i]))
			k += snprintf(buf + k, n - (size_t)k, " %s %llu (%.1f M)", fname[i], K(st_fail[i]), M(st_failtok[i]));
}
