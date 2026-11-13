/* Direct register-only sibling calls. The shared proof limits this to exact
   forwarding wrappers; a terminal marker keeps ABI arguments live to exit. */
#include "x64-internal.h"

VPRIV bool x64_tail_return(codegen_context *e, uint32_t expr)
{
	cc *c = e->compile_context;
	walk_state *W = e->walk_state;
	uint32_t callee;
	int64_t slots[6];
	codegen_value values[6];
	const node *call;

	if (e->x64_context->interrupt || W->inline_call || W->instance || W->cleanup_count || W->vla_count || W->stack_pointer_needed || e->unit->options->sprot ||
	    W->frame_alignment > 16 || !em_tail_candidate(e, W->function_symbol, expr, 6, &callee))
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
		if (values[i].kind != CODEGEN_VALUE_CONSTANT) {
			int r = walk_value_to_register(e, &values[i], ty);
			codegen_lvalue to = { .kind = CODEGEN_LVALUE_FRAME, .offset = slots[i] = walk_slot_temporary(e, 8, 8), .align = 8 };

			x64_store(e, r, &to, 8);
			walk_value_free(e, &values[i]);
		}
	}
	for (uint32_t i = 0; i < call->c; i++) {
		ast_qualified_type ty = N(c->lists[call->b + i])->ty;
		if (slots[i]) {
			codegen_lvalue from = { .kind = CODEGEN_LVALUE_FRAME, .offset = slots[i], .align = 8 };

			x64_load(e, x64_argument_register((int)i), &from, walk_type_size(c, ty), walk_type_is_signed(c, ty));
		} else {
			x64_record_emit_three_registers(e, X64_OP_MOVI, walk_type_size(c, ty), x64_argument_register((int)i), X64_REGISTER_NONE, X64_REGISTER_NONE)->immediate =
				(int64_t)codegen_integer_truncate(c, values[i].integer, ty);
		}
	}
	codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, callee));
	x64_record_emit(e, X64_OP_TAIL)->reference = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, callee);
	return true;
}

VPRIV uint32_t x64_tail_ref(const codegen_context *e)
{
	const x64_lowering_context *X = e->x64_context;

	/* Only the common return label may follow this slice's terminal marker. */
	for (uint32_t k = X->record_count; k-- > 0;)
		if (X->records[k].opcode != X64_OP_LABEL)
			return X->records[k].opcode == X64_OP_TAIL ? X->records[k].reference : 0;
	return 0;
}
