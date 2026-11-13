/* Direct register-only sibling calls. The shared proof limits this to exact
   forwarding wrappers; a terminal marker keeps ABI arguments live to exit. */
#include "x64impl.h"

VPRIV bool x64_tail_return(codegen_context *e, uint32_t expr)
{
	cc *c = e->c;
	walk_state *W = e->wk;
	uint32_t callee;
	int64_t slots[6];
	codegen_value values[6];
	const node *call;

	if (e->x64->interrupt || W->inl || W->inst || W->nclean || W->nvla || W->spneed || e->E->o->sprot ||
	    W->falign > 16 || !em_tail_candidate(e, W->fsym, expr, 6, &callee))
		return false;
	call = N(expr);
	/* Snapshot every argument before writing any ABI register. This handles
	   permutations, repeated arguments and homes overlapping incoming regs.
	   Literal values need no frame slot and are materialized after snapshots. */
	for (uint32_t i = 0; i < call->c; i++) {
		uint32_t a = c->lists[call->b + i];
		ast_qualified_type ty = N(a)->ty;
		values[i] = walk_value_emit(e, a);
		slots[i] = 0;
		if (values[i].k != CODEGEN_VALUE_CONSTANT) {
			int r = walk_value_to_register(e, &values[i], ty);
			codegen_lvalue to = { .k = CODEGEN_LVALUE_FRAME, .off = slots[i] = walk_slot_temporary(e, 8, 8), .align = 8 };

			x64_store(e, r, &to, 8);
			walk_value_free(e, &values[i]);
		}
	}
	for (uint32_t i = 0; i < call->c; i++) {
		ast_qualified_type ty = N(c->lists[call->b + i])->ty;
		if (slots[i]) {
			codegen_lvalue from = { .k = CODEGEN_LVALUE_FRAME, .off = slots[i], .align = 8 };

			x64_load(e, x64_argreg((int)i), &from, walk_type_size(c, ty), walk_type_is_signed(c, ty));
		} else {
			x64_op3(e, X_MOVI, walk_type_size(c, ty), x64_argreg((int)i), XNOREG, XNOREG)->imm =
				(int64_t)codegen_integer_truncate(c, values[i].i, ty);
		}
	}
	codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, callee));
	x64_put(e, X_TAIL)->ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, callee);
	return true;
}

VPRIV uint32_t x64_tail_ref(const codegen_context *e)
{
	const xst *X = e->x64;

	/* Only the common return label may follow this slice's terminal marker. */
	for (uint32_t k = X->nin; k-- > 0;)
		if (X->in[k].op != X_LABEL)
			return X->in[k].op == X_TAIL ? X->in[k].ref : 0;
	return 0;
}
