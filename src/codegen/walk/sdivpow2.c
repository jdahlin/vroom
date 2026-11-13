/*
 * sdivpow2.c: signed division or remainder by a power of two for the walker: add a bias so that the
 * shift rounds toward zero, as C requires (recipe: opt/sdivpow2.c).
 *
 * wk_sdivpow2 builds the sequence through LO(), so it is the same for every CPU and for VIR.  Every
 * operation consumes its input register: the remainder case keeps a copy of the bias in a separate
 * temporary.  d == 1 (shift 0) is handled without a bias.
 */
#include "walk.h"

/* All operations consume their input register; retain a separate bias for %. */
VPRIV val wk_sdivpow2(ecx *e, qty t, val a, const em_sdivpow2_recipe *r)
{
	int size = r->bits / 8, ra = wk_reg(e, &a, t), rb;
	val bias;

	if (!r->shift) {
		if (r->remainder) {
			wk_free(e, &a);
			return vconst(0);
		}
		return r->negative ? LO(un)(e, LEX_TOKEN_MINUS, size, a) : a;
	}
	rb = LO(treg)(e);
	LO(mov)(e, rb, ra);
	bias = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, (val){ .k = V_REG, .r = (uint8_t)rb }, vconst(r->bits - 1));
	bias = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, false, bias, vconst(r->bits - r->shift));
	if (r->remainder) {
		int saved = LO(treg)(e);

		LO(mov)(e, saved, bias.r);
		a = LO(bin)(e, LEX_TOKEN_PLUS, size, false, a, bias);
		a = LO(bin)(e, LEX_TOKEN_AMPERSAND, size, false, a, vconst(((i128)1 << r->shift) - 1));
		return LO(bin)(e, LEX_TOKEN_MINUS, size, false, a, (val){ .k = V_REG, .r = (uint8_t)saved });
	}
	a = LO(bin)(e, LEX_TOKEN_PLUS, size, false, a, bias);
	a = LO(bin)(e, LEX_TOKEN_GREATER_GREATER, size, true, a, vconst(r->shift));
	return r->negative ? LO(un)(e, LEX_TOKEN_MINUS, size, a) : a;
}
