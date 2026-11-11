/*
 * vcc code generator for WebAssembly (wasm64, PLAN-wasm.md): shared state
 * of arch/wasm/emit/.  Straight from the AST to LLVM's wasm assembly
 * syntax (what clang -S --target=wasm64-unknown-emscripten writes): one
 * wasm value per C expression on the operand stack, scalar locals whose
 * address is never taken in wasm locals, the rest in a frame on the shadow
 * stack (__stack_pointer), structured control flow from C's.
 *
 * Part of the emit module (emit_SRC): its statics share the unity TU with
 * arch/a64/emit/, so every name here starts with w.
 */
#ifndef VCC_WIMPL_H
#define VCC_WIMPL_H
#include "../../../emit/emitimpl.h"

/* wasm value types; WT_MEM: a C value in memory, its address (WPT) on the stack */
enum { WT_NONE, WT_I32, WT_I64, WT_F32, WT_F64, WT_MEM };

/* where a local lives */
enum { WB_NONE, WB_LOC, WB_FRAME, WB_PTR };	/* WB_PTR: the wasm local holds the object's address */

typedef struct wbind {
	uint32_t gen;		/* valid if == W->gen */
	uint8_t k;
	uint32_t loc;		/* WB_LOC / WB_PTR: local index */
	uint64_t off;		/* WB_FRAME: offset from the frame base */
} wbind;

/* an lvalue: WL_LOC a wasm local; WL_MEM an address on the stack plus off */
enum { WL_LOC, WL_MEM };
typedef struct wlv {
	uint8_t k, bf, sgn;
	uint16_t bitoff, width;	/* bit-field: bits from the byte at off */
	uint32_t loc;
	uint64_t off;
	bool vol;
} wlv;

/* a switch case: its range and segment */
typedef struct wswc { i128 lo, hi; uint32_t seg; bool dflt; } wswc;

typedef struct wst {
	tbuf b;			/* the function's body */
	tbuf fn;		/* the function assembled */
	tbuf sig;		/* scratch: a call's signature */
	tbuf decl;		/* functions referenced: "name\tsig\n", for the header's .functype lines */
	uint8_t *lt;		/* local types (WT_I32 ..) */
	uint32_t nloc, nparam;
	uint32_t *fl[5];	/* free temporaries per type */
	uint32_t nfl[5];
	wbind *sb;		/* per sym */
	uint32_t gen;
	uint64_t frame;		/* frame bytes so far */
	uint32_t falign;
	uint32_t fp, sp0, rv, vap, sret;	/* locals (UINT32_MAX: none) */
	bool dyn;		/* alloca / VLA: __stack_pointer moves during the function */
	int depth;		/* open blocks, loops and ifs */
	int brk, cont;		/* innermost targets: block index, -1 none */
	qty rty;
	int rt;			/* the wasm result: WT_NONE for void and sret */
	uint32_t fsym;
	wswc *sw;		/* switch case scratch */
	uint32_t nsw;
	uint32_t *seg;		/* switch segments: first statement index */
	uint32_t nseg;
	/* goto: compound statements that are jumped into (a dispatch loop each) */
	struct wgd *gd;
	uint32_t ngd;
	uint64_t *gdx;		/* node << 32 | gd index, sorted */
	uint32_t ngdx;
	struct wgp *gp;		/* (gd, statement index) pairs: segment starts, labels */
	uint32_t ngp;		/* segment starts; the labels follow, sorted by label */
	uint32_t ngl;
	uint32_t *vszn, *vszl;	/* VLA sizes fixed: size expression node, its i64 local */
	uint32_t nvsz;
	struct wvb *vb;		/* blocks with VLAs being emitted: sp to restore on leaving */
	uint32_t nvb;
	struct wvb *cl;		/* variables with a cleanup in scope: block depth, sym (.sp) */
	uint32_t ncl;
	int bdepth;		/* the depth at the start of the innermost compound statement */
	uint32_t *sj;		/* setjmp call sites (nodes) of the function (-mllvm -wasm-enable-sjlj) */
	uint32_t nsj;
	int sjr;		/* scan: the statement's record (-1: none, -2: a loop's condition) */
	uint32_t sji;		/* ... and its position there */
	uint32_t sjl, sjv, sjsp, sjp;	/* the site to resume (0: none), the value, sp, the payload */
	uint64_t sjfid;		/* the frame slot whose address identifies this invocation */
	int sjloop;
	uint32_t notop;		/* a compound only entered by goto (the dead branch of if (0)) */
	uint32_t sxv, sxt;	/* the statement expression's value statement, its local */
	struct wst *next;	/* the free list */
} wst;

typedef struct wvb { int depth; uint32_t sp, first; } wvb;	/* first: the first VLA statement */

/* a compound statement goto can enter */
typedef struct wgd {
	uint32_t node;
	int parent;		/* gd it is in (a compound, if or loop), -1: none */
	uint32_t pidx;		/* its statement index there; in an if 1 then, 2 else */
	int op;			/* N_BLOCK, N_IF, N_WHILE, N_DO, N_FOR */
	uint32_t seg, nseg;	/* segment starts: gp[seg ..] (.i) */
	uint32_t st;		/* the state local (i32): segment to start in; if: branch; loop: skip the test */
	int loop;		/* block index of the dispatch loop while emitted, -1 */
	bool need;
} wgd;

/* label (or 0 for a plain segment start), gd, statement index */
typedef struct wgp { uint32_t label, d, i; } wgp;
#define WG_CASE (1u << 31)	/* label: a case label (node) in a nested statement */
#define WG_SJ (1u << 30)	/* label: the statement of setjmp call site N (W->sj[N]) */
#define WMAXGD ((size_t)1 << 22)

#define WMAXLOC ((size_t)1 << 22)
#define WMAXSW ((size_t)1 << 22)

static const char *const wt_name[] = { "", "i32", "i64", "f32", "f64", "i64" };

/* the wasm type of an address: i32 for wasm32, i64 for wasm64 */
#define WPT(c) (WASM32(c) ? WT_I32 : WT_I64)

/* ---------------- types ---------------- */

/* the wasm type of a C value of type t */
static inline int wty(cc *c, qty t)
{
	const ctype *y = T(c, t);

	switch (y->kind) {
	case TY_VOID:
		return WT_NONE;
	case TY_FLOAT:
		return WT_F32;
	case TY_DOUBLE:
		return WT_F64;
	case TY_PTR:
	case TY_FUNC:
		return WPT(c);
	default:
		if (k_int(y->kind))
			return y->size <= 4 ? WT_I32 : y->size == 8 ? WT_I64 : WT_MEM;
		return WT_MEM;
	}
}

/* its representation on the operand stack */
static inline int wrep(cc *c, int t) { return t == WT_MEM ? WPT(c) : t; }

/* ---------------- printing ---------------- */

#define WB_ (&e->w->b)

static inline void wi(ecx *e, const char *s)
{
	tbuf *b = WB_;

	tb_c(b, '\t');
	tb_s(b, s);
	tb_c(b, '\n');
}

static inline void wiu(ecx *e, const char *s, uint64_t v)
{
	tbuf *b = WB_;

	tb_c(b, '\t');
	tb_s(b, s);
	tb_c(b, '\t');
	tb_u(b, v);
	tb_c(b, '\n');
}

static inline void wii(ecx *e, const char *s, int64_t v)
{
	tbuf *b = WB_;

	tb_c(b, '\t');
	tb_s(b, s);
	tb_c(b, '\t');
	tb_i(b, v);
	tb_c(b, '\n');
}

/* "<type>.<op>" */
static inline void wop(ecx *e, int t, const char *op)
{
	tbuf *b = WB_;

	tb_c(b, '\t');
	tb_s(b, wt_name[wrep(e->c, t)]);
	tb_c(b, '.');
	tb_s(b, op);
	tb_c(b, '\n');
}

static inline void wconst(ecx *e, int t, int64_t v)
{
	if (t == WT_I32)
		wii(e, "i32.const", (int32_t)v);
	else
		wii(e, "i64.const", v);
}

static inline void wfconst(ecx *e, int t, double d)
{
	if (t == WT_F32) {
		float f = (float)d;
		uint32_t u;

		memcpy(&u, &f, 4);
		wii(e, "i32.const", (int32_t)u);
		wi(e, "f32.reinterpret_i32");
	} else {
		uint64_t u;

		memcpy(&u, &d, 8);
		wii(e, "i64.const", (int64_t)u);
		wi(e, "f64.reinterpret_i64");
	}
}

/* an address operation: i32.<op> for wasm32, i64.<op> for wasm64 */
static inline void wpop(ecx *e, const char *op) { wop(e, WPT(e->c), op); }
static inline void wpconst(ecx *e, int64_t v) { wconst(e, WPT(e->c), v); }

/* i64.const ref+off (i32.const for wasm32): the address of a symbol */
static inline void waddr_ref(ecx *e, uint32_t ref, int64_t off)
{
	tbuf *b = WB_;

	claim_ref(e, ref);
	tb_s(b, WASM32(e->c) ? "\ti32.const\t" : "\ti64.const\t");
	ref_off(e->c, b, ref, off);
	tb_c(b, '\n');
}

/* ---------------- locals ---------------- */

static inline uint32_t wnewloc(ecx *e, int t)
{
	wst *W = e->w;

	if (W->nloc >= WMAXLOC)
		efatal(e, e->curloc, "wasm: too many locals");
	W->lt[W->nloc] = (uint8_t)wrep(e->c, t);
	return W->nloc++;
}

/* a temporary local (wfree gives it back) */
static inline uint32_t wtmp(ecx *e, int t)
{
	wst *W = e->w;

	t = wrep(e->c, t);
	if (W->nfl[t])
		return W->fl[t][--W->nfl[t]];
	return wnewloc(e, t);
}

static inline void wfree(ecx *e, uint32_t l)
{
	wst *W = e->w;
	int t = W->lt[l];

	W->fl[t][W->nfl[t]++] = l;
}

static inline void wget(ecx *e, uint32_t l) { wiu(e, "local.get", l); }
static inline void wset(ecx *e, uint32_t l) { wiu(e, "local.set", l); }
static inline void wtee(ecx *e, uint32_t l) { wiu(e, "local.tee", l); }

/* ---------------- frame ---------------- */

static inline uint32_t wfp(ecx *e)
{
	wst *W = e->w;

	if (W->fp == UINT32_MAX)
		W->fp = wnewloc(e, WPT(e->c));
	return W->fp;
}

static inline uint64_t wslot(ecx *e, uint64_t size, uint32_t align)
{
	wst *W = e->w;
	uint64_t o;

	if (align < 1)
		align = 1;
	wfp(e);
	if (align > W->falign)
		W->falign = align;
	o = (W->frame + align - 1) & ~(uint64_t)(align - 1);
	W->frame = o + (size ? size : 1);
	return o;
}

/* push the address of a frame slot */
static inline void wslot_addr(ecx *e, uint64_t off)
{
	wget(e, wfp(e));
	if (off) {
		wpconst(e, (int64_t)off);
		wpop(e, "add");
	}
}

/* ---------------- blocks ---------------- */

static inline int wblock(ecx *e, const char *kind)
{
	wi(e, kind);
	return e->w->depth++;
}

static inline void wend(ecx *e, const char *kind)
{
	e->w->depth--;
	wi(e, kind);
}

static inline void wbr(ecx *e, const char *op, int target)
{
	wiu(e, op, (uint64_t)(e->w->depth - 1 - target));
}

/* the C ABI (clang's WebAssemblyABIInfo): how a value is passed */
enum { WA_IGNORE, WA_SCALAR, WA_IND, WA_PAIR };
/*
 * WA_SCALAR: wasm type st, loaded as C type ft; WA_PAIR: n parts of wasm type
 * st (an __int128 or long double: two i64; a vector: its lanes), ft the type
 */
typedef struct wabi { uint8_t k, st; qty ft; uint32_t n; } wabi;

/* the wasm values an argument of class a takes */
static inline uint32_t wparts(wabi a) { return a.k == WA_PAIR ? a.n : a.k == WA_IGNORE ? 0 : 1; }

/* a WA_PAIR part's C type */
static inline qty wpart_ty(cc *c, wabi a) { return is_vec(c, a.ft) ? T(c, a.ft)->base : prim(TY_ULLONG); }

/* wasm.c */
VPRIV bool wis_main(cc *c, uint32_t s);
VPRIV void wfname(cc *c, tbuf *b, uint32_t ref);
VPRIV bool wreg_ok(ecx *e, uint32_t s);
VPRIV wabi wclassify(cc *c, qty t);
VPRIV wabi wclassify_ret(cc *c, qty t);
VPRIV void wdecl(ecx *e, uint32_t ref, qty ft, const char *sig);
VPRIV void wsig(ecx *e, tbuf *b, qty ft, uint32_t call, uint32_t def, bool *sret);
VPRIV qty wpromote(cc *c, qty t);
/* wexpr.c */
VPRIV wbind *wlocal(ecx *e, uint32_t s);
VPRIV void wto_ptr(ecx *e, qty t);
VPRIV int wexpr(ecx *e, uint32_t n, bool want);
VPRIV void wtruth(ecx *e, uint32_t n);
VPRIV void wlval(ecx *e, uint32_t n, wlv *l, qty t);
VPRIV void wload(ecx *e, wlv *l, qty t);
VPRIV void wstore(ecx *e, wlv *l, qty t);
VPRIV void wfold_off(ecx *e, wlv *l);
VPRIV void wcopy(ecx *e, uint64_t n);
VPRIV void wst_sxval(ecx *e, uint32_t v);
VPRIV void wsplit(ecx *e);
VPRIV void wconv(ecx *e, qty from, qty to);
VPRIV int w16(cc *c, qty t);
VPRIV void w16arith(ecx *e, int op, qty ty, qty rty);
VPRIV void w16cmp(ecx *e, int op, qty t);
VPRIV void w16conv(ecx *e, qty from, qty to);
VPRIV void warith(ecx *e, int op, qty ty, qty rty);
VPRIV void wcompare(ecx *e, int op, qty t);
/* wcall.c */
VPRIV int wcall(ecx *e, uint32_t n, bool want);
VPRIV int wbuiltin(ecx *e, uint32_t n, bool want);
VPRIV int wconstant_p(ecx *e, uint32_t a);
VPRIV int wsjlj_kind(cc *c, uint32_t n);
VPRIV void wsj_begin(ecx *e);
VPRIV void wsj_end(ecx *e);
/* sdivpow2.c */
VPRIV bool wsdivpow2_ok(const em_sdivpow2_recipe *r);
VPRIV void wsdivpow2(ecx *e, qty ty, const em_sdivpow2_recipe *r);
/* reachability.c: conservative proofs, without state or allocations */
VPRIV bool wlabels(cc *c, uint32_t n);
VPRIV bool wterm(cc *c, uint32_t n);
/* wstmt.c */
VPRIV void wgoto_scan(ecx *e, uint32_t body);
VPRIV void wvla_bytes(ecx *e, qty t);
VPRIV int wknown(ecx *e, uint32_t n);
VPRIV void wvla_fix(ecx *e, qty t, bool pure);
VPRIV void wstmt(ecx *e, uint32_t n);
VPRIV void winit(ecx *e, uint64_t off, qty ty, uint32_t init, bool zeroed);
#endif
