/*
 * division-unsigned-constant.c: unsigned division or remainder by a constant as a multiply-high and shifts, with the
 * correction step for large magic numbers, for the walker (recipe: opt/udivconst.c).
 *
 * walk_division_unsigned_constant_check says whether the target has the registers for the peak (a saved numerator and the
 * magic constant); walk_division_unsigned_constant_emit builds the sequence through WALK_LOWER() (a widened product for 32 bits,
 * WALK_LOWER(umulhi) for 64), so it is the same for every CPU and for VIR.  A divisor above 2^(W-1) is just a
 * compare.  The caller checks _ok first and keeps operand evaluation order.
 */
#include "walk.h"

/* During multiplication, a saved numerator and a materialized large magic
 * need two extra registers. Correction/rematerialized divisor never exceed
 * that peak; simple quotients need only the magic. A lazy address needs its
 * own input register before any of these temporaries are allocated. */
VPRIV bool walk_division_unsigned_constant_check(codegen_context *e, codegen_value a, const em_udivconst_recipe *r)
{
	int extra = (a.kind != CODEGEN_VALUE_REGISTER) + 1 + (r->add || r->remainder);
	return WALK_LOWER(nfree)(e) >= extra;
}

/* Unsigned32 uses a complete widened product. Unsigned64 asks the target for
 * the high half, including preservation of any implicit machine registers. */
VPRIV codegen_value walk_division_unsigned_constant_emit(codegen_context *e, ast_qualified_type ty, codegen_value a, const em_udivconst_recipe *r)
{
	int size = r->bits / 8, input = walk_value_to_register(e, &a, ty), saved = -1;
	if (r->bits == 32)
		WALK_LOWER(ext)(e, input, 4, false);
	if (r->remainder || r->add) {
		saved = WALK_LOWER(treg)(e);
		WALK_LOWER(mov)(e, saved, input);
	}
	codegen_value q;
	if (r->compare) {
		q = WALK_LOWER(cmp)(e, LEX_TOKEN_GREATER_EQUAL, size, false, a, codegen_value_constant(r->divisor));
	} else {
		if (r->bits == 64) {
			q = WALK_LOWER(umulhi)(e, a, codegen_value_constant(r->magic));
			if (!r->add && r->shift)
				q = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, false, q, codegen_value_constant(r->shift));
		} else {
			q = WALK_LOWER(bin)(e, LEX_TOKEN_STAR, 8, false, a, codegen_value_constant(r->magic));
			q = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, 8, false, q,
				    codegen_value_constant(r->add ? 32 : 32 + r->shift));
		}
		if (r->add) {
			int correction = saved;
			if (r->remainder) {
				correction = WALK_LOWER(treg)(e);
				WALK_LOWER(mov)(e, correction, saved);
			}
			codegen_value n = { .kind=CODEGEN_VALUE_REGISTER, .register_number=(uint8_t)correction };
			int qr = q.register_number;
			n = WALK_LOWER(bin)(e, LEX_TOKEN_MINUS, size, false, n, q);
			WALK_LOWER(tkeep)(e, qr);
			n = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, n, codegen_value_constant(1));
			q = WALK_LOWER(bin)(e, LEX_TOKEN_PLUS, size, false, n, q);
			q = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, q, codegen_value_constant(r->shift));
		}
	}
	if (r->remainder) {
		q = WALK_LOWER(bin)(e, LEX_TOKEN_STAR, size, false, q, codegen_value_constant(r->divisor));
		q = WALK_LOWER(bin)(e, LEX_TOKEN_MINUS, size, false, (codegen_value){ .kind=CODEGEN_VALUE_REGISTER, .register_number=(uint8_t)saved }, q);
	}
	return q;
}
