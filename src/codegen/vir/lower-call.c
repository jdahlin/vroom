/* The walker's lowering interface for calls, parameters and returns, and the start and end of a function. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- calls ---------------- */

/* the classification for arm64 (the signature's groups): scalars only for now */
VPRIV void virbuild_classify(ecx *e, qty t, wkarg *a, bool unnamed)
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
		virbuild_fail(e, VIRBUILD_FAILURE(record_argument_or_result));
	a->n = 1;
	a->cls[0] = a->cls[1] = is_flt(c, t) ? WA_SSE : WA_INT;
}

static uint32_t virbuild_call_group_pack(const wkarg *a, uint32_t *w)
{
	uint32_t pc = a->cls[0] == WA_SSE ? (a->size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;

	w[0] = VIR_GROUP_WORD0(VIR_GROUP_REG, 1, pc, 0, 0, 0, a->unnamed ? VIR_GROUP_UNNAMED : 0);
	w[1] = VIR_GROUP_WORD1(a->size, 0);	/* (the size: Apple packs stack arguments) */
	return pc;
}

static uint32_t virbuild_pass_class_type(uint32_t pc) { return pc == VIR_CLASS_F32 ? VIR_TYPE_F32 : pc == VIR_CLASS_F64 ? VIR_TYPE_F64 : VIR_TYPE_I64; }

/* a slot word's value: the store table's, else a load */
static uint32_t virbuild_call_word_load(ecx *e, int64_t off, uint32_t ty)
{
	virbuild_context *V = e->vw;
	int64_t rel;
	uint32_t slot = virbuild_slot_find(e, off, 8, &rel);

	for (uint32_t i = V->nst; i-- > 0;)
		if (V->st[i].slot == slot && V->st[i].off == off) {
			uint32_t x = V->st[i].v;

			if (V->st[i].size == 8 || (V->st[i].size == 4 && ty == VIR_TYPE_F32))
				return virbuild_value_convert(e, x, ty == VIR_TYPE_I64 && virbuild_value_type(e, x) == VIR_TYPE_PTR ? VIR_TYPE_PTR : ty);
		}
	V->sl[slot].loads++;
	{
		uint32_t v = virbuild_insn_append(e, VIR_OP_LOAD, ty, virbuild_memory_flags_make(ty == VIR_TYPE_F32 ? 4 : 8, 8, false), virbuild_slot_address(e, slot, rel), 0, 0);

		V->p[v].slot = slot + 1;
		return v;
	}
}

static uint32_t virbuild_call_signature_make(ecx *e, const wkarg *a, int n, bool variadic, qty rt, uint32_t *rpc)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, 3 + 2 * (uint32_t)n);
	V->x[o] = VIR_SIGNATURE_HEADER((uint32_t)n, variadic);
	if (KIND(c, rt) == TY_VOID) {
		V->x[o + 1] = VIR_GROUP_WORD0(VIR_GROUP_NONE, 0, 0, 0, 0, 0, 0), V->x[o + 2] = 0;
		*rpc = 0;
	} else {
		wkarg r;

		virbuild_classify(e, rt, &r, false);
		*rpc = virbuild_call_group_pack(&r, &V->x[o + 1]);
	}
	for (int k = 0; k < n; k++)
		virbuild_call_group_pack(&a[k], &V->x[o + 3 + 2 * k]);
	V->nx += 3 + 2 * (uint32_t)n;
	return o;
}

VPRIV val virbuild_call(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;
	uint32_t callee, sig, rpc, o, v;
	uint32_t *w;

	if (r)
		virbuild_fail(e, VIRBUILD_FAILURE(record_result));
	for (int k = 0; k < n; k++)
		if (a[k].mem || a[k].n != 1)
			virbuild_fail(e, VIRBUILD_FAILURE(record_argument));
	if (fn) {
		callee = virbuild_walker_value_get(e, fn, VIR_TYPE_PTR);
		if (fn->k == V_REG)
			virbuild_tfree(e, fn->r);
	} else {
		callee = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, ref, 0, 0);
	}
	sig = virbuild_call_signature_make(e, a, n, variadic, rt, &rpc);
	o = V->nx;
	GROW(V->x, V->nx, V->cx, 1 + (uint32_t)n);
	V->nx += 1 + (uint32_t)n;
	V->x[o] = (uint32_t)n;
	for (int k = 0; k < n; k++) {
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;
		uint32_t x = virbuild_call_word_load(e, a[k].slot, virbuild_pass_class_type(pc));

		w = &V->x[o + 1 + k];
		*w = x;
	}
	v = virbuild_insn_append(e, VIR_OP_CALL, rpc ? virbuild_pass_class_type(rpc) : VIR_TYPE_VOID, 0, callee, sig, o);
	virbuild_store_table_clear(e, true);
	if (!rpc)
		return vconst(0);
	if (rpc != VIR_CLASS_INT) {
		int q = virbuild_ftreg(e);

		virbuild_register_write(e, q, v);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
	{
		int q = virbuild_treg(e), size = wk_size(c, rt);
		bool sgn = wk_signed(c, rt);

		if (is_ptr(c, rt))
			v = virbuild_value_convert(e, v, VIR_TYPE_PTR);
		virbuild_register_write(e, q, v);
		if (size == 4 && !sgn)
			virbuild_register_write(e, q, virbuild_value_convert(e, v, VIR_TYPE_I32));
		else if (size < 8)
			virbuild_ext(e, q, size, sgn);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
}

VPRIV void virbuild_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;
	const node *body = N(c->syms[e->wk->fsym].body);

	if (r)
		virbuild_fail(e, VIRBUILD_FAILURE(record_result));
	if (variadic && !DARWIN(c))
		virbuild_fail(e, VIRBUILD_FAILURE(variadic_function));
	uint32_t pv0 = V->np;	/* the params first (SPEC 15.2: the allocator reads them from the ABI registers) */

	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;

		if (a[k].mem || a[k].n != 1)
			virbuild_fail(e, VIRBUILD_FAILURE(record_parameter));
		if (virbuild_insn_append(e, VIR_OP_PARAM, pc == VIR_CLASS_INT && is_ptr(c, ty) ? VIR_TYPE_PTR : virbuild_pass_class_type(pc), 0, (uint32_t)k, 0, 0) != pv0 + (uint32_t)k)
			virbuild_fail(e, VIRBUILD_FAILURE(parameters));
	}
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VIR_CLASS_F32 : VIR_CLASS_F64) : VIR_CLASS_INT;
		uint32_t v = pv0 + (uint32_t)k;
		lval home;
		int q;

		q = pc == VIR_CLASS_INT ? virbuild_treg(e) : virbuild_ftreg(e);
		virbuild_register_write(e, q, v);
		if (pc == VIR_CLASS_INT && wk_size(c, ty) == 4)
			virbuild_register_write(e, q, virbuild_value_convert(e, v, VIR_TYPE_I32));	/* (the low half is the int; an i32 for the walker) */
		else if (pc == VIR_CLASS_INT && wk_size(c, ty) < 4)
			virbuild_ext(e, q, wk_size(c, ty), wk_signed(c, ty));
		if (virbuild_local(e, sym, &home)) {
			virbuild_store(e, q, &home, pc == VIR_CLASS_INT ? wk_size(c, ty) : (int)a[k].size);
		} else {
			lval s = { .k = L_FRAME, .off = a[k].slot, .align = 8 };

			virbuild_store(e, q, &s, 8);
		}
		virbuild_tfree(e, q);
	}
	V->nparam = (uint32_t)n;
	{
		uint32_t rpc;

		V->fsig = virbuild_call_signature_make(e, a, n, variadic, e->wk->rty, &rpc);
	}
}

VPRIV void virbuild_ret(ecx *e, val *v, qty t, uint32_t lbl)
{
	virbuild_context *V = e->vw;
	cc *c = e->c;

	if (v) {
		uint32_t x;

		if (is_flt(c, t)) {
			x = virbuild_walker_value_get(e, v, virbuild_float_type(e, t));
		} else {
			int size = wk_size(c, t);
			bool sgn = wk_signed(c, t);

			if (v->k == V_CONST) {
				int64_t k = (int64_t)v->i;

				if (size < 8)
					k = sgn ? (int64_t)((uint64_t)k << (64 - 8 * size)) >> (64 - 8 * size) :
						  (int64_t)((uint64_t)k & ((1ull << (8 * size)) - 1));
				x = virbuild_constant_integer(e, is_ptr(c, t) ? VIR_TYPE_PTR : VIR_TYPE_I64, k);
			} else {
				int q = virbuild_toreg(e, v, size);

				if (size < 4)	/* (an int's upper half is unspecified in AAPCS64: a free zero extension) */
					virbuild_ext(e, q, size, sgn);
				else if (size == 4)
					virbuild_register_write(e, q, virbuild_register_read(e, q, VIR_TYPE_I32));
				x = virbuild_register_read(e, q, is_ptr(c, t) ? VIR_TYPE_PTR : VIR_TYPE_I64);
				virbuild_tfree(e, q);
			}
		}
		virbuild_variable_write(e, VIRBUILD_RESULT_VARIABLE, V->cur, x);
		V->rty = virbuild_value_type(e, x);
	}
	virbuild_branch_emit(e, virbuild_label_block(e, lbl));
}

/* ---------------- the function ---------------- */

VPRIV void virbuild_begin(ecx *e, uint32_t s)
{
	virbuild_context *V = virbuild_context_get(e);

	(void)s;
	V->np = 1;	/* (0 is no instruction) */
	GROW(V->p, 0, V->cp, 1);
	V->p[0] = (virbuild_preir_insn){ 0 };
	V->nb = V->nx = V->nsl = V->nact = V->nst = V->nloc = V->nprom = 0;
	V->live[0] = V->live[1] = V->live[2] = 0;
	V->flive = 0;
	memset(V->fprod, 0, sizeof V->fprod);
	V->hn = 0;
	virbuild_variable_table_clear(V);
	V->hcap = V->halloc < VIRBUILD_VARIABLE_TABLE_INITIAL ? V->halloc : VIRBUILD_VARIABLE_TABLE_INITIAL;
	if (V->lhcap)
		memset(V->lh, 0, V->lhcap * sizeof *V->lh);
	V->lhcap = V->lhalloc < 256 ? V->lhalloc : 256;
	if (V->labhi)	/* (only the labels the last function used) */
		memset(V->lab, 0xff, V->labhi * sizeof *V->lab);
	V->labhi = 0;
	V->sealing = false;
	V->why = NULL;	/* (one function's reason is not the next one's) */
	V->rty = VIR_TYPE_VOID;
	V->nparam = 0;
	V->cur = virbuild_block_new(e);
	e->ndl = 0;
}

VPRIV void virbuild_finish(ecx *e, uint32_t s)
{
	virbuild_context *V = e->vw;

	(void)s;
	{	/* the return block: ret, with the result if there is one */
		uint32_t w0 = V->x[V->fsig + 1], rty = VIR_GROUP_KIND(w0) == VIR_GROUP_REG ? virbuild_pass_class_type(VIR_GROUP_CLASS(w0, 0)) : VIR_TYPE_VOID;
		uint32_t o, rv;

		if (rty == VIR_TYPE_I64 && V->rty == VIR_TYPE_PTR)
			rty = VIR_TYPE_PTR;
		rv = rty != VIR_TYPE_VOID ? virbuild_value_convert(e, virbuild_variable_read(e, VIRBUILD_RESULT_VARIABLE, V->cur, rty), rty) : 0;
		o = V->nx;

		GROW(V->x, V->nx, V->cx, 2);
		V->x[V->nx++] = rv ? 1 : 0;
		if (rv)
			V->x[V->nx++] = rv;
		V->b[V->cur].term = virbuild_preir_insn_add(e, VIR_OP_RET, VIR_TYPE_VOID, 0, 0, 0, o, V->cur);
	}
	if (!virbuild_function_emit(e))
		virbuild_fail(e, V->why ? V->why : VIRBUILD_FAILURE(build_failed));
}

VPRIV void virbuild_ecx_free(ecx *e)
{
	virbuild_context *V = e->vw;

	if (!V)
		return;
	free(V->p), free(V->b), free(V->x), free(V->h), free(V->lh), free(V->fwd), free(V->lab), free(V->sl), free(V->act);
	free(V->st), free(V->loc), free(V->vty), free(V->vsgn), free(V->pr), free(V->map), free(V->order), free(V->bmap);
	free(V->xslot), free(V->xspill), free(V->xlabel), free(V->xuses), free(V->xedgelab);
	free(V);
	e->vw = NULL;
}
