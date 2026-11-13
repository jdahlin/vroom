/*
 * division-signed-power-of-two.c: signed division or remainder by a power of two for the walker: add a bias so that the
 * shift rounds toward zero, as C requires (recipe: opt/sdivpow2.c).
 *
 * walk_division_signed_power_of_two_emit builds the sequence through WALK_LOWER(), so it is the same for every CPU and for VIR.  Every
 * operation consumes its input register: the remainder case keeps a copy of the bias in a separate
 * temporary.  d == 1 (shift 0) is handled without a bias.
 */
#include "walk.h"

/* All operations consume their input register; retain a separate bias for %. */
VPRIV codegen_value walk_division_signed_power_of_two_emit(codegen_context *e, ast_qualified_type t, codegen_value a, const em_sdivpow2_recipe *r)
{
	int size = r->bits / 8, ra = walk_value_to_register(e, &a, t), rb;
	codegen_value bias;

	if (!r->shift) {
		if (r->remainder) {
			walk_value_free(e, &a);
			return codegen_value_constant(0);
		}
		return r->negative ? WALK_LOWER(un)(e, LEX_TOKEN_MINUS, size, a) : a;
	}
	rb = WALK_LOWER(treg)(e);
	WALK_LOWER(mov)(e, rb, ra);
	bias = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, (codegen_value){ .k = CODEGEN_VALUE_REGISTER, .r = (uint8_t)rb }, codegen_value_constant(r->bits - 1));
	bias = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, bias, codegen_value_constant(r->bits - r->shift));
	if (r->remainder) {
		int saved = WALK_LOWER(treg)(e);

		WALK_LOWER(mov)(e, saved, bias.r);
		a = WALK_LOWER(bin)(e, LEX_TOKEN_PLUS, size, false, a, bias);
		a = WALK_LOWER(bin)(e, LEX_TOKEN_AMPERSAND, size, false, a, codegen_value_constant(((i128)1 << r->shift) - 1));
		return WALK_LOWER(bin)(e, LEX_TOKEN_MINUS, size, false, a, (codegen_value){ .k = CODEGEN_VALUE_REGISTER, .r = (uint8_t)saved });
	}
	a = WALK_LOWER(bin)(e, LEX_TOKEN_PLUS, size, false, a, bias);
	a = WALK_LOWER(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, a, codegen_value_constant(r->shift));
	return r->negative ? WALK_LOWER(un)(e, LEX_TOKEN_MINUS, size, a) : a;
}
