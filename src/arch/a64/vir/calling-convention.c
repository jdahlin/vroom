/* arm64 lowering of VIR: the ABI (AAPCS64, Apple's), the fixed operands of calls, parameters and returns, and the registers offered to the allocator. */

#include "lowering-internal.h"

/* where each argument of signature w goes (AAPCS64; Apple: unnamed on the stack, named
 * packed); returns the count, *stk the bytes of stack arguments */
VPRIV uint32_t vl_assign(bool darwin, const uint32_t *w, vlarg *out, uint32_t max, int32_t *stk)
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

#define VL_FCS 0xff00ull						/* v8-v15: a call keeps their low 64 bits */

VPRIV bool vl_bigmem(const vir_fn *f, const vir_ins *in)
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
	case VO_SELECT:	/* (csel reads zero from wzr) */
		if (in->ty != VT_F32 && in->ty != VT_F64) {
			if (vl_zero(f, in->b))
				x->mem |= 2;
			if (vl_zero(f, in->c))
				x->mem |= 4;
		}
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

VPRIV void vl_regs(vir_regs *r, bool darwin, bool fcs)
{
	static const uint8_t ops[] = { VO_PARAM, VO_CALL, VO_RET, VO_MEMCPY, VO_MEMSET, VO_LOAD, VO_STORE, VO_ADD, VO_SUB,
				       VO_AND, VO_OR, VO_XOR, VO_ICMP, VO_PTRADD, VO_SHL, VO_LSHR, VO_ASHR, VO_MUL,
				       VO_I2P, VO_P2I, VO_TRUNC, VO_EXT, VO_SELECT };

	for (size_t i = 0; i < sizeof ops; i++)
		vl_opfix[ops[i]] = 1;
	*r = (vir_regs){ 0 };
	r->alloc[0] = VL_ALLOC0;
	r->alloc[1] = VL_ALLOC1 | (fcs ? VL_FCS : 0);
	r->callee[0] = VL_ALLOC0 & ~VL_CALLER0;
	r->callee[1] = fcs ? VL_FCS : 0;
	r->fixed = vl_fixed;
	r->ctx = darwin ? (void *)1 : NULL;
	r->opfix = vl_opfix;
}
