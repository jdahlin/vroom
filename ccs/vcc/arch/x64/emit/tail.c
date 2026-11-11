/* Direct register-only sibling calls. The shared proof limits this to exact
   forwarding wrappers; a terminal marker keeps ABI arguments live to exit. */
#include "x64impl.h"

VPRIV bool x64_tail_return(ecx *e, uint32_t expr)
{
	cc *c = e->c;
	wkst *W = e->wk;
	uint32_t callee;
	int64_t slots[6];
	const node *call;

	if (W->inl || W->inst || W->nclean || W->nvla || W->spneed || e->E->o->sprot ||
	    W->falign > 16 || !em_tail_candidate(e, W->fsym, expr, 6, &callee))
		return false;
	call = N(expr);
	/* Snapshot every argument before writing any ABI register. This handles
	   permutations, repeated arguments and homes overlapping incoming regs. */
	for (uint32_t i = 0; i < call->c; i++) {
		uint32_t a = c->lists[call->b + i];
		qty ty = N(a)->ty;
		val v = wk_gen(e, a);
		int r = wk_reg(e, &v, ty);
		lval to = { .k = L_FRAME, .off = slots[i] = wk_slot(e, 8, 8), .align = 8 };

		x64_store(e, r, &to, 8);
		wk_free(e, &v);
	}
	for (uint32_t i = 0; i < call->c; i++) {
		qty ty = N(c->lists[call->b + i])->ty;
		lval from = { .k = L_FRAME, .off = slots[i], .align = 8 };

		x64_load(e, x64_argreg((int)i), &from, wk_size(c, ty), wk_signed(c, ty));
	}
	note_ref(e, REF(RK_SYM, callee));
	x64_put(e, X_TAIL)->ref = REF(RK_SYM, callee);
	return true;
}

VPRIV uint32_t x64_tail_ref(const ecx *e)
{
	const xst *X = e->x64;

	/* Only the common return label may follow this slice's terminal marker. */
	for (uint32_t k = X->nin; k-- > 0;)
		if (X->in[k].op != X_LABEL)
			return X->in[k].op == X_TAIL ? X->in[k].ref : 0;
	return 0;
}
