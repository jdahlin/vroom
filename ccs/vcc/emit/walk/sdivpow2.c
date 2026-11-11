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
		return r->negative ? LO(un)(e, T_MINUS, size, a) : a;
	}
	rb = LO(treg)(e);
	LO(mov)(e, rb, ra);
	bias = LO(bin)(e, T_GREATERGREATER, size, true, (val){ .k = V_REG, .r = (uint8_t)rb }, vconst(r->bits - 1));
	bias = LO(bin)(e, T_GREATERGREATER, size, false, bias, vconst(r->bits - r->shift));
	if (r->remainder) {
		int saved = LO(treg)(e);

		LO(mov)(e, saved, bias.r);
		a = LO(bin)(e, T_PLUS, size, false, a, bias);
		a = LO(bin)(e, T_AMP, size, false, a, vconst(((i128)1 << r->shift) - 1));
		return LO(bin)(e, T_MINUS, size, false, a, (val){ .k = V_REG, .r = (uint8_t)saved });
	}
	a = LO(bin)(e, T_PLUS, size, false, a, bias);
	a = LO(bin)(e, T_GREATERGREATER, size, true, a, vconst(r->shift));
	return r->negative ? LO(un)(e, T_MINUS, size, a) : a;
}

