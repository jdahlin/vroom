/* Internal header of the arm64 lowering of VIR (src/arch/a64/vir/): the lowering state and the
 * helpers and prototypes its files share.  One unity TU with codegen, or file by file. */
#ifndef VIR_A64_LOWERING_INTERNAL_H
#define VIR_A64_LOWERING_INTERNAL_H

#include "../../../codegen/emitimpl.h"
#include "../../../codegen/vir/preir.h"
#include "../../../codegen/vir/lowering-driver.h"
#include "vir/layout.h"

/* data several files read: static in the unity TU, defined once (compares.c, function.c) otherwise */
#ifdef VCC_UNITY
#define VLDATA static
#else
#define VLDATA extern
#endif

#define VL_MAXOP 512

typedef struct vlarg { uint8_t reg; uint8_t size; int32_t stk; } vlarg;	/* reg VR_NONE: at stk */

typedef struct vl {
	ecx *e;
	vir_fn *f;
	const vir_alloc_out *o;
	int64_t *soff;		/* per VIR slot: its FRAME offset */
	int64_t *spoff;		/* per spill slot */
	uint32_t *lbl;		/* per block: its label */
	uint32_t *chs;		/* per block: where a branch to it ends up (vl_chase) */
	uint32_t *ord, *opos;	/* the layout: the blocks in the order they are emitted, and each block's place in it (vl_order) */
	const vir_loops *lp;
	uint32_t sp;		/* shrink-wrapping: the save point (vir_alloc_out.spoint), VIR_NOBLK none */
	bool bare;		/* the code being emitted runs in front of it: returns without an epilogue */
	uint8_t *uses;
	uint8_t *lo32;		/* per value: 1 if every use is a trunc or a ret32 return (only the low 32 bits are read) */
	uint32_t retlbl;
	uint32_t ov[VL_MAXOP];	/* the instruction's value operands (vir_oit order, no multi) */
	uint8_t opr[VL_MAXOP];	/* ... and their registers */
	uint32_t nop;
	int fcc;		/* a compare's condition the next instruction reads from the flags, -1 none */
	uint32_t fval;		/* ... that compare */
	int fzr, fzx;		/* ... a test of register fzr (fzx: 64-bit) against 0 instead: cbz, cbnz (-1 none) */
	int fbit;		/* ... of its bit fbit instead (fzr's register): tbz, tbnz (-1 none) */
	uint32_t tlo, thi;	/* (tlo, thi): instructions folded into a branch (vl_tfold) that emit nothing */
	uint32_t cclo, cchi;	/* [cclo, cchi]: compares joined by and/or into one condition (cmp, ccmp ...); cchi 0 none */
	int ccrun;		/* ... the condition the flags hold so far */
	uint32_t pval;		/* a ptradd the next load or store addresses through (0 none) */
	int pbase;		/* ... its base register */
	int64_t poff;		/* ... and offset */
	uint32_t xval;		/* a ptradd the next load or store addresses through as [base, index] (0 none) */
	uint32_t xfirst, xc;	/* the chain vl_cfold folds: its first instruction, its consumer (xc VIR_NOBLK none) */
	struct vlx { uint32_t first, xv; uint8_t kind, opt, sh; int64_t k; } xx;	/* ... vl_cinfo's answer */
	int xbase, xreg;	/* ... its base register, the index's */
	uint8_t xopt, xsh;	/* ... the index's extension (2 uxtw, 3 lsl, 6 sxtw), scaled by the access size */
	uint32_t ei, eend;	/* the block's edits: the next one, the end */
	uint32_t eb;		/* ... the first */
	bool risk;		/* x17 held data across a frame access: the frame must stay small */
	bool tail;		/* a call right before a ret may be a sibling call (vl_notail) */
	bool ret32;		/* the function returns a 32-bit integer: the upper half of x0 is unspecified */
	int64_t fcs[8];		/* the FRAME offsets where d8-d15 are kept (those in o->callee[1]) */
	uint32_t skip;		/* an instruction not to lower (the ret after a sibling call), 0 none */
	uint32_t at;		/* the block whose place in the layout the code is at (a copy of another block's: vl_dup) */
	int dup;		/* ... copies being lowered, one in another: vir_layout_duplicate */
	bool fallok;		/* the code may fall through to the block after at (no edge moves follow) */
	struct { uint32_t l, e, to; } tr[1024];	/* moves of a branch's edges, after the block */
	uint32_t ntr;
	virbuild_lowering drv;	/* the shared driver's inputs (codegen/vir/lowering-driver.h) */
} vl;

struct vlt { int64_t *soff, *spoff; uint32_t *lbl, *chs, *path, *ord, *opos, *hl, *lbf; uint8_t *nf; uint32_t co, cop, chl, cnf, clf; uint8_t *uses, *lo32; uint32_t cs, csp, cl, cch, cpa, cu, c32; };

static inline bool vl_isconst(const vir_fn *f, uint32_t v) { return v && v < f->nconst; }

static inline int64_t vl_k64(const vir_ins *k) { return (int64_t)((uint64_t)k->c << 32 | k->b); }

/* an integer constant's value as the operation's width sees it */
static inline int64_t vl_ival(const vir_ins *k) { return k->ty == VT_I32 ? (int64_t)(int32_t)k->b : vl_k64(k); }

/* ... as the bits of a logical immediate (i32: the low 32) */
static inline uint64_t vl_lval(const vir_ins *k) { return k->ty == VT_I32 ? (uint64_t)k->b : (uint64_t)vl_k64(k); }

static inline int vl_pow2(const vir_ins *k)	/* log2 of a positive power of two, else -1 */
{
	uint64_t v = k->ty == VT_I32 ? (uint64_t)k->b : (uint64_t)vl_k64(k);

	return v && !(v & (v - 1)) ? __builtin_ctzll(v) : -1;
}

/* a multiplier opt.c's fmulk turns into neg, add or sub with a shift (then a shift): -1,
 * (2^n + 1) << s, 2^n - 1; the constant is materialized next to the madd, where it sees it */
static inline bool vl_mulk(const vir_ins *k)
{
	int w = k->ty == VT_I32 ? 32 : 64;
	uint64_t m = w == 64 ? ~0ull : 0xffffffffull, c = (k->ty == VT_I32 ? (uint64_t)k->b : (uint64_t)vl_k64(k)) & m;
	int sh;

	if (!c || c == m)
		return c != 0;
	sh = __builtin_ctzll(c);
	c >>= sh;
	return (c > 1 && !((c - 1) & (c - 2)) && sh < w) || (!sh && c > 3 && !(c & (c + 1)) && 64 - __builtin_clzll(c) < w);
}

static inline bool vl_iconst(const vir_fn *f, uint32_t v) { return vl_isconst(f, v) && f->ins[v].op == VO_ICONST; }

static inline bool vl_zero(const vir_fn *f, uint32_t v)
{
	return vl_isconst(f, v) && (f->ins[v].op == VO_ICONST || f->ins[v].op == VO_FCONST) && !f->ins[v].b && !f->ins[v].c;
}

static inline bool vl_memaddr(const vir_fn *f, uint32_t v)	/* an address a load or store uses directly */
{
	return vl_isconst(f, v) && (f->ins[v].op == VO_SLOTADDR || f->ins[v].op == VO_SYMADDR);
}

static inline int vl_x(const vir_fn *f, uint32_t v) { return f->ins[v].ty != VT_I32; }	/* 64-bit */

/* a load or store of a vector: 16 bytes through a base register, no folded address */
static inline bool vl_vecacc(const vir_fn *f, const vir_ins *m)
{
	return vir_isvec(m->op == VO_LOAD ? m->ty : m->op == VO_STORE ? f->ins[m->b].ty : VT_VOID);
}

static inline int vl_gx(uint8_t r) { return r == VR_SCR0 ? R_X16 : (int)VR_NUM(r); }

static inline int vl_gv(uint8_t r) { return r == VR_SCR1 ? 31 : (int)VR_NUM(r); }

static inline bool vl_fcls(uint8_t r) { return r == VR_SCR1 || (r != VR_SCR0 && r < 128 && VR_CLASS(r)); }

static inline int vl_ssz(const vl *L, uint32_t s) { return L->o->ssz[s] == 4 ? 2 : 3; }

/* a branch's condition: flags (r < 0) or register r against zero (cc EQ or NE) */
typedef struct vlcc { int cc, r, x, bit; } vlcc;	/* bit >= 0: of r (tbz, tbnz) */

/* a folded value: used once, by the next instruction, never in a slot */
static inline bool vl_once(const vl *L, uint32_t v) { return L->uses[v] == 1 && L->o->slot[v] == VIR_NOBLK; }

typedef struct vlx vlx;

VLDATA _Thread_local struct vlt VLT;
VLDATA const uint8_t vl_icc[10];
VLDATA const int8_t vl_fccs[8];
VLDATA const uint8_t vl_swapcc[10];

VPRIV __attribute__((noreturn, cold)) void vl_fail(vl *L, const char *why);
VPRIV void *vl_grow(void *p, uint32_t *cap, uint32_t n, size_t sz);
VPRIV uint32_t vl_assign(bool darwin, const uint32_t *w, vlarg *out, uint32_t max, int32_t *stk);
VPRIV bool vl_bigmem(const vir_fn *f, const vir_ins *in);
VPRIV void vl_regs(vir_regs *r, bool darwin, bool fcs);
VPRIV int vl_ix(vl *L, uint32_t k, int t);
VPRIV int vl_fx(vl *L, uint32_t k);
VPRIV void vl_spill(vl *L, uint8_t src, uint32_t s, int t);
VPRIV void vl_edit(vl *L, const vir_edit *x);
VPRIV void vl_emoves(vl *L, uint32_t g);
VPRIV uint32_t vl_lbl(vl *L, uint32_t t);
VPRIV bool vl_skip(vl *L, uint32_t b);
VPRIV uint32_t vl_fnext(vl *L);
VPRIV uint32_t vl_succ(vl *L, uint32_t b, uint32_t k, uint32_t to);
VPRIV bool vl_plain(vl *L, uint32_t b, uint32_t k);
VPRIV int vl_cmp(vl *L, const vir_ins *in);
VPRIV void vl_fcmp(vl *L, const vir_ins *in);
VPRIV bool vl_chain(vl *L, uint32_t b, uint32_t i);
VPRIV void vl_ccmp(vl *L, const vir_ins *in, bool isor);
VPRIV int vl_cond(vl *L, const vir_ins *in);
VPRIV vlcc vl_brcond(vl *L, const vir_ins *in);
VPRIV void vl_bc(ecx *e, vlcc c, bool inv, uint32_t l);
VPRIV bool vl_kept(const vl *L, uint8_t r, uint32_t upto);
VPRIV bool vl_tfold(vl *L, uint32_t b, uint32_t i);
VPRIV lval vl_addr(vl *L, uint32_t w, int t);
VPRIV void vl_load(vl *L, uint32_t i, const vir_ins *in, uint8_t res);
VPRIV void vl_store(vl *L, const vir_ins *in);
VPRIV void vl_mem(vl *L, const vir_ins *in);
VPRIV const char *vl_notail(vl *L, uint8_t *d);
VPRIV bool vl_sibling(vl *L, uint32_t b, uint32_t i);
VPRIV void vl_call(vl *L, uint32_t b, uint32_t i, const vir_ins *in);
VPRIV void vl_param(vl *L, const vir_ins *in, uint8_t res);
VPRIV void vl_switch(vl *L, uint32_t b, const vir_ins *in);
VPRIV bool vl_pfold(vl *L, uint32_t b, uint32_t i);
VPRIV bool vl_cinfo(const vl *L, uint32_t b, uint32_t c, vlx *x);
VPRIV bool vl_cfold(vl *L, uint32_t b, uint32_t i, uint8_t res);
VPRIV uint32_t vl_chase(vl *L, uint32_t t);
VPRIV void vl_fcsave(vl *L, bool restore, int t);
VPRIV void vl_ret(vl *L);
VPRIV void vl_goto(vl *L, uint32_t b, uint32_t t);
VPRIV bool vl_sibcand(vl *L, uint32_t b, uint32_t i);
VPRIV bool vl_vec(vl *L, const vir_ins *in, uint8_t res);
VPRIV void vl_ins(vl *L, uint32_t b, uint32_t i, uint8_t res);
VPRIV void vl_order(vl *L);
VPRIV void vl_body(vl *L);

#endif
