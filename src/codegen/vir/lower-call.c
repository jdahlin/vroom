/* The walker's lowering interface for calls, parameters and returns, and the start and end of a function. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- calls ---------------- */

/* the classification for arm64 (the signature's groups): scalars only for now */
VPRIV void vw_classify(ecx *e, qty t, wkarg *a, bool unnamed)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int k = KIND(c, t);

	memset(a, 0, sizeof *a);
	a->size = size;
	a->align = talign(c, t) > 8 ? talign(c, t) : 8;
	a->unnamed = unnamed;
	if (is_record(c, t) || is_array(c, t) || is_vec(c, t) || is_wide(c, t) || k == TY_LDOUBLE || k == TY_FLOAT128 ||
	    k == TY_FLOAT16 || k == TY_BFLOAT16 || size > 8)
		vw_fail(e, VIRFAIL(record_argument_or_result));
	a->n = 1;
	a->cls[0] = a->cls[1] = is_flt(c, t) ? WA_SSE : WA_INT;
}

static uint32_t vw_group(const wkarg *a, uint32_t *w)
{
	uint32_t pc = a->cls[0] == WA_SSE ? (a->size == 4 ? VC_F32 : VC_F64) : VC_INT;

	w[0] = VG_WORD0(VG_REG, 1, pc, 0, 0, 0, a->unnamed ? VG_UNNAMED : 0);
	w[1] = VG_WORD1(a->size, 0);	/* (the size: Apple packs stack arguments) */
	return pc;
}

static uint32_t vw_pcty(uint32_t pc) { return pc == VC_F32 ? VT_F32 : pc == VC_F64 ? VT_F64 : VT_I64; }

/* a slot word's value: the store table's, else a load */
static uint32_t vw_word(ecx *e, int64_t off, uint32_t ty)
{
	vwst *V = e->vw;
	int64_t rel;
	uint32_t slot = vw_slotat(e, off, 8, &rel);

	for (uint32_t i = V->nst; i-- > 0;)
		if (V->st[i].slot == slot && V->st[i].off == off) {
			uint32_t x = V->st[i].v;

			if (V->st[i].size == 8 || (V->st[i].size == 4 && ty == VT_F32))
				return vw_conv(e, x, ty == VT_I64 && vw_ty(e, x) == VT_PTR ? VT_PTR : ty);
		}
	V->sl[slot].loads++;
	{
		uint32_t v = vw_ins(e, VO_LOAD, ty, vw_mfl(ty == VT_F32 ? 4 : 8, 8, false), vw_slotaddr(e, slot, rel), 0, 0);

		V->p[v].slot = slot + 1;
		return v;
	}
}

static uint32_t vw_sig(ecx *e, const wkarg *a, int n, bool variadic, qty rt, uint32_t *rpc)
{
	vwst *V = e->vw;
	cc *c = e->c;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, 3 + 2 * (uint32_t)n);
	V->x[o] = VSIG_HDR((uint32_t)n, variadic);
	if (KIND(c, rt) == TY_VOID) {
		V->x[o + 1] = VG_WORD0(VG_NONE, 0, 0, 0, 0, 0, 0), V->x[o + 2] = 0;
		*rpc = 0;
	} else {
		wkarg r;

		vw_classify(e, rt, &r, false);
		*rpc = vw_group(&r, &V->x[o + 1]);
	}
	for (int k = 0; k < n; k++)
		vw_group(&a[k], &V->x[o + 3 + 2 * k]);
	V->nx += 3 + 2 * (uint32_t)n;
	return o;
}

VPRIV val vw_call(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt)
{
	vwst *V = e->vw;
	cc *c = e->c;
	uint32_t callee, sig, rpc, o, v;
	uint32_t *w;

	if (r)
		vw_fail(e, VIRFAIL(record_result));
	for (int k = 0; k < n; k++)
		if (a[k].mem || a[k].n != 1)
			vw_fail(e, VIRFAIL(record_argument));
	if (fn) {
		callee = vw_val(e, fn, VT_PTR);
		if (fn->k == V_REG)
			vw_tfree(e, fn->r);
	} else {
		callee = vw_k(e, VO_SYMADDR, VT_PTR, ref, 0, 0);
	}
	sig = vw_sig(e, a, n, variadic, rt, &rpc);
	o = V->nx;
	GROW(V->x, V->nx, V->cx, 1 + (uint32_t)n);
	V->nx += 1 + (uint32_t)n;
	V->x[o] = (uint32_t)n;
	for (int k = 0; k < n; k++) {
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;
		uint32_t x = vw_word(e, a[k].slot, vw_pcty(pc));

		w = &V->x[o + 1 + k];
		*w = x;
	}
	v = vw_ins(e, VO_CALL, rpc ? vw_pcty(rpc) : VT_VOID, 0, callee, sig, o);
	vw_st_clear(e, true);
	if (!rpc)
		return vconst(0);
	if (rpc != VC_INT) {
		int q = vw_ftreg(e);

		vw_set(e, q, v);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
	{
		int q = vw_treg(e), size = wk_size(c, rt);
		bool sgn = wk_signed(c, rt);

		if (is_ptr(c, rt))
			v = vw_conv(e, v, VT_PTR);
		vw_set(e, q, v);
		if (size == 4 && !sgn)
			vw_set(e, q, vw_conv(e, v, VT_I32));
		else if (size < 8)
			vw_ext(e, q, size, sgn);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
}

VPRIV void vw_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	vwst *V = e->vw;
	cc *c = e->c;
	const node *body = N(c->syms[e->wk->fsym].body);

	if (r)
		vw_fail(e, VIRFAIL(record_result));
	if (variadic && !DARWIN(c))
		vw_fail(e, VIRFAIL(variadic_function));
	uint32_t pv0 = V->np;	/* the params first (SPEC 15.2: the allocator reads them from the ABI registers) */

	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;

		if (a[k].mem || a[k].n != 1)
			vw_fail(e, VIRFAIL(record_parameter));
		if (vw_ins(e, VO_PARAM, pc == VC_INT && is_ptr(c, ty) ? VT_PTR : vw_pcty(pc), 0, (uint32_t)k, 0, 0) != pv0 + (uint32_t)k)
			vw_fail(e, VIRFAIL(parameters));
	}
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;
		uint32_t v = pv0 + (uint32_t)k;
		lval home;
		int q;

		q = pc == VC_INT ? vw_treg(e) : vw_ftreg(e);
		vw_set(e, q, v);
		if (pc == VC_INT && wk_size(c, ty) == 4)
			vw_set(e, q, vw_conv(e, v, VT_I32));	/* (the low half is the int; an i32 for the walker) */
		else if (pc == VC_INT && wk_size(c, ty) < 4)
			vw_ext(e, q, wk_size(c, ty), wk_signed(c, ty));
		if (vw_local(e, sym, &home)) {
			vw_store(e, q, &home, pc == VC_INT ? wk_size(c, ty) : (int)a[k].size);
		} else {
			lval s = { .k = L_FRAME, .off = a[k].slot, .align = 8 };

			vw_store(e, q, &s, 8);
		}
		vw_tfree(e, q);
	}
	V->nparam = (uint32_t)n;
	{
		uint32_t rpc;

		V->fsig = vw_sig(e, a, n, variadic, e->wk->rty, &rpc);
	}
}

VPRIV void vw_ret(ecx *e, val *v, qty t, uint32_t lbl)
{
	vwst *V = e->vw;
	cc *c = e->c;

	if (v) {
		uint32_t x;

		if (is_flt(c, t)) {
			x = vw_val(e, v, vw_fty(e, t));
		} else {
			int size = wk_size(c, t);
			bool sgn = wk_signed(c, t);

			if (v->k == V_CONST) {
				int64_t k = (int64_t)v->i;

				if (size < 8)
					k = sgn ? (int64_t)((uint64_t)k << (64 - 8 * size)) >> (64 - 8 * size) :
						  (int64_t)((uint64_t)k & ((1ull << (8 * size)) - 1));
				x = vw_iconst(e, is_ptr(c, t) ? VT_PTR : VT_I64, k);
			} else {
				int q = vw_toreg(e, v, size);

				if (size < 4)	/* (an int's upper half is unspecified in AAPCS64: a free zero extension) */
					vw_ext(e, q, size, sgn);
				else if (size == 4)
					vw_set(e, q, vw_get(e, q, VT_I32));
				x = vw_get(e, q, is_ptr(c, t) ? VT_PTR : VT_I64);
				vw_tfree(e, q);
			}
		}
		vw_write(e, VW_RETV, V->cur, x);
		V->rty = vw_ty(e, x);
	}
	vw_br(e, vw_lblk(e, lbl));
}

/* ---------------- the function ---------------- */

VPRIV void vw_begin(ecx *e, uint32_t s)
{
	vwst *V = vw_st(e);

	(void)s;
	V->np = 1;	/* (0 is no instruction) */
	GROW(V->p, 0, V->cp, 1);
	V->p[0] = (vwp){ 0 };
	V->nb = V->nx = V->nsl = V->nact = V->nst = V->nloc = V->nprom = 0;
	V->live[0] = V->live[1] = V->live[2] = 0;
	V->flive = 0;
	memset(V->fprod, 0, sizeof V->fprod);
	V->hn = 0;
	vw_hgen(V);
	V->hcap = V->halloc < VW_H0 ? V->halloc : VW_H0;
	if (V->lhcap)
		memset(V->lh, 0, V->lhcap * sizeof *V->lh);
	V->lhcap = V->lhalloc < 256 ? V->lhalloc : 256;
	if (V->labhi)	/* (only the labels the last function used) */
		memset(V->lab, 0xff, V->labhi * sizeof *V->lab);
	V->labhi = 0;
	V->sealing = false;
	V->why = NULL;	/* (one function's reason is not the next one's) */
	V->rty = VT_VOID;
	V->nparam = 0;
	V->cur = vw_newblk(e);
	e->ndl = 0;
}

VPRIV void vw_finish(ecx *e, uint32_t s)
{
	vwst *V = e->vw;

	(void)s;
	{	/* the return block: ret, with the result if there is one */
		uint32_t w0 = V->x[V->fsig + 1], rty = VG_KIND(w0) == VG_REG ? vw_pcty(VG_CLASS(w0, 0)) : VT_VOID;
		uint32_t o, rv;

		if (rty == VT_I64 && V->rty == VT_PTR)
			rty = VT_PTR;
		rv = rty != VT_VOID ? vw_conv(e, vw_readv(e, VW_RETV, V->cur, rty), rty) : 0;
		o = V->nx;

		GROW(V->x, V->nx, V->cx, 2);
		V->x[V->nx++] = rv ? 1 : 0;
		if (rv)
			V->x[V->nx++] = rv;
		V->b[V->cur].term = vw_put(e, VO_RET, VT_VOID, 0, 0, 0, o, V->cur);
	}
	if (!vw_emit(e))
		vw_fail(e, V->why ? V->why : VIRFAIL(build_failed));
}

VPRIV void vw_ecx_free(ecx *e)
{
	vwst *V = e->vw;

	if (!V)
		return;
	free(V->p), free(V->b), free(V->x), free(V->h), free(V->lh), free(V->fwd), free(V->lab), free(V->sl), free(V->act);
	free(V->st), free(V->loc), free(V->vty), free(V->vsgn), free(V->pr), free(V->map), free(V->order), free(V->bmap);
	free(V->xslot), free(V->xspill), free(V->xlabel), free(V->xuses), free(V->xedgelab);
	free(V);
	e->vw = NULL;
}
