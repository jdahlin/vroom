/*
 * division-signed-constant.c: signed division or remainder by a constant as a multiply-high, shifts and a sign
 * correction, for the walker: the code behind the recipe of opt/sdivconst.c.
 *
 * walk_division_signed_constant_check says whether the target has the registers the sequence needs (the saved input and
 * the magic constant are the peak, WALK_LOWER(nfree) >= extra); walk_division_signed_constant_emit then builds the sequence through
 * the lowering interface WALK_LOWER() (smulhi for 64 bits, a widened multiply for 32), so it is the same for
 * every CPU and for VIR.  The caller checks _ok first and keeps operand evaluation order.
 */
#include "walk.h"

/* The saved input and materialized magic are the peak two extra registers.
 * The final sign-bit temporary reuses the magic's released allocation. */
VPRIV bool walk_division_signed_constant_check(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r)
{
	int extra = (a.k != CODEGEN_VALUE_REGISTER) + 1 + (r->add || r->remainder);
	return WALK_LOWER(nfree)(e) >= extra;
}

VPRIV codegen_value walk_division_signed_constant_emit(codegen_context *e, ast_qualified_type ty, codegen_value a, const em_sdivconst_recipe *r)
{
	int size = r->bits / 8, input = walk_value_to_register(e, &a, ty), saved = -1;
	codegen_value q;

	if (r->bits == 32)
		WALK_LOWER(ext)(e, input, 4, true);
	if (r->add || r->remainder) {
		saved = WALK_LOWER(treg)(e);
		WALK_LOWER(mov)(e, saved, input);
	}
	if (r->bits == 64)
		q = WALK_LOWER(smulhi)(e, a, codegen_value_constant(r->magic));
	else {
		q = WALK_LOWER(bin)(e, LEX_TOKEN_STAR, 8, false, a, codegen_value_constant(r->magic));
		q = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, true, q, codegen_value_constant(32));
	}
	if (r->add) {
		codegen_value n = { .k=CODEGEN_VALUE_REGISTER, .r=(uint8_t)saved };

		q = WALK_LOWER(bin)(e, LEX_TOKEN_PLUS, size, false, q, n);
		if (r->remainder)
			WALK_LOWER(tkeep)(e, saved);
	}
	if (r->shift)
		q = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, q, codegen_value_constant(r->shift));
	{
		int sign = WALK_LOWER(treg)(e);
		codegen_value s = { .k=CODEGEN_VALUE_REGISTER, .r=(uint8_t)sign };

		WALK_LOWER(mov)(e, sign, q.r);
		s = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, s, codegen_value_constant(r->bits - 1));
		q = WALK_LOWER(bin)(e, LEX_TOKEN_PLUS, size, false, q, s);
	}
	if (r->remainder) {
		q = WALK_LOWER(bin)(e, LEX_TOKEN_STAR, size, false, q, codegen_value_constant(r->divisor));
		return WALK_LOWER(bin)(e, LEX_TOKEN_MINUS, size, false, (codegen_value){ .k=CODEGEN_VALUE_REGISTER, .r=(uint8_t)saved }, q);
	}
	return r->negative ? WALK_LOWER(un)(e, LEX_TOKEN_MINUS, size, q) : q;
}
