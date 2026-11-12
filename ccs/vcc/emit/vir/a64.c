/*
 * Allocated VIR to arm64 records (PLAN-vir.md V4, SPEC-vir.md 15.3): the
 * registers arm64 offers the allocator, the fixed operands of calls,
 * parameters and returns, then one pass over the blocks that writes the
 * emitter's records with the allocated registers, the edits and the edges'
 * moves.  finish_function then runs the record clean-ups, lays out the frame
 * and prints or encodes, as for any function.
 *
 *   integer: x0-x15 and x19-x28 (callee-saved); x16 is VR_SCR0 and the
 *            lowering's own scratch, x17 the printer's (large immediates),
 *            x18 the platform's, x29/x30 the frame
 *   float:   v0-v7 and v16-v30, all caller-saved (the prologue saves no
 *            v register); v31 is VR_SCR1
 *
 * Floating-point operations are encoded words (I_WORD), as in fp.c.  A
 * value in a spill slot or a VIR slot is addressed from FRAME, so the frame
 * layout stays finish_function's.  Edge moves of a branch that is not a `br`
 * go into a small block after the branch's own (a label, the moves, a jump).
 */
#include "../emitimpl.h"
#include "vw.h"

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
	uint32_t skip;		/* an instruction not to lower (the ret after a sibling call), 0 none */
	uint32_t at;		/* the block whose place in the layout the code is at (a copy of another block's: vl_dup) */
	int dup;		/* ... copies being lowered, one in another: vl_dupok */
	bool fallok;		/* the code may fall through to the block after at (no edge moves follow) */
	struct { uint32_t l, e, to; } tr[1024];	/* moves of a branch's edges, after the block */
	uint32_t ntr;
} vl;

static _Thread_local struct { int64_t *soff, *spoff; uint32_t *lbl, *chs, *path; uint8_t *uses, *lo32; uint32_t cs, csp, cl, cch, cpa, cu, c32; } VLT;

static __attribute__((noreturn, cold)) void vl_fail(vl *L, const char *why)
{
	L->e->vfail = why;	/* (in ecx: L is not volatile across the jump) */
	_longjmp(L->e->bail, 3);
}

static void *vl_grow(void *p, uint32_t *cap, uint32_t n, size_t sz)
{
	if (n <= *cap)
		return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * sz);
	if (!p)
		abort();
	return p;
}

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

/* ---------------- the ABI ---------------- */

/* where each argument of signature w goes (AAPCS64; Apple: unnamed on the stack, named
 * packed); returns the count, *stk the bytes of stack arguments */
static uint32_t vl_assign(bool darwin, const uint32_t *w, vlarg *out, uint32_t max, int32_t *stk)
{
	uint32_t ng = VSIG_NGROUPS(w[0]), ngr = 0, nfr = 0;
	int32_t s = 0;

	for (uint32_t g = 0; g < ng; g++) {
		uint32_t w0 = w[3 + 2 * g], w1 = w[4 + 2 * g], pc = VG_CLASS(w0, 0), size = VG_SIZE(w1);
		vlarg a = { VR_NONE, 8, 0 };

		if (!size || size > 8)
			size = 8;
		if (!(darwin && (VG_FLAGS(w0) & VG_UNNAMED))) {
			if (pc == VC_INT && ngr < 8)
				a.reg = VR_MAKE(0, ngr++);
			else if (pc != VC_INT && nfr < 8)
				a.reg = VR_MAKE(1, nfr++);
		}
		if (a.reg == VR_NONE) {
			uint32_t al = darwin && !(VG_FLAGS(w0) & VG_UNNAMED) ? size : 8;

			s = (int32_t)(((uint32_t)s + al - 1) & ~(al - 1));
			a.stk = s;
			a.size = (uint8_t)(al == 8 ? 8 : size);
			s += (int32_t)a.size;
		} else {
			a.size = (uint8_t)size;
		}
		if (g < max)
			out[g] = a;
	}
	*stk = s;
	return ng;
}

static uint8_t vl_retreg(uint32_t ty) { return ty == VT_F32 || ty == VT_F64 ? VR_MAKE(1, 0) : VR_MAKE(0, 0); }

#define VL_CALLER0 ((1ull << 16) - 1)				/* x0-x15 */
#define VL_ALLOC0 (VL_CALLER0 | ((1ull << 29) - (1ull << 19)))	/* ... and x19-x28 */
#define VL_ALLOC1 (((1ull << 8) - 1) | ((1ull << 31) - (1ull << 16)))	/* v0-v7, v16-v30 */

static bool vl_bigmem(const vir_fn *f, const vir_ins *in)
{
	uint64_t n = vl_isconst(f, in->c) ? (uint64_t)vl_k64(&f->ins[in->c]) : ~0ull;

	return in->op == VO_MEMCPY ? n > 128 : n > 256;
}

static void vl_fixed(const vir_fn *f, uint32_t v, vir_fix *x, void *ctx)
{
	const vir_ins *in = &f->ins[v];
	bool darwin = ctx != NULL;
	vlarg arg[VIR_FIX_N];
	int32_t stk;

	switch (in->op) {
	case VO_PARAM: {
		uint32_t ng = VSIG_NGROUPS(f->extra[f->sig]);
		vlarg *a = ng <= VL_MAXOP ? __builtin_alloca(sizeof(vlarg) * ng) : NULL;

		if (!a || in->a >= ng)
			return;
		vl_assign(darwin, &f->extra[f->sig], a, ng, &stk);
		x->res = a[in->a].reg;
		return;
	}
	case VO_CALL: {
		uint32_t n = vl_assign(darwin, &f->extra[in->b], arg, VIR_FIX_N - 1, &stk);

		x->nopnd = (uint8_t)(n + 1 < VIR_FIX_N ? n + 1 : VIR_FIX_N);
		for (uint32_t i = 1; i < x->nopnd; i++) {
			x->opnd[i] = arg[i - 1].reg;
			if (arg[i - 1].reg == VR_NONE)
				x->mem |= 1u << i;
		}
		x->fl |= VFX_RESTMEM;
		if (f->ins[in->a].op == VO_SYMADDR && !f->ins[in->a].b && !f->ins[in->a].c)
			x->mem |= 1;
		x->clob[0] = VL_CALLER0;
		x->clob[1] = VL_ALLOC1;
		if (in->ty != VT_VOID && in->ty != VT_MULTI)
			x->res = vl_retreg(in->ty);
		return;
	}
	case VO_RET:
		if (f->extra[in->c]) {
			x->nopnd = 1;
			x->opnd[0] = vl_retreg(f->ins[f->extra[in->c + 1]].ty);
		}
		return;
	case VO_MEMCPY: case VO_MEMSET:
		if (vl_bigmem(f, in)) {
			x->nopnd = 3;
			x->opnd[0] = VR_MAKE(0, 0), x->opnd[1] = VR_MAKE(0, 1), x->opnd[2] = VR_MAKE(0, 2);
			x->clob[0] = VL_CALLER0;
			x->clob[1] = VL_ALLOC1;
			return;
		}
		if (f->ins[in->a].op == VO_SLOTADDR)
			x->mem |= 1;
		if (in->op == VO_MEMCPY && f->ins[in->b].op == VO_SLOTADDR)
			x->mem |= 2;
		if (in->op == VO_MEMSET && vl_zero(f, in->b))
			x->mem |= 2;
		x->mem |= 4;
		return;
	case VO_LOAD: case VO_VA_START:
		if (vl_memaddr(f, in->a))
			x->mem |= 1;
		return;
	case VO_STORE:
		if (vl_memaddr(f, in->a))
			x->mem |= 1;
		if (vl_zero(f, in->b))
			x->mem |= 2;
		return;
	case VO_AND: case VO_OR: case VO_XOR:	/* (a record's logical immediate must be encodable) */
		if (vl_iconst(f, in->b) && logimm_ok(vl_lval(&f->ins[in->b]), in->ty != VT_I32))
			x->mem |= 2;
		return;
	case VO_ICMP:
		if (vl_iconst(f, in->b))
			x->mem |= 2;
		else if (vl_iconst(f, in->a))
			x->mem |= 1;	/* (compared the other way round) */
		return;
	case VO_ADD: case VO_SUB: case VO_PTRADD:
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		if (vl_iconst(f, in->b))
			x->mem |= 2;
		return;
	case VO_MUL:	/* by a power of two: a shift; by a constant fmulk folds: next to the madd */
		if (vl_iconst(f, in->b) && (vl_pow2(&f->ins[in->b]) >= 0 || vl_mulk(&f->ins[in->b])))
			x->mem |= 2;
		return;
	case VO_I2P: case VO_P2I: case VO_TRUNC: case VO_EXT:
		/* copies (or nearly): in the operand's register when it dies here */
		if (in->op != VO_EXT || VIF_W(in->fl) == 3 || (VIF_W(in->fl) == 2 && !(in->fl & VIF_MS)))
			x->fl |= VFX_TIED;
		return;
	default:
		return;
	}
}

static uint8_t vl_opfix[256];

static void vl_regs(vir_regs *r, bool darwin)
{
	static const uint8_t ops[] = { VO_PARAM, VO_CALL, VO_RET, VO_MEMCPY, VO_MEMSET, VO_LOAD, VO_STORE, VO_ADD, VO_SUB,
				       VO_AND, VO_OR, VO_XOR, VO_ICMP, VO_PTRADD, VO_SHL, VO_LSHR, VO_ASHR, VO_MUL,
				       VO_I2P, VO_P2I, VO_TRUNC, VO_EXT };

	for (size_t i = 0; i < sizeof ops; i++)
		vl_opfix[ops[i]] = 1;
	*r = (vir_regs){ 0 };
	r->alloc[0] = VL_ALLOC0;
	r->alloc[1] = VL_ALLOC1;
	r->callee[0] = VL_ALLOC0 & ~VL_CALLER0;
	r->callee[1] = 0;
	r->fixed = vl_fixed;
	r->ctx = darwin ? (void *)1 : NULL;
	r->opfix = vl_opfix;
}

/* ---------------- registers and constants ---------------- */

static inline int vl_gx(uint8_t r) { return r == VR_SCR0 ? R_X16 : (int)VR_NUM(r); }
static inline int vl_gv(uint8_t r) { return r == VR_SCR1 ? 31 : (int)VR_NUM(r); }
static inline bool vl_fcls(uint8_t r) { return r == VR_SCR1 || (r != VR_SCR0 && r < 128 && VR_CLASS(r)); }

static inline int vl_ssz(const vl *L, uint32_t s) { return L->o->ssz[s] == 4 ? 2 : 3; }

/* constant v into general register rd */
static void vl_mat(vl *L, uint32_t v, int rd)
{
	ecx *e = L->e;
	const vir_ins *k = &L->f->ins[v];

	switch (k->op) {
	case VO_ICONST:
		movi(e, 1, rd, k->ty == VT_I32 ? (int64_t)(uint32_t)k->b : vl_k64(k));
		return;
	case VO_FCONST:
		movi(e, 1, rd, vl_k64(k));
		return;
	case VO_SYMADDR:
		addr_of_ref(e, rd, k->a, vl_k64(&(vir_ins){ .b = k->b, .c = k->c }));
		return;
	case VO_SLOTADDR:
		opi(e, I_ADDI, 1, rd, FRAME, L->soff[k->a] + vl_k64(&(vir_ins){ .b = k->b, .c = k->c }));
		return;
	default:
		vl_fail(L, "VIR lowering: a constant it cannot materialize");
	}
}

/* constant v into float register vd, through general register t */
static void vl_matf(vl *L, uint32_t v, int vd, int t)
{
	const vir_ins *k = &L->f->ins[v];
	int d = k->ty == VT_F64;

	if (k->op != VO_FCONST)
		vl_fail(L, "VIR lowering: a float constant that is not fconst");
	if (!k->b && !k->c) {
		word(L->e, f_in(d, vd, ZR));
		return;
	}
	vl_mat(L, v, t);
	word(L->e, f_in(d, vd, t));
}

/* operand k as a general register: its register, else the constant or its slot in t */
static int vl_ix(vl *L, uint32_t k, int t)
{
	uint8_t r = L->opr[k];
	uint32_t v = L->ov[k];

	if (r != VR_NONE)
		return vl_gx(r);
	if (vl_zero(L->f, v))
		return ZR;
	if (vl_isconst(L->f, v)) {
		vl_mat(L, v, t);
		return t;
	}
	if (L->o->slot[v] != VIR_NOBLK) {
		mem(L->e, I_LD, vl_ssz(L, L->o->slot[v]), 0, t, FRAME, L->spoff[L->o->slot[v]]);
		return t;
	}
	vl_fail(L, "VIR lowering: an operand without a location");
}

static int vl_fx(vl *L, uint32_t k)
{
	if (L->opr[k] == VR_NONE)
		vl_fail(L, "VIR lowering: a float operand without a register");
	return vl_gv(L->opr[k]);
}

/* ---------------- moves ---------------- */

static void vl_rmov(vl *L, uint8_t dst, uint8_t src)
{
	if (dst == src)
		return;
	if (vl_fcls(dst) != vl_fcls(src))
		vl_fail(L, "VIR lowering: a move between register classes");
	if (vl_fcls(dst))
		word(L->e, 0x1e604000u | (unsigned)vl_gv(src) << 5 | (unsigned)vl_gv(dst));	/* fmov d, d */
	else
		mov(L->e, 1, vl_gx(dst), vl_gx(src));
}

/* register dst <- spill slot s, through general register t for a float */
static void vl_reload(vl *L, uint8_t dst, uint32_t s, int t)
{
	int sz = vl_ssz(L, s);

	if (vl_fcls(dst)) {
		mem(L->e, I_LD, sz, 0, t, FRAME, L->spoff[s]);
		word(L->e, f_in(sz == 3, vl_gv(dst), t));
	} else {
		mem(L->e, I_LD, sz, 0, vl_gx(dst), FRAME, L->spoff[s]);
	}
}

static void vl_spill(vl *L, uint8_t src, uint32_t s, int t)
{
	int sz = vl_ssz(L, s);

	if (vl_fcls(src)) {
		word(L->e, f_out(sz == 3, t, vl_gv(src)));
		mem(L->e, I_ST, sz, 0, t, FRAME, L->spoff[s]);
	} else {
		mem(L->e, I_ST, sz, 0, vl_gx(src), FRAME, L->spoff[s]);
	}
}

static void vl_remat(vl *L, uint8_t dst, uint32_t v, int t)
{
	if (vl_fcls(dst))
		vl_matf(L, v, vl_gv(dst), t);
	else
		vl_mat(L, v, vl_gx(dst));
}

/* the edits before instruction pos */
static void vl_edits(vl *L, uint32_t *ei, uint32_t end, uint32_t pos)
{
	const vir_alloc_out *o = L->o;

	for (; *ei < end && o->edit[*ei].pos == pos; ++*ei) {
		const vir_edit *x = &o->edit[*ei];

		switch (x->kind) {
		case VE_RELOAD:
			vl_reload(L, x->dst, o->slot[x->v], R_X16);
			break;
		case VE_REMAT:
			vl_remat(L, x->dst, x->v, R_X16);
			break;
		case VE_MOVE:
			vl_rmov(L, x->dst, (uint8_t)x->src);
			break;
		case VE_STORE:
			vl_spill(L, x->src, o->slot[x->v], R_X16);
			break;
		default:
			vl_fail(L, "VIR lowering: swapped operands");
		}
	}
}

/* edge g's sequenced parallel move */
static void vl_emoves(vl *L, uint32_t g)
{
	const vir_alloc_out *o = L->o;
	const vir_aedge *ed = &o->edge[g];
	int t = R_X16;

	for (uint32_t m = 0; m < ed->n; m++) {
		const vir_emove *mv = &o->move[ed->first + m];

		if ((!(mv->dst & VL_SLOT) && mv->dst == VR_SCR0) || (mv->kind != VE_REMAT && !(mv->src & VL_SLOT) && mv->src == VR_SCR0))
			t = R_X17;
	}
	for (uint32_t m = 0; m < ed->n; m++) {
		const vir_emove *mv = &o->move[ed->first + m];
		bool ds = mv->dst & VL_SLOT, ss = mv->src & VL_SLOT;
		uint32_t dn = mv->dst & ~VL_SLOT, sn = mv->src & ~VL_SLOT;

		if (t == R_X17 && (ds || ss || mv->kind == VE_REMAT))
			L->risk = true;
		switch (mv->kind) {
		case VE_REMAT:
			if (ds) {
				vl_mat(L, mv->v, t);
				mem(L->e, I_ST, vl_ssz(L, dn), 0, t, FRAME, L->spoff[dn]);
			} else {
				vl_remat(L, (uint8_t)dn, mv->v, t);
			}
			break;
		case VE_MOVE:
			if (ds && ss) {
				mem(L->e, I_LD, vl_ssz(L, sn), 0, t, FRAME, L->spoff[sn]);
				mem(L->e, I_ST, vl_ssz(L, dn), 0, t, FRAME, L->spoff[dn]);
			} else if (ds) {
				vl_spill(L, (uint8_t)sn, dn, t);
			} else if (ss) {
				vl_reload(L, (uint8_t)dn, sn, t);
			} else {
				vl_rmov(L, (uint8_t)dn, (uint8_t)sn);
			}
			break;
		case VE_XCHG:
			if (ds || ss || vl_fcls((uint8_t)dn) != vl_fcls((uint8_t)sn))
				vl_fail(L, "VIR lowering: an exchange with a slot");
			if (vl_fcls((uint8_t)dn)) {
				unsigned a = (unsigned)vl_gv((uint8_t)dn), b = (unsigned)vl_gv((uint8_t)sn);

				word(L->e, 0x2e201c00u | b << 16 | a << 5 | a);	/* eor a.8b, a.8b, b.8b */
				word(L->e, 0x2e201c00u | a << 16 | b << 5 | b);
				word(L->e, 0x2e201c00u | b << 16 | a << 5 | a);
			} else {
				int a = vl_gx((uint8_t)dn), b = vl_gx((uint8_t)sn);

				op3(L->e, I_EOR, 1, a, a, b);
				op3(L->e, I_EOR, 1, b, b, a);
				op3(L->e, I_EOR, 1, a, a, b);
			}
			break;
		default:
			vl_fail(L, "VIR lowering: an unknown edge move");
		}
	}
}

static bool vl_plain(vl *L, uint32_t b, uint32_t k);
static uint32_t vl_chase(vl *L, uint32_t t);
static void vl_block(vl *L, uint32_t b);

/* a block every branch goes past (vl_chase): not emitted */
static bool vl_skip(vl *L, uint32_t b) { return b && vl_chase(L, b) != b; }

static uint32_t vl_next(vl *L, uint32_t b)	/* the block emitted after b */
{
	while (++b < L->f->nblk && vl_skip(L, b))
		;
	return b;
}

/* the block the code being emitted falls through to (none: UINT32_MAX) */
static uint32_t vl_fnext(vl *L) { return L->fallok ? vl_next(L, L->at) : UINT32_MAX; }

/* the label a branch from block b to its successor k (block to) takes: the block's, or a
 * new one whose moves follow the block */
static uint32_t vl_succ(vl *L, uint32_t b, uint32_t k, uint32_t to)
{
	const vir_alloc_out *o = L->o;
	uint32_t g = o->eidx[o->soff[b] + k];

	if (g == VIR_NOBLK || !o->edge[g].n)
		return L->lbl[vl_chase(L, to)];
	if (L->ntr == sizeof L->tr / sizeof L->tr[0])
		vl_fail(L, "VIR lowering: too many edges with moves from one block");
	L->tr[L->ntr].l = newlabel(L->e);
	L->tr[L->ntr].e = g;
	L->tr[L->ntr].to = to;
	return L->tr[L->ntr++].l;
}

static bool vl_plain(vl *L, uint32_t b, uint32_t k)	/* the edge has no moves */
{
	uint32_t g = L->o->eidx[L->o->soff[b] + k];

	return g == VIR_NOBLK || !L->o->edge[g].n;
}

/* ---------------- instructions ---------------- */

static const uint8_t vl_icc[10] = { C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE, C_LO, C_LS, C_HI, C_HS };
static const int8_t vl_fccs[8] = { C_EQ, C_NE, C_MI, C_LS, C_GT, C_GE, C_VS, -1 };

static const uint8_t vl_swapcc[10] = { 0, 1, 4, 5, 2, 3, 8, 9, 6, 7 };	/* icmp codes, operands swapped */

static int vl_cmp(vl *L, const vir_ins *in)	/* the flags for an icmp; its code (swapped if the operands were) */
{
	ecx *e = L->e;
	int x = vl_x(L->f, in->a), a;

	if (L->opr[0] == VR_NONE && vl_iconst(L->f, in->a) && L->opr[1] != VR_NONE) {
		opi(e, I_SUBSI, x, ZR, vl_gx(L->opr[1]), vl_ival(&L->f->ins[in->a]));
		return vl_swapcc[in->fl & 15];
	}
	a = vl_ix(L, 0, R_X16);
	if (L->opr[1] == VR_NONE && vl_iconst(L->f, in->b)) {
		int64_t k = vl_ival(&L->f->ins[in->b]);

		opi(e, I_SUBSI, x, ZR, a, k);
	} else {
		op3(e, I_SUBS, x, ZR, a, vl_ix(L, 1, R_X17));
	}
	return in->fl & 15;
}

static void vl_fcmp(vl *L, const vir_ins *in)
{
	int d = L->f->ins[in->a].ty == VT_F64;

	word(L->e, 0x1e202000u | (unsigned)d << 22 | (unsigned)vl_fx(L, 1) << 16 | (unsigned)vl_fx(L, 0) << 5);
}

/* the flags of a condition value (operand 0): the fused compare's, else `cmp w, #0` and NE */
static int vl_cond(vl *L, const vir_ins *in)
{
	if (L->fcc >= 0 && L->fval == in->a) {
		int c = L->fcc;

		L->fcc = -1;
		return c;
	}
	opi(L->e, I_SUBSI, vl_x(L->f, in->a), ZR, vl_ix(L, 0, R_X16), 0);	/* (a condition of either width) */
	return C_NE;
}

/* a branch's condition: flags (r < 0) or register r against zero (cc EQ or NE) */
typedef struct vlcc { int cc, r, x, bit; } vlcc;	/* bit >= 0: of r (tbz, tbnz) */

static vlcc vl_brcond(vl *L, const vir_ins *in)
{
	if (L->fcc >= 0 && L->fval == in->a) {
		vlcc c = { L->fcc, L->fzr, L->fzx, L->fbit };

		L->fcc = -1, L->fzr = -1, L->fbit = -1;
		return c;
	}
	return (vlcc){ C_NE, vl_ix(L, 0, R_X16), vl_x(L->f, in->a), -1 };
}

static void vl_bc(ecx *e, vlcc c, bool inv, uint32_t l)
{
	int cc = inv ? c.cc ^ 1 : c.cc;

	if (c.r >= 0 && c.bit >= 0)
		tbz(e, cc == C_NE, c.r, c.bit, l);
	else if (c.r >= 0)
		cbz(e, cc == C_NE, c.x, c.r, l);
	else
		bcond(e, cc, l);
}

/* register r (VIR's) is still there at instruction upto: no edit after the current
 * instruction and up to upto writes it (x16, the edits' scratch: no edit at all) */
static bool vl_kept(const vl *L, uint8_t r, uint32_t upto)
{
	for (uint32_t x = L->ei; x < L->eend && L->o->edit[x].pos <= upto; x++)
		if (L->o->edit[x].dst == r || vl_gx(r) == R_X16)
			return false;
	return true;
}

/* instruction j of block b is used once, by j + 1, and not spilled */
static bool vl_once1(const vl *L, uint32_t b, uint32_t j)
{
	return L->uses[j] == 1 && L->o->slot[j] == VIR_NOBLK && j + 1 < L->f->blk[b].first + L->f->blk[b].n &&
	       L->f->ins[j + 1].a == j;
}

/*
 * A test of bits feeding the branch: [lshr/ashr by a constant,] and with a constant[,
 * trunc][, icmp.eq/ne 0], brif, each used once by the next: tbz/tbnz on the first
 * operand's register for one bit, else tst and b.ne/b.eq (as LLVM).  Lowered at the
 * chain's first instruction i; the others up to the brif leave nothing.
 */
static bool vl_tfold(vl *L, uint32_t b, uint32_t i)
{
	const vir_fn *f = L->f;
	const vir_ins *in = &f->ins[i];
	uint32_t j = i;
	int x = in->ty != VT_I32, cc = C_NE, k = 0;
	uint64_t wm = x ? ~0ull : 0xffffffffull, m, tm = ~0ull;

	if (L->opr[0] == VR_NONE || !vl_once1(L, b, i))
		return false;
	if (in->op == VO_LSHR || in->op == VO_ASHR) {	/* (a constant in a register too: the immediate) */
		if (!vl_iconst(f, in->b) || (k = (int)vl_ival(&f->ins[in->b])) <= 0 || k >= (x ? 64 : 32))
			return false;
		if (!vl_once1(L, b, ++j))
			return false;
	}
	if (f->ins[j].op != VO_AND || !vl_iconst(f, f->ins[j].b) || (f->ins[j].ty != VT_I32) != (x != 0))
		return false;
	m = vl_lval(&f->ins[f->ins[j].b]) & wm;
	j++;
	if (f->ins[j].op == VO_TRUNC && f->ins[j].ty == VT_I32 && vl_once1(L, b, j))
		tm = 0xffffffffull, j++;
	if (f->ins[j].op == VO_ICMP && (f->ins[j].fl & 15) <= 1 && vl_iconst(f, f->ins[j].b) &&
	    !vl_ival(&f->ins[f->ins[j].b]) && vl_once1(L, b, j))
		cc = f->ins[j].fl & 15 ? C_NE : C_EQ, j++;
	if (f->ins[j].op != VO_BRIF || f->ins[j].a != j - 1)
		return false;
	m &= tm;
	if (in->op == VO_ASHR && k && (m >> ((x ? 64 : 32) - k)))
		return false;	/* (it would test copies of the sign) */
	m = (m << k) & wm;
	if (!m || !logimm_ok(m, x))
		return false;
	L->fcc = cc;
	L->fval = j - 1;
	L->fzr = -1, L->fbit = -1;
	L->tlo = i, L->thi = j;
	if (!(m & (m - 1)) && vl_kept(L, L->opr[0], j)) {
		L->fzr = vl_gx(L->opr[0]);
		L->fbit = __builtin_ctzll(m);
		return true;
	}
	opi(L->e, I_ANDSI, x, ZR, vl_gx(L->opr[0]), (int64_t)m);
	return true;
}

/* load or store at operand 0's address: lv for the symbol or frame cases, else base register */
static lval vl_addr(vl *L, uint32_t w, int t)
{
	lval l = { 0 };
	uint32_t v = L->ov[0];

	if (L->opr[0] == VR_NONE && vl_isconst(L->f, v) && L->f->ins[v].op == VO_SLOTADDR) {
		l.k = L_FRAME;
		l.off = L->soff[L->f->ins[v].a] + vl_k64(&(vir_ins){ .b = L->f->ins[v].b, .c = L->f->ins[v].c });
		return l;
	}
	if (L->opr[0] == VR_NONE && vl_isconst(L->f, v) && L->f->ins[v].op == VO_SYMADDR) {
		l.k = L_SYM;
		l.ref = L->f->ins[v].a;
		l.off = vl_k64(&(vir_ins){ .b = L->f->ins[v].b, .c = L->f->ins[v].c });
		l.align = 1u << VIF_AL(w);
		return l;
	}
	l.k = L_REG;
	if (L->pval && L->pval == v) {
		l.r = (uint8_t)L->pbase;
		l.off = L->poff;
		L->pval = 0;
		return l;
	}
	l.r = (uint8_t)vl_ix(L, 0, t);
	return l;
}

static void vl_load(vl *L, uint32_t i, const vir_ins *in, uint8_t res)
{
	ecx *e = L->e;
	int w = VIF_W(in->fl), fl = in->ty == VT_F32 || in->ty == VT_F64;
	int ext = fl || w == 3 || !(in->fl & VIF_MS) ? 0 : in->ty == VT_I32 || L->lo32[i] ? (w == 2 ? 0 : 2) : 1;
	int rd = fl || res == VR_NONE ? R_X16 : vl_gx(res);
	lval l;

	if (L->xval && L->xval == L->ov[0]) {
		ins *x = put(e, I_LDX);

		x->sz = (uint8_t)w, x->cond = (uint8_t)ext, x->rd = (uint8_t)rd, x->rn = (uint8_t)L->xbase;
		x->rm = (uint8_t)L->xreg, x->ra = L->xopt, x->sh = L->xsh;
		L->xval = 0;
		if (fl && res != VR_NONE)
			word(e, f_in(in->ty == VT_F64, vl_gv(res), R_X16));
		return;
	}
	l = vl_addr(L, in->fl, R_X16);
	e->novtag += (in->fl & VIF_VOL) != 0;	/* (a volatile slot is no slot to the record clean-up) */
	if (l.k == L_SYM && ext != 2) {
		load_n(e, rd, &l, 1 << w, ext);
	} else if (l.k == L_SYM) {
		addr_of_ref(e, R_X16, l.ref, l.off);
		mem(e, I_LD, w, ext, rd, R_X16, 0);
	} else {
		mem(e, I_LD, w, ext, rd, l.k == L_FRAME ? FRAME : l.r, l.off);
		if ((in->fl & VIF_VOL) && l.k != L_FRAME && e->reach && e->in[e->nin - 1].op == I_LD)
			e->in[e->nin - 1].ra = LD_VOLATILE;	/* (not merged with its neighbours: opt.c's fold) */
	}
	e->novtag -= (in->fl & VIF_VOL) != 0;
	if (fl && res != VR_NONE)
		word(e, f_in(in->ty == VT_F64, vl_gv(res), R_X16));
}

static void vl_store(vl *L, const vir_ins *in)
{
	ecx *e = L->e;
	int w = VIF_W(in->fl);
	uint32_t v = L->ov[1];
	bool xa = L->xval && L->xval == L->ov[0];
	lval l = xa ? (lval){ .k = L_REG, .r = (uint8_t)L->xbase } : vl_addr(L, in->fl, R_X16);
	int base_t = l.k == L_SYM || (l.k == L_REG && l.r == R_X16);	/* the address needs x16 */
	int rs;

	if (L->opr[1] == VR_NONE && vl_zero(L->f, v))
		rs = ZR;
	else if (L->opr[1] != VR_NONE && vl_fcls(L->opr[1])) {
		rs = base_t ? R_X17 : R_X16;
		word(e, f_out(w == 3, rs, vl_gv(L->opr[1])));
	} else {
		rs = vl_ix(L, 1, base_t ? R_X17 : R_X16);
	}
	if (xa) {
		ins *x = put(e, I_STX);

		x->sz = (uint8_t)w, x->rd = (uint8_t)rs, x->rn = (uint8_t)L->xbase, x->rm = (uint8_t)L->xreg;
		x->ra = L->xopt, x->sh = L->xsh;
		L->xval = 0;
	} else if (l.k == L_SYM) {
		store_n(e, rs, &l, 0, 1 << w);
	} else {
		e->novtag += (in->fl & VIF_VOL) != 0;
		mem(e, I_ST, w, 0, rs, l.k == L_FRAME ? FRAME : l.r, l.off);
		e->novtag -= (in->fl & VIF_VOL) != 0;
	}
}

/* an address operand of memcpy/memset: base register and offset */
static int vl_maddr(vl *L, uint32_t k, int t, int64_t *off)
{
	uint32_t v = L->ov[k];

	*off = 0;
	if (L->opr[k] == VR_NONE && vl_isconst(L->f, v) && L->f->ins[v].op == VO_SLOTADDR) {
		*off = L->soff[L->f->ins[v].a] + vl_k64(&(vir_ins){ .b = L->f->ins[v].b, .c = L->f->ins[v].c });
		return FRAME;
	}
	return vl_ix(L, k, t);
}

static void vl_mem(vl *L, const vir_ins *in)
{
	ecx *e = L->e;
	uint64_t n = (uint64_t)vl_k64(&L->f->ins[in->c]);

	if (vl_bigmem(L->f, in)) {
		e->calls = true;
		put(e, I_BL)->ref = FIXED(in->op == VO_MEMCPY ? FN_MEMCPY : FN_MEMSET);
		return;
	}
	if (in->op == VO_MEMSET && !vl_zero(L->f, in->b))
		vl_fail(L, "VIR lowering: memset of a nonzero byte");
	{
		int64_t doff, soff = 0;
		int d = vl_maddr(L, 0, R_X17, &doff), s = in->op == VO_MEMCPY ? vl_maddr(L, 1, R_X16, &soff) : ZR;

		if (d == R_X17 || s == R_X16)
			vl_fail(L, "VIR lowering: a memcpy address without a register");
		for (uint64_t o = 0; o < n;) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			if (in->op == VO_MEMCPY) {
				mem(e, I_LD, log2sz(k), 0, R_X16, s, soff + (int64_t)o);
				mem(e, I_ST, log2sz(k), 0, R_X16, d, doff + (int64_t)o);
			} else {
				mem(e, I_ST, log2sz(k), 0, ZR, d, doff + (int64_t)o);
			}
			o += k;
		}
	}
}

/*
 * Why the function's calls cannot be sibling calls (NULL: those right before a ret can):
 * the callee must not see the frame, so no slot's address may leave the loads, stores
 * and copies that use it (as LLVM's TailCallElim: no escaping alloca).
 */
/* callee k (a symbol's address) returns twice: setjmp and the like */
static bool vl_twice(vl *L, const vir_ins *k)
{
	cc *c = L->e->c;
	uint32_t s;

	if (k->op != VO_SYMADDR || RKIND(k->a) != RK_SYM)
		return false;
	s = canon_sym(c, RIDX(k->a));
	return (sym_attrs(c, s)->flags & AF_TAIL_RETURNS_TWICE) ||
	       EM_IDENT_IS(c->syms[s].name, "setjmp") || EM_IDENT_IS(c->syms[s].name, "_setjmp") ||
	       EM_IDENT_IS(c->syms[s].name, "sigsetjmp") || EM_IDENT_IS(c->syms[s].name, "__sigsetjmp");
}

static const char *vl_notail(vl *L, uint8_t *d)
{
	const vir_fn *f = L->f;
	bool ch = true;

	memset(d, 0, f->nins);
	while (ch) {
		ch = false;
		for (uint32_t b = 0; b < f->nblk; b++)
			for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
				vir_ins x = f->ins[i];
				vir_oit it;
				uint32_t *p;

				if (x.op == VO_ASM || x.op == VO_ASMGOTO || x.op == VO_FRAMEADDR || x.op == VO_RETADDR ||
				    x.op == VO_VA_START)
					return "asm or the frame's address";
				if (x.op == VO_CALL && vl_twice(L, &f->ins[x.a]))
					return "a call that returns twice";
				vir_oit_init(&it, (vir_fn *)f, &x, false);
				while ((p = vir_oit_next(&it))) {
					if (f->ins[*p].op != VO_SLOTADDR && !d[*p])
						continue;
					if (x.op == VO_PTRADD && it.pos == 0) {
						ch |= !d[i];
						d[i] = 1;
					} else if (!(((x.op == VO_LOAD || x.op == VO_STORE || x.op == VO_MEMSET) && it.pos == 0) ||
						     (x.op == VO_MEMCPY && it.pos <= 1))) {
						return "a slot's address escapes";
					}
				}
			}
	}
	return NULL;
}

/* from j in block k: past the instructions that only pass *res on (i2p, p2i; under ret32
 * trunc to i32 and ext.4: the low half is the result, the upper half unspecified), *res
 * following them; the first other instruction */
static uint32_t vl_pass(const vl *L, const vir_blk *k, uint32_t j, uint32_t *res)
{
	const vir_fn *f = L->f;

	for (; j < k->first + k->n && f->ins[j].a == *res; j++) {
		const vir_ins *x = &f->ins[j];

		if (!(x->op == VO_I2P || x->op == VO_P2I ||
		      (L->ret32 && ((x->op == VO_TRUNC && x->ty == VT_I32) || (x->op == VO_EXT && VIF_W(x->fl) == 2)))))
			break;
		*res = j;
	}
	return j;
}

/* the call at i, in b, is a sibling call: what the ret after it returns is its result (or
 * nothing), also through brs to blocks of only what passes the result on */
static bool vl_sibling(vl *L, uint32_t b, uint32_t i)
{
	const vir_fn *f = L->f;
	const vir_blk *k = &f->blk[b];
	const vir_ins *r, *t = &f->ins[f->ins[i].a];
	uint32_t res = i, j;

	if (!L->tail || f->ins[i].ty == VT_MULTI ||
	    (t->op == VO_SYMADDR && RKIND(t->a) == RK_SYM && !em_tail_target_ok(L->e, canon_sym(L->e->c, RIDX(t->a)))))
		return false;
	j = vl_pass(L, k, i + 1, &res);
	for (int hop = 0;; hop++) {
		uint32_t q;

		if (j >= k->first + k->n)
			return false;
		r = &f->ins[j];
		if (r->op != VO_BR || hop == 4)
			break;
		k = &f->blk[r->a];
		for (q = 0; q < k->nparam && f->extra[r->c + q] != res; q++)
			;
		if (q < k->nparam)
			res = k->first + q;	/* (else the ret may name the value itself) */
		j = vl_pass(L, k, k->first + k->nparam, &res);
	}
	if (r->op != VO_RET)
		return false;
	return !f->extra[r->c] || (f->extra[r->c] == 1 && f->extra[r->c + 1] == res);
}

static void vl_call(vl *L, uint32_t b, uint32_t i, const vir_ins *in)
{
	ecx *e = L->e;
	const vir_fn *f = L->f;
	uint32_t ng = VSIG_NGROUPS(f->extra[in->b]);
	vlarg *a;
	int32_t stk;

	if (ng + 1 > L->nop)
		vl_fail(L, "VIR lowering: call operands");
	a = __builtin_alloca(sizeof(vlarg) * (ng + 1));
	uint8_t gm = 0;	/* the x registers the arguments are in */

	vl_assign(DARWIN(e->c), &f->extra[in->b], a, ng, &stk);
	for (uint32_t k = 0; k < ng; k++) {
		uint8_t r = L->opr[1 + k];

		if (a[k].reg != VR_NONE) {
			if (r != a[k].reg)
				vl_fail(L, "VIR lowering: an argument not in its register");
			if (!vl_fcls(r) && vl_gx(r) < 8)
				gm |= (uint8_t)(1u << vl_gx(r));
			else if (!vl_fcls(r))
				gm = 0xff;	/* (not an argument register: assume all) */
			continue;
		}
		{
			int sz = log2sz(a[k].size), rs;

			if (r != VR_NONE && vl_fcls(r)) {
				word(e, f_out(a[k].size == 8, R_X16, vl_gv(r)));
				rs = R_X16;
			} else {
				rs = vl_ix(L, 1 + k, R_X16);
			}
			mem(e, I_ST, sz, 0, rs, SP, a[k].stk);
		}
	}
	if (!stk && vl_sibling(L, b, i)) {
		ins *t = put(e, I_TAIL);

		if (L->opr[0] == VR_NONE) {
			const vir_ins *k = &f->ins[in->a];

			if (k->op != VO_SYMADDR || k->b || k->c)
				vl_fail(L, "VIR lowering: an indirect callee without a register");
			t->cond = 2;
			t->ref = f->ins[in->a].a;
			note_ref(e, t->ref);
		} else {
			t->cond = 3;
			t->rn = (uint8_t)vl_gx(L->opr[0]);
		}
		e->reach = false;
		L->skip = i + 1;
		return;
	}
	if (stk > e->outmax)
		e->outmax = stk;
	e->calls = true;
	if (L->opr[0] == VR_NONE) {
		const vir_ins *k = &f->ins[in->a];

		if (k->op != VO_SYMADDR || k->b || k->c)
			vl_fail(L, "VIR lowering: an indirect callee without a register");
		note_ref(e, k->a);
		ins *c = put(e, I_BL);

		c->ref = k->a;
		c->cond = CALL_ARGS, c->sh = gm;
	} else {
		ins *c = put(e, I_BLR);

		c->rn = (uint8_t)vl_gx(L->opr[0]);
		c->cond = CALL_ARGS, c->sh = gm;
	}
}

static void vl_param(vl *L, const vir_ins *in, uint8_t res)
{
	ecx *e = L->e;
	const vir_fn *f = L->f;
	uint32_t ng = VSIG_NGROUPS(f->extra[f->sig]);
	vlarg *a;
	int32_t stk;

	if (ng > VL_MAXOP || in->a >= ng)
		vl_fail(L, "VIR lowering: a parameter");
	a = __builtin_alloca(sizeof(vlarg) * ng);
	vl_assign(DARWIN(e->c), &f->extra[f->sig], a, ng, &stk);
	if (a[in->a].reg != VR_NONE) {
		if (res != VR_NONE && res != a[in->a].reg)
			vl_fail(L, "VIR lowering: a parameter not in its register");
		return;
	}
	if (res == VR_NONE)
		return;
	if (vl_fcls(res)) {
		mem(e, I_LD, log2sz(a[in->a].size), 0, R_X16, R_FP, 16 + a[in->a].stk);
		word(e, f_in(in->ty == VT_F64, vl_gv(res), R_X16));
	} else {
		mem(e, I_LD, log2sz(a[in->a].size), 0, vl_gx(res), R_FP, 16 + a[in->a].stk);
	}
}

static void vl_switch(vl *L, uint32_t b, const vir_ins *in)
{
	ecx *e = L->e;
	const vir_fn *f = L->f;
	int x = vl_x(f, in->a), r = vl_ix(L, 0, R_X16);
	uint32_t n = f->extra[in->c], stride = x ? 5 : 3;
	const uint32_t *cw = &f->extra[in->c + 1];
	uint32_t dl = vl_succ(L, b, 0, in->b);
	uint64_t span = 0, lo = 0, cnt = 0;

	if (n) {
		lo = x ? (uint64_t)cw[1] << 32 | cw[0] : cw[0];
		uint64_t hi = x ? (uint64_t)cw[stride * (n - 1) + 3] << 32 | cw[stride * (n - 1) + 2] : cw[stride * (n - 1) + 1];

		span = hi - lo;
		for (uint32_t k = 0; k < n; k++) {
			uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
			uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];

			cnt += h - a + 1 > 64 ? 64 : h - a + 1;
		}
	}
	if (n >= 6 && span < 4096 && span + 1 <= 4 * cnt && !e->E->noopt) {
		uint32_t tab = newlabel(e), off = e->njtl;
		int ix = r;

		/* the index: the selector itself from 0 (a word through uxtw), else x17; never x16,
		 * which expand_tables' adr writes first, nor zr */
		if (lo)
			opi(e, I_SUBI, x, ix = R_X17, r, (int64_t)lo);
		else if (r == R_X16 || r >= 31)
			op3(e, I_MOV, x, ix = R_X17, r, 0);
		opi(e, I_SUBSI, x, ZR, ix, (int64_t)span);
		bcond(e, C_HI, dl);
		if (e->njtl + (uint32_t)span + 1 > e->capjtl) {
			e->capjtl = (e->njtl + (uint32_t)span + 1) * 2;
			e->jtl = realloc(e->jtl, (size_t)e->capjtl * sizeof *e->jtl);
		}
		if (e->njt == e->capjt) {
			e->capjt = e->capjt ? e->capjt * 2 : 4;
			e->jt = realloc(e->jt, (size_t)e->capjt * sizeof *e->jt);
		}
		if (!e->jtl || !e->jt)
			abort();
		uint32_t k = 0, l = 0;

		for (uint64_t v = lo; v <= lo + span; v++) {
			uint64_t h;

			while (k < n && (h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1]) < v)
				k++, l = 0;
			uint64_t a = k < n ? (x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k]) : 0;
			uint32_t t;

			if (k < n && a <= v) {
				if (!l)
					l = vl_succ(L, b, 1 + k, cw[stride * k + stride - 1]);
				t = l;
			} else {
				t = dl;
			}
			e->jtl[e->njtl++] = t;
			if (!(e->lrefc[t] & LREF_EXT)) {
				e->lrefc[t]++;
				e->lrefc[t] |= LREF_EXT;
			}
		}
		e->jt[e->njt++] = (struct ejt){ tab, off, (uint32_t)span + 1 };
		ins *i = put(e, I_JT);
		i->rd = (uint8_t)ix;
		i->cond = ix != R_X17 && !x ? 2 : 0;
		i->label = tab;
		e->reach = false;
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint64_t a = x ? (uint64_t)cw[stride * k + 1] << 32 | cw[stride * k] : cw[stride * k];
		uint64_t h = x ? (uint64_t)cw[stride * k + 3] << 32 | cw[stride * k + 2] : cw[stride * k + 1];
		uint32_t t = vl_succ(L, b, 1 + k, cw[stride * k + stride - 1]);

		if (a == h) {
			opi(e, I_SUBSI, x, ZR, r, x ? (int64_t)a : (int64_t)(int32_t)a);
			bcond(e, C_EQ, t);
		} else {
			if (r == R_X17)
				vl_fail(L, "VIR lowering: switch");
			opi(e, I_SUBI, x, R_X17, r, (int64_t)a);
			opi(e, I_SUBSI, x, ZR, R_X17, (int64_t)(h - a));
			bcond(e, C_LS, t);
		}
	}
	jump(e, dl);
}

/* ptradd i (operands collected) with a constant offset, used once, by the next
 * instruction as its address: that adds the offset (its base's register is still
 * there unless an edit before it writes it) */
static bool vl_pfold(vl *L, uint32_t b, uint32_t i)
{
	const vir_fn *f = L->f;
	const vir_ins *in = &f->ins[i], *nx = &f->ins[i + 1];
	uint8_t base = L->opr[0];
	int64_t k;

	if (L->opr[1] != VR_NONE || !vl_iconst(f, in->b) || base == VR_NONE || L->uses[i] != 1 ||
	    L->o->slot[i] != VIR_NOBLK || i + 1 >= f->blk[b].first + f->blk[b].n || nx->a != i ||
	    (nx->op != VO_LOAD && !(nx->op == VO_STORE && nx->b != i)))
		return false;
	k = vl_k64(&f->ins[in->b]);
	if (k < -256 || k > 32760)
		return false;
	for (uint32_t x = L->ei; x < L->eend && L->o->edit[x].pos <= i + 1; x++)
		if (L->o->edit[x].pos == i + 1 && L->o->edit[x].dst == base)
			return false;
	L->pval = i;
	L->pbase = vl_gx(base);
	L->poff = k;
	return true;
}

/* a folded value: used once, by the next instruction, never in a slot */
static bool vl_once(const vl *L, uint32_t v) { return L->uses[v] == 1 && L->o->slot[v] == VIR_NOBLK; }

enum { VX_MEM = 1, VX_EXT, VX_LSL, VX_MADDL };
typedef struct vlx vlx;

/* the index operand (b) of c, a ptradd, add or sub of 64 bits, built by the instructions right
 * before c, each used once by the next, that c folds:
 *   VX_MEM   c a ptradd used once by the next instruction, a load or store through it:
 *            [base, index], the index extended (ext.4s/4u) and scaled by the access size (mul or
 *            shl);
 *   VX_EXT   a + (ext.4s/4u x << 0-4): add/sub (extended register);
 *   VX_LSL   a + (x << k): add/sub (shifted register);
 *   VX_MADDL a + ext.4s/4u x * K (a ptradd or add): smaddl/umaddl with K in a register.
 * The skipped instructions write no register, so x and a are still where they were when no
 * allocator edit comes in between.  x->first: the chain's first instruction, x->xv: x. */
static bool vl_cinfo(const vl *L, uint32_t b, uint32_t c, vlx *x)
{
	const vir_fn *f = L->f;
	const vir_blk *k = &f->blk[b];
	const vir_ins *in = &f->ins[c], *m = &f->ins[c + 1];
	uint32_t j = in->b, lo = k->first + k->nparam, end = k->first + k->n, w = 0;
	bool mem, sh = false;
	int kk = 0;

	if ((in->op != VO_PTRADD && in->op != VO_ADD && in->op != VO_SUB) || in->ty == VT_I32 || vl_isconst(f, j) ||
	    (vl_isconst(f, in->a) && in->op != VO_PTRADD))
		return false;
	mem = in->op == VO_PTRADD && c + 1 < end && m->a == c && vl_once(L, c) &&
	      (m->op == VO_LOAD || (m->op == VO_STORE && m->b != c));
	if (mem)
		w = VIF_W(m->fl);
	*x = (vlx){ c, j, mem ? VX_MEM : 0, 3, 0, 0 };
	if (j + 1 == c && j > lo && vl_once(L, j) && !vl_isconst(f, f->ins[j].a) && vl_iconst(f, f->ins[j].b) &&
	    (f->ins[j].op == VO_MUL || f->ins[j].op == VO_SHL)) {
		const vir_ins *q = &f->ins[j], *e = &f->ins[q->a];
		int64_t K = vl_ival(&f->ins[q->b]);

		kk = q->op == VO_SHL ? (int)K : vl_pow2(&f->ins[q->b]);
		if (q->op == VO_SHL && (K < 0 || K > 63))
			kk = -1;
		if (mem ? kk == (int)w && w : kk > 0) {
			x->first = j, x->xv = q->a, sh = true;
			j = q->a;
		} else if (!mem && q->op == VO_MUL && in->op != VO_SUB && q->a + 1 == j && q->a >= lo && vl_once(L, q->a) &&
			   e->op == VO_EXT && VIF_W(e->fl) == 2 && e->ty == VT_I64 && !vl_isconst(f, e->a) &&
			   ((e->fl & VIF_MS) ? K >= INT32_MIN && K <= INT32_MAX : K >= 0 && K <= UINT32_MAX)) {
			*x = (vlx){ q->a, e->a, VX_MADDL, (e->fl & VIF_MS) ? 6 : 2, 0, K };
			return true;
		}
	}
	if (j + 1 == x->first && j >= lo && vl_once(L, j) && f->ins[j].op == VO_EXT && VIF_W(f->ins[j].fl) == 2 &&
	    f->ins[j].ty == VT_I64 && !vl_isconst(f, f->ins[j].a) && (mem || kk <= 4)) {
		x->first = j, x->xv = f->ins[j].a, x->opt = f->ins[j].fl & VIF_MS ? 6 : 2;
	}
	x->sh = mem ? sh : (uint8_t)(sh ? kk : 0);
	if (!mem)
		x->kind = x->opt != 3 ? VX_EXT : sh ? VX_LSL : 0;
	return x->kind != 0;
}

/* the chain's consumer c: its folded form (VX_MEM: the next load or store's address) */
static void vl_cact(vl *L, uint32_t c, uint8_t res)
{
	ecx *e = L->e;
	uint32_t op = L->f->ins[c].op;
	const vlx *x = &L->xx;
	int a = vl_ix(L, 0, R_X16), rd = res == VR_NONE ? R_X16 : vl_gx(res), t;	/* (a base in x16: vl_store takes x17) */

	L->xc = VIR_NOBLK;
	switch (x->kind) {
	case VX_MEM:
		L->xval = c, L->xbase = a, L->xopt = x->opt, L->xsh = x->sh;
		return;
	case VX_EXT: {
		ins *n = put(e, op == VO_SUB ? I_SUB : I_ADD);	/* (extended register: cond 8 + option) */

		n->sz = 1, n->rd = (uint8_t)rd, n->rn = (uint8_t)a, n->rm = (uint8_t)L->xreg, n->cond = (uint8_t)(8 + x->opt), n->sh = x->sh;
		return;
	}
	case VX_LSL:
		op3s(e, op == VO_SUB ? I_SUB : I_ADD, 1, rd, a, L->xreg, 0, x->sh);
		return;
	default:	/* VX_MADDL */
		t = L->xreg == R_X17 || a == R_X17 ? R_X16 : R_X17;
		movi(e, 1, t, x->k);
		op4(e, x->opt == 6 ? I_SMADDL : I_UMADDL, 1, rd, L->xreg, t, a);
		return;
	}
}

/* instruction i (lowered with its operands collected) is in a chain vl_cinfo folds: true when it
 * is handled (nothing to emit, or the consumer's folded form).  Decided at the chain's first
 * instruction, where the index's register is known: between it and the consumer only remats and
 * reloads into other registers (a constant operand, the base), none before the load or store */
static bool vl_cfold(vl *L, uint32_t b, uint32_t i, uint8_t res)
{
	const vir_fn *f = L->f;
	vlx x;

	if (L->xc != VIR_NOBLK && i > L->xfirst && i <= L->xc) {
		if (i == L->xc)
			vl_cact(L, i, res);
		return true;
	}
	for (uint32_t c = i; c <= i + 2 && c < f->blk[b].first + f->blk[b].n; c++) {
		uint32_t op = f->ins[c].op, end;
		uint8_t r;

		if (op != VO_PTRADD && op != VO_ADD && op != VO_SUB)
			continue;
		if (!vl_cinfo(L, b, c, &x) || x.first != i)
			return false;
		r = L->opr[i == c ? 1 : 0];
		if (r == VR_NONE || vl_fcls(r) || vl_gx(r) == R_X16 || vl_gx(r) == R_X17)
			return false;
		end = c + (x.kind == VX_MEM);
		for (uint32_t q = L->ei; q < L->eend && L->o->edit[q].pos <= end; q++) {	/* (those before are at i or earlier) */
			const vir_edit *d = &L->o->edit[q];

			if (d->pos > i && !(d->pos <= c && (d->kind == VE_REMAT || d->kind == VE_RELOAD) && d->dst != r))
				return false;
		}
		L->xx = x, L->xfirst = i, L->xc = c, L->xreg = vl_gx(r);
		if (i == c)
			vl_cact(L, i, res);
		return true;
	}
	return false;
}

/* the block a branch to t ends up in: past blocks that are a lone br without moves */
static uint32_t vl_chase(vl *L, uint32_t t) { return L->chs[t]; }

static bool vl_lonebr(vl *L, uint32_t t)
{
	const vir_blk *k = &L->f->blk[t];
	const vir_ins *in = &L->f->ins[k->first];

	return !k->nparam && k->n == 1 && in->op == VO_BR && vl_plain(L, t, 0) && !(k->fl & VB_ADDR);
}

/* L->chs for every block; in a cycle of lone brs the block the walk reaches twice stays */
static void vl_chases(vl *L, uint32_t *path)
{
	uint32_t nb = L->f->nblk, *c = L->chs;
	const uint32_t todo = UINT32_MAX, busy = UINT32_MAX - 1;

	for (uint32_t b = 0; b < nb; b++)
		c[b] = todo;
	for (uint32_t b = 0; b < nb; b++) {
		uint32_t t = b, n = 0, r;

		for (;;) {
			if (c[t] == busy) {
				r = t;
				break;
			}
			if (c[t] != todo) {
				r = c[t];
				break;
			}
			if (!vl_lonebr(L, t)) {
				r = t;
				break;
			}
			c[t] = busy;
			path[n++] = t;
			t = L->f->ins[L->f->blk[t].first].a;
		}
		c[t] = c[t] == busy || c[t] == todo ? r : c[t];
		while (n)
			c[path[--n]] = r;
	}
}

/* a return: the epilogue and ret, or a jump to the shared one (finish_function decides) */
static void vl_ret(vl *L)
{
	ecx *e = L->e;
	ins *t;

	if (!e->reach)
		return;
	t = put(e, I_TAIL);
	t->cond = 4;
	t->label = L->retlbl;
	e->lrefc[L->retlbl]++;
	e->reach = false;
}

/*
 * Block t may be copied to the end of a block that jumps to it (tail duplication, as
 * LLVM's block placement): a return (its epilogue, or the jump to the shared one,
 * finish_function's choice), and a lone br (its edge's moves, then on) up to 8 deep.
 * Copies of blocks with work in them (a loop's test at the end of the latch) measured
 * more instructions on speedtest1: their constants and edge moves are lowered again.
 */
static bool vl_dupok(vl *L, uint32_t t);

/* from block b's code, on to block t (an edge without moves left): a jump, or a copy of t */
static void vl_goto(vl *L, uint32_t b, uint32_t t)
{
	if (t != vl_fnext(L) && t != b && vl_dupok(L, t)) {
		int d = L->dup++;

		vl_block(L, t);
		L->dup = d;
		return;
	}
	jump(L->e, L->lbl[t]);
}

static bool vl_dupok(vl *L, uint32_t t)
{
	const vir_fn *f = L->f;
	const vir_blk *k = &f->blk[t];
	uint32_t n = k->n - k->nparam;
	uint8_t op = f->ins[k->first + k->n - 1].op;

	if (k->fl & VB_ADDR)
		return false;
	if (n == 1 && op == VO_RET)
		return true;
	return !L->e->E->noopt && n == 1 && op == VO_BR && L->dup < 8;
}

/* instruction i of block b, its operands collected; res its result's register */
static void vl_ins(vl *L, uint32_t b, uint32_t i, uint8_t res)
{
	ecx *e = L->e;
	vir_fn *f = L->f;
	const vir_ins *in = &f->ins[i];
	int x = in->ty == VT_I64 || in->ty == VT_PTR;
	int rd = res == VR_NONE || vl_fcls(res) ? R_X16 : vl_gx(res);
	int vd = res == VR_NONE || !vl_fcls(res) ? 31 : vl_gv(res);
	int d = in->ty == VT_F64;

	if (i > L->tlo && i < L->thi)
		return;	/* (vl_tfold: in the branch's test) */
	switch (in->op) {
	case VO_PARAM:
		vl_param(L, in, res);
		return;
	case VO_PTRADD:
		if (vl_pfold(L, b, i))
			return;
		__attribute__((fallthrough));
	case VO_ADD: case VO_SUB: case VO_AND: case VO_OR: case VO_XOR: {
		if ((in->op == VO_PTRADD || in->op == VO_ADD || in->op == VO_SUB) && vl_cfold(L, b, i, res))
			return;
		if (in->op == VO_AND && vl_tfold(L, b, i))
			return;
		static const uint8_t rr[] = { [VO_ADD] = I_ADD, [VO_SUB] = I_SUB, [VO_AND] = I_AND, [VO_OR] = I_ORR,
					      [VO_XOR] = I_EOR, [VO_PTRADD] = I_ADD };
		static const uint8_t ri[] = { [VO_ADD] = I_ADDI, [VO_SUB] = I_SUBI, [VO_AND] = I_ANDI, [VO_OR] = I_ORRI,
					      [VO_XOR] = I_EORI, [VO_PTRADD] = I_ADDI };
		int a = vl_ix(L, 0, R_X16);

		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b))
			opi(e, ri[in->op], x, rd, a, in->op == VO_AND || in->op == VO_OR || in->op == VO_XOR ?
				(int64_t)vl_lval(&f->ins[in->b]) : vl_ival(&f->ins[in->b]));
		else
			op3(e, rr[in->op], x, rd, a, vl_ix(L, 1, R_X17));
		return;
	}
	case VO_PSUB:
		op3(e, I_SUB, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_MUL:
		if (vl_cfold(L, b, i, res))
			return;
		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b) && vl_pow2(&f->ins[in->b]) >= 0) {
			int w = x ? 64 : 32, k = vl_pow2(&f->ins[in->b]);

			opbf(e, I_UBFM, x, rd, vl_ix(L, 0, R_X16), (w - k) & (w - 1), w - 1 - k);
			return;
		}
		op4(e, I_MADD, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17), ZR);
		return;
	case VO_SDIV: case VO_UDIV:
		op3(e, in->op == VO_SDIV ? I_SDIV : I_UDIV, x, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_SREM: case VO_UREM: {
		int a = vl_ix(L, 0, R_X16), c = vl_ix(L, 1, R_X17);

		if (a == R_X16 || c == R_X16)
			vl_fail(L, "VIR lowering: a remainder of a constant");
		op3(e, in->op == VO_SREM ? I_SDIV : I_UDIV, x, R_X16, a, c);
		op4(e, I_MSUB, x, rd, R_X16, c, a);
		return;
	}
	case VO_SHL: case VO_LSHR: case VO_ASHR: {
		if (in->op == VO_SHL && vl_cfold(L, b, i, res))
			return;
		if (in->op != VO_SHL && vl_tfold(L, b, i))
			return;
		int a = vl_ix(L, 0, R_X16), w = x ? 64 : 32;

		if (L->opr[1] == VR_NONE && vl_iconst(f, in->b)) {
			int s = (int)(vl_ival(&f->ins[in->b]) & (w - 1));

			if (in->op == VO_SHL)
				opbf(e, I_UBFM, x, rd, a, (w - s) & (w - 1), w - 1 - s);
			else
				opbf(e, in->op == VO_LSHR ? I_UBFM : I_SBFM, x, rd, a, s, w - 1);
		} else {
			op3(e, in->op == VO_SHL ? I_LSLV : in->op == VO_LSHR ? I_LSRV : I_ASRV, x, rd, a, vl_ix(L, 1, R_X17));
		}
		return;
	}
	case VO_UMULHI: case VO_SMULHI:
		op3(e, in->op == VO_UMULHI ? I_UMULH : I_SMULH, 1, rd, vl_ix(L, 0, R_X16), vl_ix(L, 1, R_X17));
		return;
	case VO_NEG:
		op3(e, I_SUB, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VO_NOT:
		op3(e, I_ORN, x, rd, ZR, vl_ix(L, 0, R_X16));
		return;
	case VO_CLZ:
		op3(e, I_CLZ, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VO_CTZ:
		op3(e, I_RBIT, x, rd, vl_ix(L, 0, R_X16), 0);
		op3(e, I_CLZ, x, rd, rd, 0);
		return;
	case VO_POPCNT:
		word(e, f_in(1, 31, vl_ix(L, 0, R_X16)));
		word(e, 0x0e205800u | 31u << 5 | 31u);	/* cnt v31.8b, v31.8b */
		word(e, 0x0e31b800u | 31u << 5 | 31u);	/* addv b31, v31.8b */
		word(e, f_out(0, rd, 31));
		return;
	case VO_BSWAP:
		op3(e, I_REV, x, rd, vl_ix(L, 0, R_X16), 0);
		return;
	case VO_EXT: {
		if (vl_cfold(L, b, i, res))
			return;
		int w = VIF_W(in->fl), a = vl_ix(L, 0, R_X16);

		if (w == 3 || (w == 2 && (!x || L->lo32[i])))
			mov(e, 1, rd, a);	/* (ext.4 whose low half alone is read: a copy) */
		else if ((in->fl & VIF_MS) && L->lo32[i])	/* only its low half is read: the i32 form */
			opbf(e, I_SBFM, 0, rd, a, 0, w == 2 ? 31 : (8 << w) - 1);
		else if (in->fl & VIF_MS)
			opbf(e, I_SBFM, x, rd, a, 0, (8 << w) - 1);
		else if (w == 2)
			op3(e, I_MOV, 0, rd, a, 0);
		else
			opbf(e, I_UBFM, 0, rd, a, 0, (8 << w) - 1);
		return;
	}
	case VO_TRUNC:	/* an i32's upper half is never read: a copy (ext.4u clears it) */
		mov(e, 1, rd, vl_ix(L, 0, R_X16));
		return;
	case VO_P2I: case VO_I2P:	/* (unused: their users read the operand) */
		return;
	case VO_ICMP:
	case VO_FCMP: {
		int cc;
		bool fuse = L->uses[i] == 1 && L->o->slot[i] == VIR_NOBLK && i + 1 < f->blk[b].first + f->blk[b].n &&
			    (f->ins[i + 1].op == VO_BRIF || f->ins[i + 1].op == VO_SELECT) && f->ins[i + 1].a == i;

		/* against 0 for a branch: x == 0, x != 0, x <=u 0, x >u 0 as cbz, cbnz; x < 0, x >= 0 as
		 * tbnz, tbz of the sign bit; x <= -1, x > -1 too (when no edit in between writes x) */
		if (fuse && in->op == VO_ICMP && f->ins[i + 1].op == VO_BRIF && L->opr[0] != VR_NONE &&
		    L->opr[1] == VR_NONE && vl_iconst(f, in->b) && vl_kept(L, L->opr[0], i + 1)) {
			int64_t k = vl_ival(&f->ins[in->b]);
			int c = in->fl & 15, cz = -1, cb = -1;

			if (!k)
				cz = c == 0 || c == 7 ? C_EQ : c == 1 || c == 8 ? C_NE : -1, cb = c == 2 ? C_NE : c == 5 ? C_EQ : -1;
			else if (k == -1)
				cb = c == 3 ? C_NE : c == 4 ? C_EQ : -1;
			if (cz >= 0 || cb >= 0) {
				L->fcc = cz >= 0 ? cz : cb;
				L->fval = i;
				L->fzr = vl_gx(L->opr[0]);
				L->fzx = vl_x(f, in->a);
				L->fbit = cb >= 0 ? (L->fzx ? 63 : 31) : -1;
				return;
			}
		}
		if (in->op == VO_ICMP) {
			cc = vl_icc[vl_cmp(L, in)];
		} else {
			vl_fcmp(L, in);
			cc = vl_fccs[in->fl & 7];
		}
		/* used once, by the next instruction as its condition: that reads the flags */
		if (cc >= 0 && fuse) {
			L->fcc = cc;
			L->fval = i;
			L->fzr = -1, L->fbit = -1;
			return;
		}
		if (res == VR_NONE)
			return;
		if (cc >= 0) {
			cset(e, rd, cc);
		} else {	/* one: less or greater */
			cset(e, rd, C_MI);
			op3s(e, I_CSINC, 0, rd, rd, ZR, 0, 0);
			e->in[e->nin - 1].cond = C_LE;
		}
		return;
	}
	case VO_SELECT: {
		int cc = vl_cond(L, in);

		if (in->ty == VT_F32 || in->ty == VT_F64)
			word(e, 0x1e200c00u | (unsigned)d << 22 | (unsigned)vl_fx(L, 2) << 16 | (unsigned)cc << 12 |
				(unsigned)vl_fx(L, 1) << 5 | (unsigned)vd);
		else
			op3s(e, I_CSEL, x, rd, vl_ix(L, 1, R_X16), vl_ix(L, 2, R_X17), 0, 0), e->in[e->nin - 1].cond = (uint8_t)cc;
		return;
	}
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV: {
		static const uint32_t op[] = { 0x1e202800u, 0x1e203800u, 0x1e200800u, 0x1e201800u };

		word(e, op[in->op - VO_FADD] | (unsigned)d << 22 | (unsigned)vl_fx(L, 1) << 16 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VO_FNEG: case VO_FABS: case VO_FSQRT: {
		uint32_t op = in->op == VO_FNEG ? 0x1e214000u : in->op == VO_FABS ? 0x1e20c000u : 0x1e21c000u;

		word(e, op | (unsigned)d << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VO_FEXT:
		word(e, 0x1e22c000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VO_FTRUNC:
		word(e, 0x1e624000u | (unsigned)vl_fx(L, 0) << 5 | (unsigned)vd);
		return;
	case VO_S2F: case VO_U2F:
		word(e, (in->op == VO_S2F ? 0x1e220000u : 0x1e230000u) | (unsigned)vl_x(f, in->a) << 31 | (unsigned)d << 22 |
			(unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)vd);
		return;
	case VO_F2S: case VO_F2U:
		word(e, (in->op == VO_F2S ? 0x1e380000u : 0x1e390000u) | (unsigned)x << 31 |
			(unsigned)(f->ins[in->a].ty == VT_F64) << 22 | (unsigned)vl_fx(L, 0) << 5 | (unsigned)rd);
		return;
	case VO_BITCAST:
		if (in->ty == VT_F32 || in->ty == VT_F64)
			word(e, f_in(d, vd, vl_ix(L, 0, R_X16)));
		else
			word(e, f_out(x, rd, vl_fx(L, 0)));
		return;
	case VO_LOAD:
		vl_load(L, i, in, res);
		return;
	case VO_STORE:
		vl_store(L, in);
		return;
	case VO_MEMCPY: case VO_MEMSET:
		vl_mem(L, in);
		return;
	case VO_ALOAD:	/* ldar: sequentially consistent with stlr */
		word(e, 0x08dffc00u | (unsigned)VIF_W(in->fl) << 30 | (unsigned)vl_ix(L, 0, R_X16) << 5 | (unsigned)rd);
		return;
	case VO_ASTORE: {
		int p = vl_ix(L, 0, R_X16), v = vl_ix(L, 1, R_X17);

		word(e, 0x089ffc00u | (unsigned)VIF_W(in->fl) << 30 | (unsigned)p << 5 | (unsigned)v);
		return;
	}
	case VO_AFENCE:
		word(e, 0xd5033bbfu);	/* dmb ish */
		return;
	case VO_BARRIER:
		return;
	case VO_CALL:
		vl_call(L, b, i, in);
		return;
	case VO_VA_START: {	/* Apple's: the first unnamed argument, after the named ones on the stack */
		vlarg none;
		int32_t stk;
		lval l = vl_addr(L, in->fl, R_X16);
		int t = l.k == L_REG && l.r == R_X16 ? R_X17 : R_X16;

		if (!DARWIN(e->c))
			vl_fail(L, "VIR lowering: va_start");
		vl_assign(true, &f->extra[f->sig], &none, 0, &stk);
		if (l.k == L_SYM) {
			addr_of_ref(e, R_X17, l.ref, l.off);
			l = (lval){ .k = L_REG, .r = R_X17 };
		}
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((stk + 7) & ~7));
		mem(e, I_ST, 3, 0, t, l.k == L_FRAME ? FRAME : l.r, l.off);
		return;
	}
	case VO_BR: {
		uint32_t g = L->o->eidx[L->o->soff[b]], t = vl_chase(L, in->a);

		if (g != VIR_NOBLK && L->o->edge[g].n)
			vl_emoves(L, g);
		vl_goto(L, b, t);
		return;
	}
	case VO_BRIF: {	/* the jump that remains goes to what follows: the next block or the first edge's moves */
		vlcc cc = vl_brcond(L, in);
		bool tp = vl_plain(L, b, 0), fp = vl_plain(L, b, 1);

		if (tp && fp) {
			uint32_t t = vl_chase(L, in->b), el = L->lbl[vl_chase(L, in->c)];

			if (t == vl_fnext(L)) {
				vl_bc(e, cc, true, el);
				jump(e, L->lbl[t]);
			} else {
				vl_bc(e, cc, false, L->lbl[t]);
				vl_goto(L, b, vl_chase(L, in->c));
			}
		} else if (fp) {
			vl_bc(e, cc, true, L->lbl[vl_chase(L, in->c)]);
			jump(e, vl_succ(L, b, 0, in->b));
		} else {
			uint32_t el = vl_succ(L, b, 1, in->c), tl = vl_succ(L, b, 0, in->b);

			vl_bc(e, cc, false, tl);
			jump(e, el);
		}
		return;
	}
	case VO_SWITCH:
		vl_switch(L, b, in);
		return;
	case VO_RET:
		vl_ret(L);
		return;
	case VO_TRAP:
		put(e, I_BRK)->imm = 1;
		e->reach = false;
		return;
	default: {
		static _Thread_local char m[80];

		snprintf(m, sizeof m, "VIR lowering: %s", vir_optab[in->op].name ? vir_optab[in->op].name : "?");
		vl_fail(L, m);
	}
	}
}

/* ---------------- the function ---------------- */

/* block b's code (its label defined by the caller), with the edge moves of its branches after it */
static void vl_block(vl *L, uint32_t b)
{
	ecx *e = L->e;
	vir_fn *f = L->f;
	const vir_alloc_out *o = L->o;
	const vir_blk *k = &f->blk[b];
	uint32_t pos = o->boff[b], ei = o->boff[f->nblk + 1 + b], eend = o->boff[f->nblk + 2 + b];

	L->fcc = -1, L->fzr = -1, L->fbit = -1;
	L->tlo = L->thi = 0;
	L->pval = 0, L->xval = 0, L->xc = VIR_NOBLK;
	L->eb = ei, L->eend = eend;
	L->ntr = 0;
	L->skip = 0;
	for (uint32_t q = 0; q < k->nparam; q++) {
		uint8_t r = o->opr[pos++];
		uint32_t v = k->first + q;

		if (o->slot[v] != VIR_NOBLK && o->sdef[v] && r != VR_NONE)
			vl_spill(L, r, o->slot[v], R_X16);
	}
	for (uint32_t i = k->first + k->nparam; i < k->first + k->n; i++) {
		vir_ins x = f->ins[i];
		vir_oit it;
		uint32_t *p;
		uint8_t res = VR_NONE;

		if (i == L->skip)
			break;	/* (the block's ret: its sibling call left) */
		vl_edits(L, &ei, eend, i);
		L->ei = ei, L->eend = eend;
		L->nop = 0;
		vir_oit_init(&it, f, &x, false);
		while ((p = vir_oit_next(&it))) {
			if (f->ins[*p].ty == VT_MULTI)
				continue;
			if (L->nop == VL_MAXOP)
				vl_fail(L, "VIR lowering: too many operands");
			L->ov[L->nop] = *p;
			L->opr[L->nop++] = o->opr[pos++];
		}
		if (x.ty != VT_VOID && x.ty != VT_MULTI)
			res = o->opr[pos++];
		if (L->fcc >= 0 && L->fval != x.a && !(i > L->tlo && i < L->thi))
			L->fcc = -1;
		if (!e->reach)
			deflabel_forced(e, newlabel(e));	/* (code after a trap: never reached) */
		vl_ins(L, b, i, res);
		if (x.ty != VT_VOID && x.ty != VT_MULTI && o->slot[i] != VIR_NOBLK && o->sdef[i] && res != VR_NONE)
			vl_spill(L, res, o->slot[i], R_X16);
	}
	if (e->reach)
		vl_fail(L, "VIR lowering: a block without a terminator");
	if (L->ntr) {	/* (a copy in vl_goto reuses L->tr) */
		uint32_t n = L->ntr;
		struct { uint32_t l, e, to; } *tr = __builtin_alloca(n * sizeof *tr);

		memcpy(tr, L->tr, n * sizeof *tr);
		L->ntr = 0;
		bool fo = L->fallok;

		for (uint32_t t = 0; t < n; t++) {
			deflabel_forced(e, tr[t].l);
			vl_emoves(L, tr[t].e);
			L->fallok = fo && t == n - 1;
			vl_goto(L, b, vl_chase(L, tr[t].to));
		}
		L->fallok = fo;
	}
	L->ntr = 0;
}

static void vl_body(vl *L)
{
	ecx *e = L->e;
	vir_fn *f = L->f;

	for (uint32_t b = 0; b < f->nblk; b++) {
		if (vl_skip(L, b))
			continue;
		deflabel_forced(e, L->lbl[b]);
		L->at = b;
		L->fallok = true;
		vl_block(L, b);
	}
}

/* lower V->f (allocated in V->ao) for function s; false: why in *why, nothing emitted */
VPRIV bool vw_a64(ecx *e, uint32_t s, const char **why)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	vir_regs regs;
	vl L = { 0 };
	cc *c = e->c;
	const ctype *ft = T(c, c->syms[s].ty);

	*why = NULL;
	if (e->E->o->debug) {
		*why = "VIR lowering: -g";
		return false;
	}
	vl_regs(&regs, DARWIN(c));
	if (!vir_loops_build(f, &V->lp)) {
		*why = "VIR: loops";
		return false;
	}
	/* i2p and p2i are copies here: their users read the operand (a load's address an add,
	 * to fold), and they go unemitted */
	for (uint32_t b = 0; b < f->nblk; b++)
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
			vir_oit it;
			uint32_t *p;

			vir_oit_init(&it, f, &f->ins[i], false);
			while ((p = vir_oit_next(&it)))
				while (f->ins[*p].op == VO_I2P || f->ins[*p].op == VO_P2I)
					*p = f->ins[*p].a;
		}
	if (!vir_alloc(f, &V->lp, &regs, NULL, &V->ao)) {
		*why = V->ao.err[0] ? V->ao.err : "VIR: allocation failed";
		return false;
	}
	if (getenv("VCC_VIR_CHECK")) {
		vbuf msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->ao, &msg);

		if (n) {
			fprintf(stderr, "vir: allocation check: %u failures\n%.*s", n, (int)msg.n, msg.p ? (char *)msg.p : "");
			free(msg.p);
			*why = "VIR: the allocation check failed";
			return false;
		}
		free(msg.p);
	}
	L.e = e, L.f = f, L.o = &V->ao;
	VLT.soff = vl_grow(VLT.soff, &VLT.cs, f->nslot + 1, sizeof(int64_t));
	VLT.spoff = vl_grow(VLT.spoff, &VLT.csp, V->ao.nspill + 1, sizeof(int64_t));
	VLT.lbl = vl_grow(VLT.lbl, &VLT.cl, f->nblk + 1, sizeof(uint32_t));
	VLT.uses = vl_grow(VLT.uses, &VLT.cu, f->nins + 1, 1);
	VLT.lo32 = vl_grow(VLT.lo32, &VLT.c32, f->nins + 1, 1);
	VLT.chs = vl_grow(VLT.chs, &VLT.cch, f->nblk + 1, sizeof(uint32_t));
	VLT.path = vl_grow(VLT.path, &VLT.cpa, f->nblk + 1, sizeof(uint32_t));
	L.soff = VLT.soff, L.spoff = VLT.spoff, L.lbl = VLT.lbl, L.uses = VLT.uses, L.lo32 = VLT.lo32;
	L.chs = VLT.chs;
	vl_chases(&L, VLT.path);
	L.ret32 = KIND(c, ft->base) != TY_VOID && is_integer(c, ft->base) && tsize(c, ft->base) == 4;
	L.tail = !(ft->flags & TF_VARIADIC) && em_tail_caller_ok(e, s) && !vl_notail(&L, VLT.lo32);	/* (lo32: scratch until it is filled) */
	vir_uses(f, L.uses);
	/* lo32 by a fixed point: only falls; a br argument is read as its parameter is.  Facts go
	 * from a use to its operands: blocks and instructions last to first, so one sweep settles
	 * all but what a back edge carries */
	memset(L.lo32, 1, f->nins);
	for (bool ch = true; ch;) {
		ch = false;
		for (uint32_t b = f->nblk; b-- > 0;)
			for (uint32_t i = f->blk[b].first + f->blk[b].n; i-- > f->blk[b].first;) {
				vir_ins x = f->ins[i];
				vir_oit it;
				uint32_t *p;

				if (x.op == VO_TRUNC || (x.op == VO_RET && L.ret32))	/* (a 32-bit return's upper half is unspecified) */
					continue;
				if (x.op == VO_BR) {
					const vir_blk *t = &f->blk[x.a];

					for (uint32_t j = 0; j < t->nparam; j++) {
						uint32_t a = f->extra[x.c + j];

						if (!L.lo32[t->first + j] && L.lo32[a])
							L.lo32[a] = 0, ch = true;
					}
					continue;
				}
				if (x.op == VO_SELECT && L.lo32[i]) {	/* its values are read as it is */
					if (L.lo32[x.a])
						L.lo32[x.a] = 0, ch = true;
					continue;
				}
				if (x.op == VO_CALL) {	/* an integer argument of 4 bytes or less: its upper half is unspecified */
					uint32_t n = f->extra[x.c], hdr = f->extra[x.b];

					for (uint32_t k = 0; k < n; k++) {
						uint32_t a = f->extra[x.c + 1 + k], w0 = f->extra[x.b + 3 + 2 * k], w1 = f->extra[x.b + 4 + 2 * k];

						if (VSIG_NGROUPS(hdr) == n && VG_KIND(w0) == VG_REG && VG_NPIECE(w0) == 1 &&
						    VG_CLASS(w0, 0) == VC_INT && VG_SIZE(w1) <= 4)
							continue;
						if (L.lo32[a])
							L.lo32[a] = 0, ch = true;
					}
					if (L.lo32[x.a])
						L.lo32[x.a] = 0, ch = true;
					continue;
				}
				vir_oit_init(&it, f, &x, false);
				while ((p = vir_oit_next(&it)))
					if (L.lo32[*p])
						L.lo32[*p] = 0, ch = true;
			}
	}

	/* the emitter's per-function state, as emit_function1 sets it */
	em_switch_values_reset(e);
	e->nin = 0;
	e->ndl = 0;
	e->raw.n = 0;
	e->nlabel = 0;
	e->nkv = 0, e->kvalid = 0, e->nkvs = 0, e->nael = 0;
	e->foff = e->fmax = e->outmax = e->cl_top = 0;
	e->nsla = e->nsstk = 0;
	e->falign = 16, e->resv = 0, e->nvsz = 0;
	e->dyn = e->calls = false;
	e->live = 0;
	e->usedcs = 0;
	e->nloop = e->nclean = 0;
	e->ncases = 0;
	e->njt = e->njtl = 0;
	e->reach = true;
	e->top = s;
	e->asmk = 0;
	e->inline_depth = 0;
	e->vararg = (ft->flags & TF_VARIADIC) != 0;
	e->nlr = 0;
	e->f = NULL;
	e->vfail = NULL;
	e->vlow = true;
	e->vtry = true;	/* efatal, eerror: back here */
	switch (_setjmp(e->bail)) {
	case 0:
		break;
	default:
		*why = e->vfail ? e->vfail : "VIR lowering: failed";
		e->vtry = false;
		e->vlow = false;
		return false;
	}
	for (uint32_t k = 0; k < f->nslot; k++) {
		if (f->slot[k].align > 16)
			vl_fail(&L, "VIR lowering: an over-aligned slot");
		L.soff[k] = slot(e, f->slot[k].size ? f->slot[k].size : 1, f->slot[k].align);
	}
	for (uint32_t k = 0; k < V->ao.nspill; k++)
		L.spoff[k] = slot(e, V->ao.ssz[k], V->ao.ssz[k]);
	for (uint32_t b = 0; b < f->nblk; b++)
		L.lbl[b] = newlabel(e);
	L.retlbl = newlabel(e);
	vl_body(&L);
	deflabel_forced(e, L.retlbl);
	if (L.risk && e->fmax + e->outmax + 128 > 4000)
		vl_fail(&L, "VIR lowering: x17 across a large frame");
	for (int r = 19; r <= 28; r++)
		if (V->ao.callee[0] >> r & 1)
			e->usedcs |= 1u << r;
	if (V->ao.callee[1])
		vl_fail(&L, "VIR lowering: a callee-saved float register");
	finish_function(e, s);
	e->vtry = false;
	e->vlow = false;
	return true;
}
