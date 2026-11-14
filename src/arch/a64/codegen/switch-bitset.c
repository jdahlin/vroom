/* Dispatch one stacked case run with a bounded membership mask. The selector
 * remains borrowed; only the index and shifted mask own temporaries.
 */
#include "../../../codegen/codegen-internal.h"

VPRIV bool a64_switch_bitset(codegen_context *e, uint32_t body, uint32_t base, int r, int cx,
			     bool sg, uint32_t dflt)
{
	em_switch_bitset_recipe recipe;

	if (e->unit->options->opt < 1 || e->unit->cleanups_disabled ||
	    !em_switch_bitset_plan(e->compile_context, body, e->case_count - base, cx ? 64 : 32, sg, &recipe))
		return false;
	int index = treg(e), mask = treg(e);
	int mx = recipe.bits == 64;

	/* A 32-bit subtraction also clears the index's upper bits when the
	 * membership mask needs a 64-bit variable shift. Guard first: A64 masks
	 * variable shift counts, which would otherwise alias out-of-range cases.
	 */
	if (recipe.lo)
		a64_record_emit_immediate(e, A64_OP_SUBI, cx, index, r, (int64_t)recipe.lo);
	else
		a64_record_emit_move(e, cx, index, r);
	a64_record_emit_immediate(e, A64_OP_SUBSI, cx, ZR, index, recipe.span);
	a64_branch_condition_emit(e, A64_CONDITION_HI, dflt);
	a64_record_emit_move_immediate(e, mx, mask, (int64_t)recipe.mask);
	a64_record_emit_three_registers(e, A64_OP_LSRV, mx, mask, mask, index);
	a64_record_emit_immediate(e, A64_OP_ANDI, mx, mask, mask, 1);
	a64_branch_compare_zero_emit(e, true, mx, mask, e->case_labels[recipe.node]);
	tfree(e, index);
	tfree(e, mask);
	a64_jump_emit(e, dflt);
	return true;
}
