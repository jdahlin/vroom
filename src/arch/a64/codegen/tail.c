/* Bounded direct sibling calls. Park arguments before assigning ABI registers
 * so permutations never overwrite another argument's incoming value.
 */
#include "../../../codegen/codegen-internal.h"

VPRIV bool a64_tail_return(codegen_context *e, uint32_t expr)
{
	cc *c = e->compile_context;
	uint32_t callee;
	int64_t slots[8];
	codegen_value values[8];

	if (CODEGEN_IS_DARWIN(c) || e->unit->options->opt < 1 || e->unit->cleanups_disabled || !e->function_instance || e->function_instance->is_inlined || e->is_variadic ||
	    e->dynamic_frame || e->cleanup_count != e->function_instance->cleanup_base ||
	    !em_tail_candidate(e, e->function_instance->function, expr, 8, &callee))
		return false;
	const node *call = N(expr);

	for (uint32_t i = 0; i < call->c; i++) {
		uint32_t arg = c->lists[call->b + i];

		values[i] = gen(e, arg);
		slots[i] = 0;
		if (values[i].kind != CODEGEN_VALUE_CONSTANT) {
			int r = toreg(e, &values[i]);

			slots[i] = a64_frame_slot_allocate(e, 8, 8);
			a64_record_emit_memory(e, A64_OP_ST, 3, 0, r, A64_REGISTER_FRAME, slots[i]);
			val_free(e, &values[i]);
		}
	}
	for (uint32_t i = 0; i < call->c; i++) {
		ast_qualified_type ty = N(c->lists[call->b + i])->ty;
		int wide = codegen_type_size(c, ty) == 8;

		if (slots[i])
			a64_record_emit_memory(e, A64_OP_LD, wide ? 3 : 2, 0, (int)i, A64_REGISTER_FRAME, slots[i]);
		else
			a64_record_emit_move_immediate(e, wide, (int)i, (int64_t)codegen_integer_truncate(c, values[i].integer, ty));
	}
	uint32_t ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, callee);

	codegen_reference_note(e, ref);
	a64_record_emit(e, A64_OP_TAIL)->ref = ref;
	e->is_reachable = false;
	return true;
}

/* Keep the zero-code terminal marker to preserve debug record indices. */
VPRIV uint32_t a64_tail_ref(const codegen_context *e)
{
	uint32_t k = e->record_count;

	while (k && e->records[k - 1].op == A64_OP_LABEL)
		k--;
	return k && e->records[k - 1].op == A64_OP_TAIL && !e->records[k - 1].cond ? e->records[k - 1].ref : 0;
}
