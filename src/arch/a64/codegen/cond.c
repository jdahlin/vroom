/* vcc code generator: conditions and range folding */
#include "../../../codegen/codegen-internal.h"

/* ---------------- conditions ---------------- */

VPRIV void gen_cond(codegen_context *e, uint32_t n, bool jump_if, uint32_t l)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	int k = e->unit->options->opt && !e->unit->cleanups_disabled ? em_const_truth(e, n, a64_constant_p) : -1;
	if (k < 0) k = codegen_condition_fold(e, n);

	if (k >= 0) {
		if (k == (int)jump_if)
			a64_jump_emit(e, l);
		return;
	}
	switch (x->op) {
	case AST_NODE_LOGICAL_AND:
	case AST_NODE_LOGICAL_OR: {
		bool isand = x->op == AST_NODE_LOGICAL_AND;

		if (isand != jump_if) {
			/* && jumping on false / || jumping on true: both operands jump to l */
			gen_cond(e, x->a, jump_if, l);
			gen_cond(e, x->b, jump_if, l);
		} else {
			uint32_t skip = a64_label_new(e);

			gen_cond(e, x->a, !jump_if, skip);
			gen_cond(e, x->b, jump_if, l);
			deflabel(e, skip);
		}
		return;
	}
	case AST_NODE_UNARY:
		if (x->aux == LEX_TOKEN_EXCLAMATION) {
			gen_cond(e, x->a, !jump_if, l);
			return;
		}
		break;
	case AST_NODE_CAST:
		if (x->aux == AST_CAST_BOOL) {
			gen_cond(e, x->a, jump_if, l);
			return;
		}
		break;
	case AST_NODE_BINARY:
		if (codegen_operator_is_comparison(x->aux) && !codegen_type_is_wide(c, N(x->a)->ty) && !codegen_type_is_float(c, N(x->a)->ty)) {
			codegen_value v = gen(e, n);

			if (v.kind == CODEGEN_VALUE_CONSTANT || v.kind == CODEGEN_VALUE_ADDRESS) {
				test_zero(e, &v, x->ty, l, jump_if);
				return;
			}
			/* gen left a cset: turn it back into a branch on the flags */
			if (e->is_reachable && e->record_count && e->records[e->record_count - 1].opcode == A64_OP_CSINC && e->records[e->record_count - 1].rn == ZR &&
			    e->records[e->record_count - 1].rm == ZR && e->records[e->record_count - 1].rd == v.register_number) {
				int cond = e->records[e->record_count - 1].condition ^ 1;

				e->record_count--;
				a64_branch_condition_emit(e, jump_if ? cond : cond ^ 1, l);
				val_free(e, &v);
				return;
			}
			test_zero(e, &v, x->ty, l, jump_if);
			return;
		}
		break;
	}
	{
		codegen_value v = gen(e, n);

		test_zero(e, &v, x->ty, l, jump_if);
	}
}
