/* arm64 lowering of VIR: returns, jumps between blocks, the d8-d15 save and sibling-call candidates. */

#include "lowering-internal.h"

/* the block a branch to t ends up in: past blocks that are a lone br without moves */
VPRIV uint32_t a64_vir_block_chase(a64_vir_context *L, uint32_t t) { return L->chases[t]; }

/* a return: the epilogue and ret, or a jump to the shared one (finish_function decides) */
/* d8-d15 as the caller had them: at the entry into their slots, before every way out back */
VPRIV void a64_vir_callee_saved_float_save(a64_vir_context *L, bool restore, int t)
{
	for (int r = 8; r < 16; r++)
		if (L->allocation->callee[1] >> r & 1) {
			if (restore) {
				a64_record_emit_memory(L->context, A64_OP_LD, 3, 0, t, A64_REGISTER_FRAME, L->callee_saved_float_offsets[r - 8]);
				a64_record_emit_word(L->context, a64_word_fmov_to_vector(1, r, t));
			} else {
				a64_record_emit_word(L->context, a64_word_fmov_from_vector(1, t, r));
				a64_record_emit_memory(L->context, A64_OP_ST, 3, 0, t, A64_REGISTER_FRAME, L->callee_saved_float_offsets[r - 8]);
			}
		}
}

VPRIV void a64_vir_return_lower(a64_vir_context *L)
{
	codegen_context *e = L->context;
	a64_record *t;

	if (!e->is_reachable)
		return;
	a64_vir_callee_saved_float_save(L, true, R_X16);
	t = a64_record_emit(e, A64_OP_TAIL);
	t->cond = 4;
	t->sh = L->is_before_save_point;
	t->label = L->return_label;
	e->label_reference_counts[L->return_label]++;
	e->is_reachable = false;
}

/*
 * Block t may be copied to the end of a block that jumps to it (tail duplication, as
 * LLVM's block placement): a return (its epilogue, or the jump to the shared one,
 * finish_function's choice), and a lone br (its edge's moves, then on) up to 8 deep.
 * Copies of blocks with work in them (a loop's test at the end of the latch) measured
 * more instructions on speedtest1: their constants and edge moves are lowered again.
 */
/* from block b's code, on to block t (an edge without moves left): a jump, or a copy of t */
VPRIV void a64_vir_goto_emit(a64_vir_context *L, uint32_t b, uint32_t t)
{
	/* into a loop whose test is at its bottom (a64_vir_blocks_order): a copy of the test here, so that
	 * the way in costs no jump (loop rotation with the guard, as LLVM's) */

	if (L->is_before_save_point && vir_block_dominates(L->function, L->save_point, t)) {	/* to the save point: its code runs behind the prologue */
		a64_jump_emit(L->context, L->labels[t]);
		return;
	}
	if (vir_layout_duplicate(L->function, L->loops, a64_vir_thread_state_instance.head_labels, b, t, a64_vir_block_fallthrough(L), L->dup, !L->context->unit->cleanups_disabled)) {
		int d = L->dup++;

		virbuild_lower_block(&L->drv, t);
		L->dup = d;
		return;
	}
	a64_jump_emit(L->context, a64_vir_block_label(L, t));
}

/* the call at i will be a sibling call (a64_vir_call_lower's test, before the registers are known) */
VPRIV bool a64_vir_call_is_sibling_candidate(a64_vir_context *L, uint32_t b, uint32_t i)
{
	const vir_function *f = L->function;
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[f->insns[i].b]);
	a64_vir_argument *a = __builtin_alloca(sizeof(a64_vir_argument) * (ng + 1));
	int32_t stk;

	a64_vir_arguments_assign(CODEGEN_IS_DARWIN(L->context->compile_context), &f->extra[f->insns[i].b], a, ng, &stk);
	return !stk && a64_vir_call_is_sibling(L, b, i);
}

/* instruction i of block b, its operands collected; res its result's register */
