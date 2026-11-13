/* Direct register-only sibling calls. The shared proof limits this to exact
   forwarding wrappers; a terminal marker keeps ABI arguments live to exit. */
#include "x64impl.h"

VPRIV bool x64_tail_return(ecx *e, uint32_t expr)
{
	cc *c = e->c;
	wkst *W = e->wk;
	uint32_t callee;
	int64_t slots[6];
	val values[6];
	const node *call;

	bool ms = e->x64->msfn;

	if (e->x64->interrupt || W->inl || W->inst || W->nclean || W->nvla || W->spneed || e->E->o->sprot ||
	    W->falign > 16 || !em_tail_candidate(e, W->fsym, expr, ms ? 4 : 6, &callee))
		return false;
	/* the same convention on both sides; Windows: not through __imp_ or .refptr. */
	if ((cc_func_abi(c, c->syms[callee].ty) == CC_FNABI_MS) != ms || (WIN64(c) && sym_needs_got(c, REF(RK_SYM, callee))))
		return false;
	call = N(expr);
	/* Snapshot every argument before writing any ABI register. This handles
	   permutations, repeated arguments and homes overlapping incoming regs.
	   Literal values need no frame slot and are materialized after snapshots. */
	for (uint32_t i = 0; i < call->c; i++) {
		uint32_t a = c->lists[call->b + i];
		qty ty = N(a)->ty;
		values[i] = wk_gen(e, a);
		slots[i] = 0;
		if (values[i].k != V_CONST) {
			int r = wk_reg(e, &values[i], ty);
			lval to = { .k = L_FRAME, .off = slots[i] = wk_slot(e, 8, 8), .align = 8 };

			x64_store(e, r, &to, 8);
			wk_free(e, &values[i]);
		}
	}
	for (uint32_t i = 0; i < call->c; i++) {
		qty ty = N(c->lists[call->b + i])->ty;
		if (slots[i]) {
			lval from = { .k = L_FRAME, .off = slots[i], .align = 8 };

			x64_load(e, ms ? x64w_argreg((int)i) : x64_argreg((int)i), &from, wk_size(c, ty), wk_signed(c, ty));
		} else {
			x64_op3(e, X_MOVI, wk_size(c, ty), ms ? x64w_argreg((int)i) : x64_argreg((int)i), XNOREG, XNOREG)->imm =
				(int64_t)em_trunc_to(c, values[i].i, ty);
		}
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
